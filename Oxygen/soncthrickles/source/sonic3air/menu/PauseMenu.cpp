/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2021 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"
#include "sonic3air/menu/PauseMenu.h"
#include "sonic3air/menu/GameApp.h"
#include "sonic3air/menu/SharedResources.h"
#include "sonic3air/Game.h"
#include "sonic3air/audio/AudioOut.h"

#include "oxygen/application/Application.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/application/input/ControlsIn.h"
#include "oxygen/application/input/InputManager.h"
#include "oxygen/helper/FileHelper.h"
#include "oxygen/simulation/Simulation.h"


PauseMenu::PauseMenu()
{
}

PauseMenu::~PauseMenu()
{
}

GameMenuBase::BaseState PauseMenu::getBaseState() const
{
	switch (mState)
	{
		case State::APPEAR:			  return BaseState::FADE_IN;
		case State::SHOW:			  return BaseState::SHOW;
		case State::DIALOG_RESTART:	  return BaseState::SHOW;
		case State::DIALOG_EXIT:	  return BaseState::SHOW;
		case State::DISAPPEAR_RESUME: return BaseState::FADE_OUT;
		case State::DISAPPEAR_EXIT:	  return BaseState::FADE_OUT;
		default:					  return BaseState::INACTIVE;
	}
}

void PauseMenu::onFadeIn()
{
	mState = State::APPEAR;
	mVisibility = 0.0f;
	mDialogVisibility = 0.0f;
	mTimeShown = 0.0f;

	// Really pause game simulation
	Application::instance().getSimulation().setSpeed(0.0f);

	// Build up menu structure
	{
		mMenuEntries.clear();
		mMenuEntries.reserve(3);
		mMenuEntries.addEntry("Continue", 0);
		if (mRestartEnabled)
			mMenuEntries.addEntry("Restart", 1);
		//mMenuEntries.addEntry("Options", 2);	// Not ready yet
		mMenuEntries.addEntry("Exit Game", 3);
	}
	mMenuEntries.mSelectedEntryIndex = 0;
	mDialogEntries.reserve(3);
}

bool PauseMenu::canBeRemoved()
{
	return (mState == State::INACTIVE && mVisibility <= 0.0f);
}

void PauseMenu::initialize()
{
}

void PauseMenu::deinitialize()
{
}

void PauseMenu::keyboard(const rmx::KeyboardEvent& ev)
{
}

