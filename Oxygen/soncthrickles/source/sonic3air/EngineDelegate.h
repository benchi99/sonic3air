/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2021 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/application/EngineMain.h"
#include "sonic3air/ConfigurationImpl.h"
#include "sonic3air/Game.h"


class EngineDelegate : public EngineDelegateInterface
{
public:
	const AppMetaData& getAppMetaData() override;
	GuiBase& createGameApp() override;
	AudioOutBase& createAudioOut() override;

	bool onEnginePreStartup() override;
	bool setupCustomGameProfile() override;

	void startupGame() override;
	void shutdownGame() override;
	void updateGame(float timeElapsed) override;

	void registerScriptBindings(lemon::Module& module) override;
	void registerNativizedCode(lemon::Program& program) override;
	void onRuntimeInit(CodeExec& codeExec) override;
	void onPreFrameUpdate() override;
	void onPostFrameUpdate() override;
	void onControlsUpdate() override;
	void onPreSaveStateLoad() override;

	void onApplicationLostFocus() override;

	bool mayLoadScriptMods() override;
	bool allowModdedData() override;
	bool useDeveloperFeatures() override;

	void onGameRecordingHeaderLoaded(const std::string& buildString, const std::vector<uint8>& buffer) override;
	void onGameRecordingHeaderSave(std::vector<uint8>& buffer) override;

	Font& getDebugFont(int size) override;
	void fillDebugVisualization(Bitmap& bitmap, int& mode) override;

private:
	AppMetaData mAppMetaData;
	ConfigurationImpl mConfiguration;
	Game mGame;
};
