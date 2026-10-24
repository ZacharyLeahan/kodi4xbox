/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinSystemXboxGL.h"

#include "ServiceBroker.h"
#include "peripherals/Peripherals.h"
#include "settings/DisplaySettings.h"
#include "windowing/GraphicContext.h"
#include "windowing/WindowSystemFactory.h"
#include "utils/log.h"
#include "platform/xbox/StartupDiagnostics.h"

#include <hal/video.h>
#include <pbgl.h>
#include <pbkit/pbkit.h>

#include <SDL.h>

void CWinSystemXboxGL::Register()
{
  KODI::WINDOWING::CWindowSystemFactory::RegisterWindowSystem(CreateWinSystem);
}

std::unique_ptr<CWinSystemBase> CWinSystemXboxGL::CreateWinSystem()
{
  return std::make_unique<CWinSystemXboxGL>();
}

bool CWinSystemXboxGL::DestroyWindowSystem()
{
  // CApplication destroys GUI resources after DestroyRenderSystem(). Their
  // texture destructors still call GL, so release the context only here.
  KODI::XBOX_STARTUP::Checkpoint("pbgl: shutdown");
  pbgl_shutdown();
  KODI::XBOX_STARTUP::Checkpoint("pbgl: shutdown complete");
  return true;
}

void CWinSystemXboxGL::PresentRenderImpl(bool rendered)
{
  KODI::XBOX_STARTUP::RecordPresent(rendered);
#ifdef KODI_XBOX_STARTUP_DIAGNOSTICS
  static bool reported = false;
  if (!reported)
  {
    GLint scissor[4] = {};
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    CLog::Log(LOGINFO, "Xbox probe: rendered={} viewport={},{},{},{} scissor={},{},{},{} buffers={}x{} glerror={}",
              rendered, m_viewPort[0], m_viewPort[1], m_viewPort[2], m_viewPort[3],
              scissor[0], scissor[1], scissor[2], scissor[3],
              pb_back_buffer_width(), pb_back_buffer_height(), glGetError());
    reported = true;
  }
#endif
  if (rendered)
    pbgl_swap_buffers();
}

bool CWinSystemXboxGL::CreateNewWindow(const std::string& name, bool fullScreen, RESOLUTION_INFO& res)
{
  if (!SetFullScreen(fullScreen, res, false))
    return false;

  return true;
}

bool CWinSystemXboxGL::SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays)
{
  // Xbox is always fullscreen
  m_nWidth      = res.iWidth;
  m_nHeight     = res.iHeight;

  // Kodi requests the same mode again after pbkit has taken ownership of scanout.
  // Reinitializing HAL video here replaces its framebuffer and display state.
  const VIDEO_MODE current = XVideoGetMode();
  if (current.width != m_nWidth || current.height != m_nHeight || current.bpp != 32)
  {
    if (!XVideoSetMode(m_nWidth, m_nHeight, 32, REFRESH_DEFAULT))
    {
      CLog::Log(LOGERROR, "Unable to set Xbox video mode {}x{}", m_nWidth, m_nHeight);
      return false;
    }
  }
  CRenderSystemGL::ResetRenderSystem(m_nWidth, m_nHeight);

  return true;
}

