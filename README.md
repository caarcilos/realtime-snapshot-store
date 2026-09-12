# Realtime Snapshot Store

[![CI](https://github.com/caarcilos/realtime-snapshot-store/actions/workflows/ci.yml/badge.svg)](https://github.com/caarcilos/realtime-snapshot-store/actions/workflows/ci.yml)

A header-only C++20 class that lets real-time threads read the latest value
published by another thread, with a single atomic load and no locks.

It's useful anywhere a thread needs lock-free, non-blocking access to changing
data: an audio callback picking up new parameters or generated patterns from the
UI, a render loop reading the current scene snapshot while a worker prepares the
next one, etc. The reader path never locks, allocates or waits.

## Usage

```cpp
#include <realtime/snapshot_store.hpp>

realtime::snapshot_store<pattern> store{ pattern{} };

// Writer (UI thread): build off the real-time path, then publish.
store.publish_new_output( render_pattern( settings ) );

// Reader (audio thread): lock-free and non-blocking.
const pattern &current = store.active_output();

// Writer, once no reader can be active (e.g. playback stopped).
store.erase_old_outputs();
```

## How it works

Each value is appended to a `std::deque` and published through an atomic pointer:
a release store on the writer and an acquire load on readers. Old values stay
alive so readers can safely finish processing them. Cleanup is explicit and
must only happen when no readers are active (the client app should decide when).

This design works well when values change at human speed and there is a natural
safe point for cleanup, such as when playback stops in the audio domain.

## Tests

The tests cover stable addresses through cleanup, size after cleanup, move-only
and non-assignable values, strong exception safety, and concurrent readers that
must never observe partial or decreasing versions.

CI runs the tests on Linux with GCC and Clang, Windows with MSVC, and Apple
Silicon macOS, plus dedicated ThreadSanitizer and ASan/UBSan jobs.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Add `-DENABLE_TSAN=ON` or `-DENABLE_ASAN_UBSAN=ON` for a sanitizer build.
