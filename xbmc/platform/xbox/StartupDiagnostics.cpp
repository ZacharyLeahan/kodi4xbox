/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "StartupDiagnostics.h"

#include <hal/xbox.h>
#include <windows.h>
#include <cstdio>

namespace KODI::XBOX_STARTUP
{
namespace
{
volatile LONG stage = 0;
volatile LONG presents = 0;
volatile LONG renderedPresents = 0;
volatile LONG stopRequested = 0;
HANDLE observerThread = nullptr;
constexpr const char* names[] = {"main", "network", "environment", "create-application",
                               "create-gui", "initialize", "running", "teardown", "finished",
                               "unload-skin", "renderer-shutdown", "destroy-windows", "deinit-services"};

DWORD WINAPI Observe(void*)
{
  const DWORD start = GetTickCount();
  FILE* log = fopen("Q:\\diagnostics\\startup.log", "w");
  if (log)
  {
    fprintf(log, "xbe_limit64=%u\n",
            (CURRENT_XBE_HEADER->InitFlags & (1U << 2)) != 0);
    fflush(log);
  }
  LONG previous = -1;
  DWORD lastWrite = start;
  while (true)
  {
    const DWORD now = GetTickCount();
    const LONG current = InterlockedCompareExchange(&stage, 0, 0);
    if (current != previous || now - lastWrite >= 5000)
    {
      MM_STATISTICS memory = {};
      memory.Length = sizeof(memory);
      const NTSTATUS status = MmQueryStatistics(&memory);
      if (log)
      {
        fprintf(log, "elapsed_ms=%lu stage=%s memory_status=%08lx total_kib=%lu free_kib=%lu presents=%ld rendered=%ld\n",
                now - start, names[current], static_cast<unsigned long>(status),
                memory.TotalPhysicalPages * (PAGE_SIZE / 1024),
                memory.AvailablePages * (PAGE_SIZE / 1024),
                InterlockedCompareExchange(&presents, 0, 0),
                InterlockedCompareExchange(&renderedPresents, 0, 0));
        fprintf(log, "memory_kib: image=%lu cache=%lu pool=%lu stacks=%lu virtual_committed=%lu\n",
                memory.ImagePagesCommitted * (PAGE_SIZE / 1024),
                memory.CachePagesCommitted * (PAGE_SIZE / 1024),
                memory.PoolPagesCommitted * (PAGE_SIZE / 1024),
                memory.StackPagesCommitted * (PAGE_SIZE / 1024),
                memory.VirtualMemoryBytesCommitted / 1024);
        fflush(log);
      }
      previous = current;
      lastWrite = now;
    }
    // Let Kodi stop submitting GPU work and release pbkit before quick reboot.
    if (now - start >= 100000 && current == static_cast<LONG>(Stage::Running))
      InterlockedExchange(&stopRequested, 1);
    // This is an opt-in, bounded hardware probe, not a production watchdog.
    if (now - start >= 120000 || current == static_cast<LONG>(Stage::Finished))
    {
      if (log)
      {
        fprintf(log, "Returning to dashboard after bounded startup probe\n");
        fclose(log);
      }
      XLaunchXBE("E:\\Dashboard\\default.xbe");
      return 0;
    }
    Sleep(250);
  }
}
}

void Start()
{
  CreateDirectoryA("Q:\\diagnostics", nullptr);
  DeleteFileA("Q:\\diagnostics\\shutdown.log");
  observerThread = CreateThread(nullptr, 0, Observe, nullptr, 0, nullptr);
}

void Finish()
{
  SetStage(Stage::Finished);
  // Keep the CRT alive until the observer has flushed and closed its log and
  // handed control back to the dashboard.
  if (observerThread)
  {
    WaitForSingleObject(observerThread, INFINITE);
    CloseHandle(observerThread);
    observerThread = nullptr;
  }
}

void SetStage(Stage value)
{
  Checkpoint(names[static_cast<unsigned int>(value)]);
  InterlockedExchange(&stage, static_cast<LONG>(value));
}

void RecordPresent(bool rendered)
{
  InterlockedIncrement(&presents);
  if (rendered)
    InterlockedIncrement(&renderedPresents);
}

bool ShouldStop()
{
  return InterlockedCompareExchange(&stopRequested, 0, 0) != 0;
}

void Checkpoint(const char* name)
{
  // Use a separate, synchronous kernel file handle: teardown can stall while
  // the observer sleeps or while another thread owns a C runtime file lock.
  HANDLE file = CreateFileA("Q:\\diagnostics\\shutdown.log", GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return;
  SetFilePointer(file, 0, nullptr, FILE_END);
  char line[192];
  const int length = snprintf(line, sizeof(line), "tick=%lu thread=%lu checkpoint=%s\r\n",
                              GetTickCount(), GetCurrentThreadId(), name);
  DWORD written;
  WriteFile(file, line, length, &written, nullptr);
  IO_STATUS_BLOCK io;
  NtFlushBuffersFile(file, &io);
  CloseHandle(file);
}
}
