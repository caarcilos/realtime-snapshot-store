#include <realtime/snapshot_store.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <iostream>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

	void require( const bool condition, const char *const message ) {
		if ( !condition )
			throw ::std::runtime_error{ message };
	}

	struct snapshot {
		static constexpr ::std::size_t PAYLOAD_SIZE = 16;

		::std::uint64_t                           sequence{};
		::std::array<::std::uint64_t, PAYLOAD_SIZE> payload{};
		::std::uint64_t                           guard{};

		explicit snapshot( const ::std::uint64_t value ) : sequence{ value }, guard{ ~value } {
			for ( ::std::size_t i = 0; i < payload.size(); ++i )
				payload[ i ] = expected_payload_value( value, i );
		}

		[[nodiscard]] bool is_valid() const noexcept {
			if ( guard != ~sequence )
				return false;

			for ( ::std::size_t i = 0; i < payload.size(); ++i )
				if ( payload[ i ] != expected_payload_value( sequence, i ) )
					return false;

			return true;
		}

	 private:
		[[nodiscard]] static constexpr ::std::uint64_t expected_payload_value(
		  const ::std::uint64_t value,
		  const ::std::size_t   index ) noexcept {
			return ( value * 0x9e3779b97f4a7c15ULL ) ^ static_cast<::std::uint64_t>( index );
		}
	};

	void newest_output_keeps_its_address_after_erase() {
		realtime::snapshot_store<snapshot> store{ snapshot{ 1 } };
		store.publish_new_output( snapshot{ 2 } );
		store.publish_new_output( snapshot{ 3 } );

		const snapshot *const newest_before_erase = &store.active_output();
		store.erase_old_outputs();

		require( &store.active_output() == newest_before_erase,
		         "erase_old_outputs() changed the address of the newest output" );
		require( store.active_output().sequence == 3, "erase_old_outputs() changed the newest output" );
	}

	void size_returns_to_one_after_erase() {
		realtime::snapshot_store<snapshot> store{ snapshot{ 1 } };
		store.publish_new_output( snapshot{ 2 } );
		store.publish_new_output( snapshot{ 3 } );

		require( store.size() == 3, "published outputs were not retained" );
		store.erase_old_outputs();
		require( store.size() == 1, "erase_old_outputs() did not retain exactly one output" );
	}

	void supports_move_only_values() {
		realtime::snapshot_store<::std::unique_ptr<int>> store{ ::std::make_unique<int>( 1 ) };
		store.publish_new_output( ::std::make_unique<int>( 2 ) );

		require( *store.active_output() == 2, "the move-only output was not published" );
		store.erase_old_outputs();
		require( store.size() == 1, "cleanup failed for move-only outputs" );
	}

	struct non_assignable_value {
		const int value;

		explicit non_assignable_value( const int initial_value ) : value{ initial_value } {}
	};

	static_assert( !::std::is_move_assignable_v<non_assignable_value> );

	void erase_supports_non_assignable_values() {
		realtime::snapshot_store<non_assignable_value> store{ non_assignable_value{ 1 } };
		store.publish_new_output( non_assignable_value{ 2 } );
		const non_assignable_value *const newest_before_erase = &store.active_output();

		store.erase_old_outputs();

		require( &store.active_output() == newest_before_erase,
		         "cleanup moved the surviving non-assignable output" );
		require( store.active_output().value == 2, "cleanup changed the non-assignable output" );
		require( store.size() == 1, "cleanup failed for non-assignable outputs" );
	}

	class deliberate_move_error : public ::std::exception {
	 public:
		[[nodiscard]] const char *what() const noexcept override {
			return "deliberate move failure";
		}
	};

	struct throwing_value {
		static inline bool throw_on_move = false;

		int value{};

		explicit throwing_value( const int initial_value ) : value{ initial_value } {
		}

		throwing_value( const throwing_value & )            = default;
		throwing_value &operator=( const throwing_value & ) = default;

		throwing_value( throwing_value &&other ) {
			if ( throw_on_move )
				throw deliberate_move_error{};

			value = other.value;
		}

		throwing_value &operator=( throwing_value && ) = default;
	};

	void failed_publish_leaves_active_output_unchanged() {
		realtime::snapshot_store<throwing_value> store{ throwing_value{ 7 } };
		const throwing_value *const               active_before_publish = &store.active_output();
		const throwing_value                     candidate{ 99 };

		bool threw = false;
		throwing_value::throw_on_move = true;
		try {
			// Copying candidate into the by-value parameter succeeds; moving that
			// parameter into the deque is the operation that deliberately throws.
			store.publish_new_output( candidate );
		} catch ( const deliberate_move_error & ) {
			threw = true;
		}
		throwing_value::throw_on_move = false;

		require( threw, "the test value did not throw while being published" );
		require( &store.active_output() == active_before_publish, "a failed publish changed the active address" );
		require( store.active_output().value == 7, "a failed publish changed the active value" );
		require( store.size() == 1, "a failed publish retained a partial output" );
	}

	void concurrent_readers_observe_complete_monotonic_values() {
		constexpr ::std::uint64_t PUBLICATION_COUNT = 50'000;
		constexpr ::std::size_t   READER_COUNT      = 4;

		realtime::snapshot_store<snapshot> store{ snapshot{ 0 } };
		::std::atomic<bool>                    finished{ false };
		::std::atomic<::std::size_t>           invalid_reads{ 0 };
		::std::atomic<::std::size_t>           backwards_reads{ 0 };
		::std::latch                          readers_running{ READER_COUNT };

		::std::vector<::std::thread> readers;
		readers.reserve( READER_COUNT );

		for ( ::std::size_t i = 0; i < READER_COUNT; ++i ) {
			readers.emplace_back( [ & ] {
				const snapshot &initial = store.active_output();
				::std::uint64_t last_seen_sequence = initial.sequence;
				readers_running.count_down();

				if ( !initial.is_valid() ) {
					invalid_reads.fetch_add( 1, ::std::memory_order_relaxed );
					return;
				}

				do {
					const snapshot &current = store.active_output();
					if ( !current.is_valid() ) {
						invalid_reads.fetch_add( 1, ::std::memory_order_relaxed );
						return;
					}

					if ( current.sequence < last_seen_sequence ) {
						backwards_reads.fetch_add( 1, ::std::memory_order_relaxed );
						return;
					}

					last_seen_sequence = current.sequence;
				} while ( !finished.load( ::std::memory_order_acquire ) );
			} );
		}

		readers_running.wait();
		for ( ::std::uint64_t sequence = 1; sequence <= PUBLICATION_COUNT; ++sequence )
			store.publish_new_output( snapshot{ sequence } );
		finished.store( true, ::std::memory_order_release );

		for ( auto &reader : readers )
			reader.join();

		require( invalid_reads.load( ::std::memory_order_relaxed ) == 0,
		         "a reader observed a partially built value" );
		require( backwards_reads.load( ::std::memory_order_relaxed ) == 0,
		         "a reader observed versions moving backwards" );
		require( store.active_output().sequence == PUBLICATION_COUNT,
		         "the final publication was not visible after the threads joined" );
	}

} // namespace

int main() {
	using test_fn = void ( * )();
	const ::std::vector<::std::pair<const char *, test_fn>> tests{
	  { "newest output keeps its address after erase", newest_output_keeps_its_address_after_erase },
	  { "size returns to one after erase", size_returns_to_one_after_erase },
	  { "move-only values are supported", supports_move_only_values },
	  { "erase supports non-assignable values", erase_supports_non_assignable_values },
	  { "failed publish leaves active output unchanged", failed_publish_leaves_active_output_unchanged },
	  { "concurrent readers observe complete monotonic values", concurrent_readers_observe_complete_monotonic_values },
	};

	for ( const auto &[ name, test ] : tests ) {
		try {
			test();
			::std::cout << "[pass] " << name << '\n';
		} catch ( const ::std::exception &error ) {
			::std::cerr << "[fail] " << name << ": " << error.what() << '\n';
			return 1;
		}
	}

	return 0;
}
