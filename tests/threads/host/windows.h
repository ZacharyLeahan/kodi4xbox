#pragma once
// Host-only semaphore adapter for testing the real Kodi Condition.h.
// This does not validate NXDK's Win32 implementation or Xbox scheduling.
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
using DWORD = uint32_t;
using LONG = int32_t;
constexpr DWORD INFINITE = 0xffffffffU, WAIT_OBJECT_0 = 0, WAIT_TIMEOUT = 258;
struct SemaphoreHandle
{
  std::mutex mutex;
  std::condition_variable condition;
  long count;
};
using HANDLE = SemaphoreHandle *;
inline HANDLE CreateSemaphore(void *, long initial, long, void *)
{
  return new SemaphoreHandle{{}, {}, initial};
}
inline void CloseHandle(HANDLE h) { delete h; }
inline LONG InterlockedIncrement(volatile LONG *value)
{
  return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST);
}
inline LONG InterlockedDecrement(volatile LONG *value)
{
  return __atomic_sub_fetch(value, 1, __ATOMIC_SEQ_CST);
}
inline bool ReleaseSemaphore(HANDLE h, long n, void *)
{
  std::lock_guard<std::mutex> lock(h->mutex);
  h->count += n;
  h->condition.notify_all();
  return true;
}
inline DWORD WaitForSingleObject(HANDLE h, DWORD ms)
{
  std::unique_lock<std::mutex> lock(h->mutex);
  if (ms == INFINITE)
    h->condition.wait(lock, [&] { return h->count > 0; });
  else if (!h->condition.wait_for(lock, std::chrono::milliseconds(ms),
                                  [&] { return h->count > 0; }))
    return WAIT_TIMEOUT;
  --h->count;
  return WAIT_OBJECT_0;
}