RESOLUTION_INFO CWinSystemXboxGL::GetResolutionInfo(RESOLUTION res)
{
  RESOLUTION_INFO info;
  switch (res)
  {
  case RES_HDTV_1080i:
    UpdateDesktopResolution(info, "", 1920, 1080, REFRESH_60HZ, D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_WIDESCREEN);
    break;
  case RES_HDTV_720p:
    UpdateDesktopResolution(info, "", 1280, 720, REFRESH_60HZ, D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN);
    break;
  case RES_HDTV_480p_4x3:
    UpdateDesktopResolution(info, "", 720, 480, REFRESH_60HZ, D3DPRESENTFLAG_PROGRESSIVE);
    info.iSubtitles = (int)(0.9 * 480);
    info.fPixelRatio = 4320.0f / 4739.0f;
    break;
  case RES_HDTV_480p_16x9:
    UpdateDesktopResolution(info, "", 720, 480, REFRESH_60HZ, D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN);
    info.fPixelRatio = 4320.0f / 4739.0f*4.0f / 3.0f;
    break;
  case RES_NTSC_4x3:
    UpdateDesktopResolution(info, "", 720, 480, REFRESH_60HZ, D3DPRESENTFLAG_INTERLACED);
    info.iSubtitles = (int)(0.9 * 480);
    info.fPixelRatio = 4320.0f / 4739.0f;
    break;
  case RES_NTSC_16x9:
    UpdateDesktopResolution(info, "", 720, 480, REFRESH_60HZ, D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_WIDESCREEN);
    info.fPixelRatio = 4320.0f / 4739.0f*4.0f / 3.0f;
    break;
  case RES_PAL_4x3:
    UpdateDesktopResolution(info, "", 720, 576, REFRESH_50HZ, D3DPRESENTFLAG_INTERLACED);
    info.iSubtitles = (int)(0.9 * 576);
    info.fPixelRatio = 128.0f / 117.0f;
    break;
  case RES_PAL_16x9:
    UpdateDesktopResolution(info, "", 720, 576, REFRESH_50HZ, D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_WIDESCREEN);
    info.fPixelRatio = 128.0f / 117.0f*4.0f / 3.0f;
    break;
  case RES_PAL60_4x3:
    UpdateDesktopResolution(info, "", 720, 480, REFRESH_60HZ, D3DPRESENTFLAG_INTERLACED);
    info.iSubtitles = (int)(0.9 * 480);
    info.fPixelRatio = 4320.0f / 4739.0f;
    break;
  case RES_PAL60_16x9:
    UpdateDesktopResolution(info, "", 720, 480, REFRESH_60HZ, D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_WIDESCREEN);
    info.fPixelRatio = 4320.0f / 4739.0f*4.0f / 3.0f;
    break;
  default:
    break;
  }
  info.iScreenWidth  = info.iWidth;
  info.iScreenHeight = info.iHeight;
  info.Overscan.right = info.iWidth;
  info.Overscan.bottom = info.iHeight;

  return info;
}

void CWinSystemXboxGL::UpdateResolutions()
{
  // erase previous stored modes
  CDisplaySettings::GetInstance().ClearCustomResolutions();

  VIDEO_MODE xmode = {};
  VIDEO_MODE desktop = {};
  void *p = NULL;
  while(XVideoListModes(&xmode, 0, 0, &p))
  {
    // ignore 640 wide modes
    if (xmode.width < 720)
      continue;

    RESOLUTION_INFO info;
    if (xmode.width == 1920 && xmode.height == 1080)
      info = GetResolutionInfo(RES_HDTV_1080i);
    else if (xmode.width == 1280)
      info = GetResolutionInfo(RES_HDTV_720p);
    else if (xmode.width == 720 && xmode.height == 480)
      info = GetResolutionInfo(RES_HDTV_480p_4x3);
    else if (xmode.width == 720 && xmode.height == 576)
      info = GetResolutionInfo(RES_PAL_4x3);
    else
      continue;

    CDisplaySettings::GetInstance().AddResolutionInfo(info);
    // SD is the hardware-validated default while HD rendering is investigated.
    // Prefer 480-line output, then PAL SD, rather than the last enumerated mode.
    if (desktop.width == 0 ||
        (xmode.height <= 576 && desktop.height > 576) ||
        (xmode.height == 480 && desktop.height == 576))
      desktop = xmode;
  }

  if (desktop.width == 0)
  {
    CLog::Log(LOGERROR, "No supported Xbox video modes available");
    return;
  }

  // Keep the default independent of the enumeration's final output parameter.
  RESOLUTION_INFO& info = CDisplaySettings::GetInstance().GetResolutionInfo(RES_DESKTOP);
  UpdateDesktopResolution(info, "RES_DESKTOP", desktop.width, desktop.height, desktop.refresh, 0);

  CDisplaySettings::GetInstance().ApplyCalibrations();
}

bool CWinSystemXboxGL::MessagePump()
{
  SDL_Event event;
  while (SDL_PollEvent(&event))
  {
    CServiceBroker::GetPeripherals().UpdateJoystick(event);
  }
  return true;
}
