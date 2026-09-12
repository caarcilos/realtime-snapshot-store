# Lock-Free Store

A small C++20 header-only utility for publishing immutable values from one
writer to multiple readers. It is adapted and anonymized from production code
used to keep a real-time audio callback free from locks.

This pattern is useful in multithreaded, latency-sensitive systems where a
reader cannot wait for a mutex, such as an audio callback, render loop, or UI
thread consuming state produced elsewhere. The reader path is lock-free and
non-blocking; publication may still allocate memory.

The writer appends a fully constructed value and publishes its address with a
release store. Readers use an acquire load and may keep the returned reference
while newer values are published. Old values must only be erased when no reader
can still be using them.

Only `active_output()` is safe to call concurrently with publication. The
writer owns `publish_new_output()`, `erase_old_outputs()`, and `size()`.

## Build and test

```sh
cmake -S . -B build -DWARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests cover lifetime, exception safety, move-only and non-assignable values,
and concurrent publication. CI is configured to run GCC, Clang, MSVC, Apple
Silicon, ThreadSanitizer, AddressSanitizer, and UndefinedBehaviorSanitizer.

Sanitizer builds can also be enabled with `ENABLE_TSAN` or
`ENABLE_ASAN_UBSAN`; the two options are mutually exclusive.
