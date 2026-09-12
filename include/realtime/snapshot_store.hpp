#ifndef REALTIME_SNAPSHOT_STORE_HPP
#define REALTIME_SNAPSHOT_STORE_HPP

#include <atomic>
#include <cstddef>
#include <deque>
#include <utility>

namespace realtime {

	/// Stores a sequence of published Ts such that one writer can publish a replacement output and
	/// multiple readers can flick over to the next one on a subsequent read in a thread-safe,
	/// lock-free way (but they'll continue accessing the previous one as necessary)
	///
	/// The motivation is to allow the audio-callback to be lock-free.
	///
	/// The reading is thread-safe and lock-free.
	///
	/// The writing (ie publish_new_output / erase_old_outputs()) is not thread-safe; there
	/// must only ever be at most one call to either of those methods at any one time.
	///
	/// erase_old_outputs() cleans up the previously published outputs. It may only be
	/// called when it is guaranteed that there are no readers.
	///
	/// The problem this sort of mechanism faces is cleanup --- when can the writing thread
	/// be sure that it's safe to clean up the old snapshots without the risk of one still being
	/// in use by one of the reading threads? There are various fancy ways to do this (eg hazard
	/// pointers) but this takes a much simpler approach of not really worrying about cleanup
	/// because, in the original audio use case, the outputs were small and infrequent. They
	/// can then be cleaned up whenever it's known to be safe (eg when the audio callback has
	/// stopped).
	///
	/// With cleanup out of the way, outputs are pushed onto the back of a std::deque
	/// and an atomic pointer marks the active one. A deque is used because push_back()
	/// never invalidates references to existing elements, and pop_front() only
	/// invalidates the element it removes. Readers never touch the deque itself, only
	/// the element the atomic pointer refers to, so the writer can keep adding outputs
	/// while they read. Cleanup must still wait until no reader can be using an older
	/// output.
	///
	/// Invariants:
	///  * !m_outputs.empty()
	///  * m_active_output_ptr points to &m_outputs.back()
	template <class T>
	class snapshot_store {
	 private:
		/// \brief The deque of outputs
		::std::deque<T> m_outputs;

		/// \brief A pointer to the currently active output in m_outputs
		::std::atomic<const T *> m_active_output_ptr;
		static_assert( decltype( m_active_output_ptr )::is_always_lock_free );

	 public:
		explicit snapshot_store( T );
		~snapshot_store() noexcept = default;

		snapshot_store()                                   = delete;
		snapshot_store( const snapshot_store & )            = delete;
		snapshot_store( snapshot_store && )                 = delete;
		snapshot_store &operator=( const snapshot_store & ) = delete;
		snapshot_store &operator=( snapshot_store && )      = delete;

		snapshot_store &publish_new_output( T );
		snapshot_store &erase_old_outputs() noexcept;

		[[nodiscard]] const T &active_output() const noexcept;

		[[nodiscard]] ::std::size_t size() const noexcept;
	};

	/// Construct from the first version of the output
	///
	/// \param prm_output The first version of the output
	template <class T>
	snapshot_store<T>::snapshot_store( T prm_output ) : m_active_output_ptr{ nullptr } {
		m_outputs.push_back( ::std::move( prm_output ) );

		// No other thread can observe the object before construction completes.
		m_active_output_ptr.store( &m_outputs.back(), ::std::memory_order_relaxed );
	}

	/// Publish a new output for readers to see
	///
	/// This is not thread-safe in that only one call to this or erase_old_outputs() can happen at a time
	///
	/// But it is thread-safe in the sense that it can be called whilst multiple readers are calling
	/// active_output()
	///
	/// If insertion throws, no new output is published and the active output remains unchanged.
	///
	/// \param prm_output The new version of the output to publish
	template <class T>
	snapshot_store<T> &snapshot_store<T>::publish_new_output( T prm_output ) {
		m_outputs.push_back( ::std::move( prm_output ) );
		// Publish only after the new value is fully constructed.
		m_active_output_ptr.store( &m_outputs.back(), ::std::memory_order_release );
		return *this;
	}

	/// Erase all previously published outputs
	///
	/// This can be called when it can be guaranteed that no other thread can be accessing this
	/// class or any of the previous outputs
	///
	/// This is not thread safe - the caller must ensure that for the duration of this call:
	///  * none of the other methods is being called
	///  * none of the previously published outputs are being accessed
	template <class T>
	snapshot_store<T> &snapshot_store<T>::erase_old_outputs() noexcept {
		while ( m_outputs.size() > 1 )
			m_outputs.pop_front();

		return *this;
	}

	/// Get the active output
	///
	/// This is thread-safe and lock-free
	///
	/// The returned reference remains valid until erase_old_outputs() is called,
	/// even if newer outputs are published in the meantime.
	template <class T>
	const T &snapshot_store<T>::active_output() const noexcept {
		// Synchronize with the release that published this pointer.
		return *m_active_output_ptr.load( ::std::memory_order_acquire );
	}

	/// Get the number of outputs in the store
	///
	/// Writer thread only, not for readers
	template <class T>
	::std::size_t snapshot_store<T>::size() const noexcept {
		return m_outputs.size();
	}

} // namespace realtime

#endif // REALTIME_SNAPSHOT_STORE_HPP
