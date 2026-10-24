/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

namespace KODI::XBOX_STARTUP
{
enum class Stage
{
  Main,
  Network,
  Environment,
  CreateApplication,
  CreateGUI,
  Initialize,
  Running,
  Teardown,
  Finished,
  UnloadSkin,
  RendererShutdown,
  DestroyWindows,
  DeinitServices
};

#ifdef KODI_XBOX_STARTUP_DIAGNOSTICS
void Start();
void Finish();
void SetStage(Stage stage);
void RecordPresent(bool rendered);
bool ShouldStop();
void Checkpoint(const char* name);
#else
inline void Start() {}
inline void Finish() {}
inline void SetStage(Stage) {}
inline void RecordPresent(bool) {}
inline bool ShouldStop() { return false; }
inline void Checkpoint(const char*) {}
#endif
}