void PauseMenu::update(float timeElapsed)
{
	if (!isEnabled())
		return;

	GameMenuBase::update(timeElapsed);

	if (mState == State::SHOW)
	{
		const InputManager::ControllerScheme& keys = InputManager::instance().getController(0);
		if (mScreenshotMode)
		{
			if (InputManager::instance().anythingPressed())
			{
				mScreenshotMode = false;
			}
		}
		else
		{
			// Update menu entries
			const GameMenuEntries::UpdateResult result = mMenuEntries.update();
			if (result != GameMenuEntries::UpdateResult::NONE)
			{
				playMenuSound(0x5b);
			}

			if (keys.Start.justPressed() || keys.A.justPressed() || keys.X.justPressed())
			{
				switch (mMenuEntries.selected().mData)
				{
					case 0:
					{
						// Continue game
						resumeGame();
						break;
					}

					case 1:
					{
						// Restart
						if (Game::instance().isTimeAttackMode())
						{
							resumeGame();
							Game::instance().restartTimeAttack(false);
						}
						else
						{
							mState = State::DIALOG_RESTART;
							mDialogVisibility = 0.0f;

							mDialogEntries.clear();
							mDialogEntries.addEntry("Continue", 0);
							mDialogEntries.addEntry("Last checkpoint", 0x10);
							mDialogEntries.addEntry("Restart act", 0x11);
							mDialogEntries.mSelectedEntryIndex = 0;
						}
						break;
					}

					case 2:
					{
						// Open options menu
						GameApp::instance().openOptionsMenu(true);
						break;
					}

					case 3:
					{
						// Exit game (without confirmation dialog during development, as I found that a bit annoying)
					#ifdef ENDUSER
						mState = State::DIALOG_EXIT;
						mDialogVisibility = 0.0f;

						mDialogEntries.clear();
						mDialogEntries.addEntry("Continue", 0);
						mDialogEntries.addEntry("Exit to Menu", 0x20);
						mDialogEntries.mSelectedEntryIndex = 0;
					#else
						exitGame();
					#endif
						break;
					}
				}
			}
			else if (keys.Y.justPressed())
			{
				mScreenshotMode = true;
				InputManager::instance().setTouchInputMode(InputManager::TouchInputMode::FULLSCREEN_START);
			}
		}

		mTimeShown += timeElapsed;
	}
	else if (mState >= State::DIALOG_RESTART && mState <= State::DIALOG_EXIT)
	{
		// Update dialog entries
		const GameMenuEntries::UpdateResult result = mDialogEntries.update();
		if (result != GameMenuEntries::UpdateResult::NONE)
		{
			playMenuSound(0x5b);
		}

		const InputManager::ControllerScheme& keys = InputManager::instance().getController(0);
		if (keys.Start.justPressed() || keys.A.justPressed() || keys.X.justPressed())
		{
			switch (mDialogEntries.selected().mData)
			{
				case 0:
				{
					// Cancel dialog
					mState = State::SHOW;
					break;
				}
				case 0x10:
				{
					// Restart at last checkpoint
					resumeGame();
					Game::instance().restartAtCheckpoint();
					break;
				}
				case 0x11:
				{
					// Restart whole level
					resumeGame();
					Game::instance().restartLevel();
					break;
				}
				case 0x20:
				{
					// Exit game
					exitGame();
					break;
				}
			}
		}
		else if (keys.B.justPressed())
		{
			// Cancel dialog
			mState = State::SHOW;
		}
	}

	if (mState == State::APPEAR)
	{
		mVisibility = saturate(mVisibility + timeElapsed * 12.0f);
		if (mVisibility >= 1.0f)
		{
			mState = State::SHOW;
		}
	}
	else if (mState >= State::DISAPPEAR_RESUME)
	{
		mVisibility = saturate(mVisibility - timeElapsed * ((mState == State::DISAPPEAR_RESUME) ? 12.0f : 8.0f));
		if (mVisibility <= 0.0f)
		{
			switch (mState)
			{
				case State::DISAPPEAR_RESUME:
					// Nothing special happens here
					break;

				case State::DISAPPEAR_EXIT:
					GameApp::instance().returnToMenu();
					break;

				default:
					break;
			}
			mState = State::INACTIVE;
		}
	}

	if (mState == State::DIALOG_RESTART || mState == State::DIALOG_EXIT)
	{
		mDialogVisibility = saturate(mDialogVisibility + timeElapsed * 12.0f);
	}
	else
	{
		mDialogVisibility = saturate(mDialogVisibility - timeElapsed * 12.0f);
	}
}

