#include "threads/Condition.h"
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>
#include <unistd.h>
int main()
{
  // Bound a regression that would otherwise leave the test runner blocked.
  alarm(5);
  CCriticalSection mutex;
  XbmcThreads::ConditionVariable condition;
  bool ready = false, released = false;
  std::promise<void> waiting;
  auto waiter = std::thread(
      [&]
      {
        std::unique_lock<CCriticalSection> first(mutex), second(mutex);
        waiting.set_value();
        while (!ready)
          condition.wait(mutex, 1000);
        released = true;
      });
  waiting.get_future().wait();
  bool threw = false;
  {
    std::unique_lock<CCriticalSection> lock(mutex);
    ready = true;
    try
    {
      condition.notify();
    }
    catch (const std::exception &e)
    {
      std::printf("notify threw: %s\n", e.what());
      threw = true;
    }
  }
  waiter.join();
  std::printf("notify=%s recursive_wait=%s\n", threw ? "FAIL" : "PASS", released ? "PASS" : "FAIL");
  return threw || !released;
}
