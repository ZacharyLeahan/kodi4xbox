# Condition-variable regression

On macOS or Linux, from the repository root:

```sh
mkdir -p build/condition-host
c++ -std=c++17 -pthread -DNXDK -Itests/threads/host -Ixbmc \
  tests/threads/host/condition.cpp -o build/condition-host/test
build/condition-host/test
```

This compiles the actual Kodi condition-variable header, with a host adapter
for its Win32 semaphore calls. It checks notification while the predicate
mutex is held, and release/restoration of recursively acquired locks while
waiting. A five-second alarm bounds deadlocks.

Before the fix, `notify()` threw `unique_lock::lock: already locked` and the
test failed. After changing that call to `unlock()`, both checks pass.
The adapter does not validate NXDK kernel primitives or establish the cause
of the separate Xbox video-browser stall.
