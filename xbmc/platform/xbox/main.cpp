/*
 *  Copyright (C) 2023-2025 Team XBMC
 *  This file is part of XBMC - https://xbmc.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "application/AppEnvironment.h"
#include "application/AppParamParser.h"
#include "platform/xbmc.h"
#include "platform/xbox/StartupDiagnostics.h"
#include "XboxDashboard.h"

#include <nxdk/mount.h>
#include <nxdk/path.h>
#include <nxdk/net.h>
#include <hal/xbox.h>
#include <windows.h>

namespace
{
void WriteExitCheckpoint(HANDLE file, const char* message)
{
  if (file == INVALID_HANDLE_VALUE)
    return;
  DWORD written;
  WriteFile(file, message, static_cast<DWORD>(strlen(message)), &written, nullptr);
  IO_STATUS_BLOCK io;
  NtFlushBuffersFile(file, &io);
}
}

int main()
{
  nxUnmountDrive('D');
  nxMountDrive('D', "\\Device\\CdRom0");
  nxMountDrive('C', "\\Device\\Harddisk0\\Partition2\\");
  nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\");
  nxMountDrive('X', "\\Device\\Harddisk0\\Partition3\\");
  nxMountDrive('Y', "\\Device\\Harddisk0\\Partition4\\");
  nxMountDrive('Z', "\\Device\\Harddisk0\\Partition5\\");
  // Mount the root is active xbe to Q:
  {
    char targetPath[260];
    nxGetCurrentXbeNtPath(targetPath);
    *(strrchr(targetPath, '\\') + 1) = '\0';
    nxMountDrive('Q', targetPath);
  }

  DeleteFileA("Q:\\exit.log");
  KODI::XBOX_STARTUP::Start();
  KODI::XBOX_STARTUP::SetStage(KODI::XBOX_STARTUP::Stage::Network);
  nxNetInit(NULL);

  char* argv[] = {"", "--fullscreen", "--standalone"};
  CAppParamParser appParamParser;
  appParamParser.Parse(argv, 3);

  KODI::XBOX_STARTUP::SetStage(KODI::XBOX_STARTUP::Stage::Environment);
  CAppEnvironment::SetUp(appParamParser.GetAppParams());
  int status = XBMC_Run(true);
  // Keep this small trace independent of Kodi's logger and CRT teardown.
  HANDLE exitLog = CreateFileA("Q:\\exit.log", GENERIC_WRITE, FILE_SHARE_READ,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  WriteExitCheckpoint(exitLog, "application cleanup complete\r\n");
  KODI::XBOX_STARTUP::SetStage(KODI::XBOX_STARTUP::Stage::Teardown);
  CAppEnvironment::TearDown();
  WriteExitCheckpoint(exitLog, "environment teardown complete\r\n");
  const char* dashboard = KODI_XBOX_DASHBOARD_PATH[0] ? KODI_XBOX_DASHBOARD_PATH : nullptr;
  WriteExitCheckpoint(exitLog, dashboard ? dashboard : "system dashboard");
  WriteExitCheckpoint(exitLog, "\r\nlaunching dashboard\r\n");
  if (exitLog != INVALID_HANDLE_VALUE)
    CloseHandle(exitLog);
  KODI::XBOX_STARTUP::Finish();

  // Match the explicit launch used by the successful diagnostic return path.
  XLaunchXBE(dashboard);
  return status;
}
