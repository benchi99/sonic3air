﻿/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2024 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/devmode/windows/DevModeMainWindow.h"

#if defined(SUPPORT_IMGUI)

#include "oxygen/devmode/ImGuiHelpers.h"
#include "oxygen/devmode/windows/GameSimWindow.h"
#include "oxygen/devmode/windows/MemoryHexViewWindow.h"
#include "oxygen/devmode/windows/PaletteViewWindow.h"
#include "oxygen/devmode/windows/ScriptBuildWindow.h"
#include "oxygen/devmode/windows/SettingsWindow.h"
#include "oxygen/devmode/windows/SpriteBrowserWindow.h"
#include "oxygen/devmode/windows/WatchesWindow.h"


DevModeMainWindow::DevModeMainWindow() :
	DevModeWindowBase("Dev Mode (F1)")
{
	mIsWindowOpen = true;

	createWindow(mGameSimWindow);
	createWindow(mMemoryHexViewWindow);
	createWindow(mWatchesWindow);
	createWindow(mPaletteViewWindow);
	createWindow(mSpriteBrowserWindow);
	createWindow(mScriptBuildWindow);
	createWindow(mSettingsWindow);
}

DevModeMainWindow::~DevModeMainWindow()
{
	for (DevModeWindowBase* window : mAllWindows)
		delete window;
}

bool DevModeMainWindow::buildWindow()
{
	const bool result = DevModeWindowBase::buildWindow();
	for (DevModeWindowBase* window : mAllWindows)
	{
		window->buildWindow();
	}

	// ImGui demo window for testing
	if (mShowImGuiDemo)
		ImGui::ShowDemoWindow();

	return result;
}

void DevModeMainWindow::buildContent()
{
	ImGui::SetWindowPos(ImVec2(5.0f, 5.0f), ImGuiCond_FirstUseEver);
	ImGui::SetWindowSize(ImVec2(150.0f, 200.0f), ImGuiCond_FirstUseEver);
	ImGui::SetWindowCollapsed(true, ImGuiCond_FirstUseEver);

	for (DevModeWindowBase* window : mAllWindows)
	{
		ImGui::Checkbox(window->mTitle.c_str(), &window->mIsWindowOpen);
	}

#ifdef DEBUG
	ImGui::Spacing();
	ImGui::Checkbox("ImGui Demo", &mShowImGuiDemo);

	// Just for debugging
	//ImGui::Text("ImGui Capture:   %s %s", ImGui::GetIO().WantCaptureMouse ? "[M]" : "      ", ImGui::GetIO().WantCaptureKeyboard ? "[K]" : "");
#endif
}

void DevModeMainWindow::openWatchesWindow()
{
	mWatchesWindow->mIsWindowOpen = true;
}

#endif
