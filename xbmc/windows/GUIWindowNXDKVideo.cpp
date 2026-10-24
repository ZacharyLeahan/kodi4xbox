/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "GUIWindowNXDKVideo.h"
#include "ServiceBroker.h"
#include "application/Application.h"
#include "application/ApplicationComponents.h"
#include "application/ApplicationPlayer.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIWindowManager.h"
#include "input/actions/Action.h"
#include "input/actions/ActionIDs.h"
#include "windowing/WinSystem.h"

CGUIWindowNXDKVideo::CGUIWindowNXDKVideo() : CGUIWindow(WINDOW_FULLSCREEN_VIDEO, "")
{
  m_loadType = LOAD_ON_GUI_INIT;
}
void CGUIWindowNXDKVideo::OnInitWindow()
{
  CGUIWindow::OnInitWindow();
  CServiceBroker::GetWinSystem()->GetGfxContext().SetFullScreenVideo(true);
}
void CGUIWindowNXDKVideo::OnDeinitWindow(int nextWindowID)
{
  CServiceBroker::GetWinSystem()->GetGfxContext().SetFullScreenVideo(false);
  CGUIWindow::OnDeinitWindow(nextWindowID);
}
void CGUIWindowNXDKVideo::Process(unsigned int time, CDirtyRegionList& dirty)
{
  MarkDirtyRegion();
  CGUIWindow::Process(time, dirty);
}
void CGUIWindowNXDKVideo::Render()
{
  auto& gfx = CServiceBroker::GetWinSystem()->GetGfxContext();
  gfx.SetRenderingResolution(gfx.GetResInfo(), false);
  gfx.SetViewWindow(0, 0, gfx.GetWidth(), gfx.GetHeight());
  CServiceBroker::GetAppComponents().GetComponent<CApplicationPlayer>()->Render(true, 255, true);
}
bool CGUIWindowNXDKVideo::OnAction(const CAction& action)
{
  auto player = CServiceBroker::GetAppComponents().GetComponent<CApplicationPlayer>();
  switch (action.GetID())
  {
    case ACTION_SHOW_OSD:
      CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_DIALOG_VIDEO_OSD);
      return true;
    case ACTION_PAUSE:
    case ACTION_PLAYER_PLAYPAUSE:
      player->Pause(); return true;
    case ACTION_STOP:
      g_application.StopPlaying(); return true;
    case ACTION_STEP_FORWARD:
    case ACTION_BIG_STEP_FORWARD:
    case ACTION_CHAPTER_OR_BIG_STEP_FORWARD:
      player->Seek(true, action.GetID() != ACTION_STEP_FORWARD, false); return true;
    case ACTION_STEP_BACK:
    case ACTION_BIG_STEP_BACK:
    case ACTION_CHAPTER_OR_BIG_STEP_BACK:
      player->Seek(false, action.GetID() != ACTION_STEP_BACK, false); return true;
    case ACTION_NAV_BACK:
    case ACTION_PREVIOUS_MENU:
    case ACTION_SHOW_GUI:
      CServiceBroker::GetGUI()->GetWindowManager().PreviousWindow(); return true;
  }
  return CGUIWindow::OnAction(action);
}