void PauseMenu::render()
{
	Drawer& drawer = EngineMain::instance().getDrawer();

	if (!mScreenshotMode)
	{
		const int screenWidth = (int)mRect.width;
		const int screenHeight = (int)mRect.height;

		// Dialog box
		if (mDialogVisibility > 0.0f)
		{
			const constexpr int LINE_HEIGHT = 15;

			int px = screenWidth - 194 + roundToInt((1.0f - mDialogVisibility) * 80.0f) - (int)mMenuEntries.size() * 9;
			int py = screenHeight - 1 - (int)mDialogEntries.size() * LINE_HEIGHT;

			if (mDialogEntries.size() <= 2)
			{
				const Recti rect = Recti(px - 66, py - 8, global::mPauseScreenDialog2BG.getWidth(), global::mPauseScreenDialog2BG.getHeight());
				drawer.drawRect(rect, global::mPauseScreenDialog2BG, Color(1.0f, 1.0f, 1.0f, mDialogVisibility));
			}
			else
			{
				py -= 15;
				const Recti rect = Recti(px - 68, py - 8, global::mPauseScreenDialog3BG.getWidth(), global::mPauseScreenDialog3BG.getHeight());
				drawer.drawRect(rect, global::mPauseScreenDialog3BG, Color(1.0f, 1.0f, 1.0f, mDialogVisibility));
				px += 5;
			}

			for (size_t line = 0; line < mDialogEntries.size(); ++line)
			{
				const auto& entry = mDialogEntries[line];
				Color color = Color(0.7f, 0.8f, 0.9f, 0.7f);
				if ((int)line == mDialogEntries.mSelectedEntryIndex)
					color = (std::fmod(FTX::getTime() * 2.0f, 1.0f) < 0.5f) ? Color::YELLOW : Color::WHITE;
				color.a = mDialogVisibility;

				drawer.printText(global::mFont7, Recti(px, py, 0, 0), entry.mText, 2, color);
				py += LINE_HEIGHT;
				px -= LINE_HEIGHT / 3;
			}
		}

		// Actual pause menu (upper & lower part)
		{
			const constexpr int LINE_HEIGHT = 28;
			const int rightAnchor = screenWidth + roundToInt((1.0f - mVisibility) * 160.0f);

			Recti rect = Recti(rightAnchor - global::mPauseScreenUpperBG.getWidth(), 0, global::mPauseScreenUpperBG.getWidth(), global::mPauseScreenUpperBG.getHeight());
			drawer.drawRect(rect, global::mPauseScreenUpperBG);

			int py = screenHeight - (int)mMenuEntries.size() * LINE_HEIGHT;
			rect = Recti(rightAnchor - 190, py - 8, global::mPauseScreenLowerBG.getWidth(), global::mPauseScreenLowerBG.getHeight());
			drawer.drawRect(rect, global::mPauseScreenLowerBG);

			for (size_t line = 0; line < mMenuEntries.size(); ++line)
			{
				const auto& entry = mMenuEntries[line];
				const bool isSelected = ((int)line == mMenuEntries.mSelectedEntryIndex);
				Color color = Color(0.7f, 0.8f, 1.0f, 0.8f);
				if (mState == State::SHOW)
				{
					if (isSelected)
						color = (std::fmod(FTX::getTime() * 2.0f, 1.0f) < 0.5f) ? Color::YELLOW : Color::WHITE;
				}
				else
				{
					color = isSelected ? Color(0.9f, 0.9f, 0.9f, 0.8f) : Color(0.7f, 0.7f, 0.7f, 0.5f);
				}

				drawer.printText(global::mFont18, Recti(rightAnchor - 16, py + 2, 0, 20), entry.mText, 6, color);
				py += LINE_HEIGHT;
			}
		}

		if (mTimeShown > 5.0f)
		{
			const float visibility = saturate((mTimeShown - 5.0f) * 3.0f) * mVisibility;
			drawer.printText(global::mFont4, Recti(8, screenHeight + roundToInt(interpolate(20, -2, visibility)), 0, 0), "[W] / (Y): Hide menu for clean screenshots", 7, Color(0.6f, 0.8f, 1.0f, 0.8f));
		}

		drawer.performRendering();
	}
}

void PauseMenu::onReturnFromOptions()
{
	mState = State::APPEAR;
}

void PauseMenu::resumeGame()
{
	mState = State::DISAPPEAR_RESUME;
	Application::instance().getSimulation().setSpeed(Application::instance().getSimulation().getDefaultSpeed());
	ControlsIn::instance().setIgnores(0x0ff3);		// Ignore most key presses, except for left/right
	AudioOut::instance().resumeSoundContext(AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_MUSIC);
	AudioOut::instance().resumeSoundContext(AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_SOUND);
	GameApp::instance().onGameResumed();
}

void PauseMenu::exitGame()
{
	mState = State::DISAPPEAR_EXIT;
}
