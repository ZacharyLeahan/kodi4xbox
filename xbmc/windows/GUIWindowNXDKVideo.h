/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "guilib/GUIWindow.h"

class CGUIWindowNXDKVideo final : public CGUIWindow
{
public:
  CGUIWindowNXDKVideo();
  void OnInitWindow() override;
  void OnDeinitWindow(int nextWindowID) override;
  void Process(unsigned int time, CDirtyRegionList& dirty) override;
  void Render() override;
  bool OnAction(const CAction& action) override;
};
