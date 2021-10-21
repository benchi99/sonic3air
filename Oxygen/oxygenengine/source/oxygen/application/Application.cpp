/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2021 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/application/Application.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/application/GameLoader.h"
#include "oxygen/application/audio/AudioOutBase.h"
#include "oxygen/application/audio/AudioPlayer.h"
#include "oxygen/application/input/ControlsIn.h"
#include "oxygen/application/input/InputManager.h"
#include "oxygen/application/mainview/GameView.h"
#include "oxygen/application/menu/GameSetupScreen.h"
#include "oxygen/application/menu/OxygenMenu.h"
#include "oxygen/application/overlays/BackdropView.h"
#include "oxygen/application/overlays/CheatSheetOverlay.h"
#include "oxygen/application/overlays/DebugLogView.h"
#include "oxygen/application/overlays/DebugSidePanel.h"
#include "oxygen/application/overlays/MemoryHexView.h"
#include "oxygen/application/overlays/MemoryHexView.h"
#include "oxygen/application/overlays/ProfilingView.h"
#include "oxygen/application/overlays/SaveStateMenu.h"
#include "oxygen/application/overlays/TouchControlsOverlay.h"
#include "oxygen/application/video/VideoOut.h"
#include "oxygen/base/PlatformFunctions.h"
#include "oxygen/helper/Log.h"
#include "oxygen/helper/Profiling.h"
#include "oxygen/simulation/LogDisplay.h"
#include "oxygen/simulation/Simulation.h"


static const float MOUSE_HIDE_TIME = 1.0f;	// Seconds until mouse cursor gets hidden after last movement


Application::Application() :
	mGameLoader(new GameLoader()),
	mSimulation(new Simulation()),
	mSaveStateMenu(new SaveStateMenu())
{
	if (hasVirtualGamepad())
	{
		mTouchControlsOverlay = new TouchControlsOverlay();
	}

	// Register profiling region IDs
	Profiling::startup();
	Profiling::registerRegion(ProfilingRegion::SIMULATION,			 "Simulation",	Color(1.0f, 1.0f, 0.0f));
	Profiling::registerRegion(ProfilingRegion::SIMULATION_USER_CALL, "User Calls",	Color(0.7f, 0.7f, 0.0f));
	Profiling::registerRegion(ProfilingRegion::AUDIO,				 "Audio",		Color::RED);
	Profiling::registerRegion(ProfilingRegion::RENDERING,			 "Rendering",	Color::BLUE);
	Profiling::registerRegion(ProfilingRegion::FRAMESYNC,			 "Frame Sync",	Color(0.3f, 0.3f, 0.3f));
}

Application::~Application()
{
	delete mGameApp;
	delete mGameView;
	delete mGameLoader;
	delete mSaveStateMenu;
	delete mSimulation;
	delete mTouchControlsOverlay;
}

void Application::initialize()
{
	GuiBase::initialize();

	if (nullptr == mGameView)
	{
		LOG_INFO("Adding game view");
		mGameView = new GameView(*mSimulation);
		addChild(mGameView);
		mBackdropView = createChild<BackdropView>();
	}

	mWindowMode = (WindowMode)Configuration::instance().mWindowMode;

	if (EngineMain::getDelegate().useDeveloperFeatures())
	{
		LOG_INFO("Adding debug views");
		mDebugSidePanel = createChild<DebugSidePanel>();
		createChild<MemoryHexView>();
		createChild<DebugLogView>();
	}

	//mOxygenMenu = mGameView->createChild<OxygenMenu>();
	mProfilingView = createChild<ProfilingView>();
	mCheatSheetOverlay = createChild<CheatSheetOverlay>();

	if (nullptr != mTouchControlsOverlay && nullptr == mTouchControlsOverlay->getParent())
	{
		mTouchControlsOverlay->buildTouchControls();
		addChild(mTouchControlsOverlay);
	}

	// Font
	mLogDisplayFont.setSize(15.0f);
	mLogDisplayFont.setShadow(true);

	LOG_INFO("Application initialization complete");
}

void Application::deinitialize()
{
	LOG_INFO("");
	LOG_INFO("--- SHUTDOWN ---");

	// Remove all children, as they must not get deleted automatically (which would be the case if they stay added as children)
	while (!mChildren.empty())
	{
		removeChild(*mChildren.begin());
	}

	EngineMain::getDelegate().shutdownGame();

	// Stop all sounds and especially streaming of emulated sounds before simulation shutdown
	EngineMain::instance().getAudioOut().getAudioPlayer().clearPlayback();
	mSimulation->shutdown();

	// Update display index, in case the window was moved meanwhile
	updateWindowDisplayIndex();
}

void Application::sdlEvent(const SDL_Event& ev)
{
	GuiBase::sdlEvent(ev);

	//LOG_INFO("SDL event: type = " << ev.type);

	// Inform input manager as well
	if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP)		// TODO: Also add joystick events?
	{
		InputManager::instance().injectSDLInputEvent(ev);
	}

	// Handle events that FTX doesn't
	switch (ev.type)
	{
		// Removed shortly after adding that code in, as automatic pause on focus change is quite annoying at least during development
		//  and can lead to some issues. Also, nobody asked for this anyways on the desktop versions.
	#if 0
		case SDL_WINDOWEVENT:
		{
			if (ev.window.windowID == SDL_GetWindowID(&EngineMain::instance().getSDLWindow()))
			{
				switch (ev.window.event)
				{
					case SDL_WINDOWEVENT_FOCUS_LOST:
					{
						EngineMain::getDelegate().onApplicationLostFocus();
						break;
					}
				}
			}
			break;
		}
	#endif

		case SDL_APP_WILLENTERBACKGROUND:
		{
			EngineMain::getDelegate().onApplicationLostFocus();
			break;
		}

		case SDL_JOYDEVICEADDED:
		{
			if (SDL_GetTicks() > 5000)
			{
				LogDisplay::instance().setLogDisplay("New game controller found");
				InputManager::instance().rescanRealDevices();
			}
			break;
		}

		case SDL_JOYDEVICEREMOVED:
		{
			if (SDL_GetTicks() > 5000)
			{
				LogDisplay::instance().setLogDisplay("Game controller was disconnected");
				InputManager::instance().rescanRealDevices();
			}
			break;
		}
	}
}

void Application::keyboard(const rmx::KeyboardEvent& ev)
{
	// Debug only
	//LOG_INFO(*String(0, "Keyboard event: key=0x%08x, scancode=0x%04x", ev.key, ev.scancode));

	GuiBase::keyboard(ev);

	if (ev.state)
	{
		if (FTX::keyState(SDLK_LALT) || FTX::keyState(SDLK_RALT))
		{
			// Alt pressed
			if (!ev.repeat)
			{
				// No key repeat for these
				switch (ev.key)
				{
					case SDLK_RETURN:
					{
						if (FTX::keyState(SDLK_LSHIFT))
						{
							setUnscaledWindow();
						}
						else
						{
							toggleFullscreen();
						}
						break;
					}

					case 'p':
					{
						Configuration::instance().mPerformanceDisplay = (Configuration::instance().mPerformanceDisplay + 1) % 3;
						break;
					}

					case 'r':
					{
						// Not available for normal users, as this would crash the application if OpenGL is not supported
						if (EngineMain::getDelegate().useDeveloperFeatures())
						{
							const Configuration::RenderMethod oldRenderMethod = Configuration::instance().mRenderMethod;
							const Configuration::RenderMethod newRenderMethod = (oldRenderMethod == Configuration::RenderMethod::SOFTWARE) ? Configuration::RenderMethod::OPENGL_SOFT :
																				(oldRenderMethod == Configuration::RenderMethod::OPENGL_SOFT) ? Configuration::RenderMethod::OPENGL_FULL : Configuration::RenderMethod::SOFTWARE;
							LogDisplay::instance().setLogDisplay((newRenderMethod == Configuration::RenderMethod::SOFTWARE) ? "Switching to pure software renderer" :
																 (newRenderMethod == Configuration::RenderMethod::OPENGL_SOFT) ? "Switching to opengl-soft renderer" : "Switching to opengl-full renderer");
							EngineMain::instance().switchToRenderMethod(newRenderMethod);
						}
						break;
					}

					case SDLK_END:
					{
						if (FTX::keyState(SDLK_RSHIFT))
						{
							// Intentional crash by null pointer exception when pressing Alt + RShift + End
							int* ptr = reinterpret_cast<int*>(mRemoveChild);	// This is usually a null pointer at this point
							*ptr = 0;
						}
					}
				}
			}
		}
		else
		{
			// Alt not pressed
			if (!ev.repeat)
			{
				// No key repeat for these
				switch (ev.key)
				{
					case SDLK_F1:
					{
						if (FTX::keyState(SDLK_LSHIFT) && EngineMain::getDelegate().useDeveloperFeatures())
						{
							PlatformFunctions::openFileExternal(L"config.json");
						}
						else
						{
							mCheatSheetOverlay->toggle();
						}
						break;
					}

					case SDLK_F2:
					{
						if (Configuration::instance().mGameRecording == 1)
						{
							WString filename;
							const uint32 numFrames = mSimulation->saveGameRecording(&filename);
							LogDisplay::instance().setLogDisplay(String(0, "Saved recording of last %d seconds in '%s'", numFrames / 60, *filename.toString()));
						}
						break;
					}

					case SDLK_F3:
					{
						const InputManager::RescanResult result = InputManager::instance().rescanRealDevices();
						LogDisplay::instance().setLogDisplay(String(0, "Re-scanned connected game controllers: %d found", result.mGamepadsFound));
						break;
					}

					case SDLK_F4:
					{
						const bool switched = ControlsIn::instance().switchGamepads();
						LogDisplay::instance().setLogDisplay(switched ? "Switched gamepads (switched)" : "Switched gamepads (original)");
						break;
					}

					case SDLK_F5:
					{
						// Save state menu
						if (EngineMain::getDelegate().useDeveloperFeatures())
						{
							if (!mSaveStateMenu->isActive() && mSimulation->isRunning())
							{
								addChild(mSaveStateMenu);
								mSaveStateMenu->init(false);
								mSimulation->setSpeed(0.0f);
							}
						}
						break;
					}

					case SDLK_F8:
					{
						// This feature is hidden in non-developer environment -- you have to press right (!) shift as well
						if (EngineMain::getDelegate().useDeveloperFeatures() || FTX::keyState(SDLK_RSHIFT))
						{
							// Load state menu
							if (!mSaveStateMenu->isActive() && mSimulation->isRunning())
							{
								addChild(mSaveStateMenu);
								mSaveStateMenu->init(true);
								mSimulation->setSpeed(0.0f);
							}
						}
						break;
					}

					case SDLK_PRINTSCREEN:
					{
						// Saving a screenshot to disk is meant to be developer-only, as the "getScreenshot" call can crash the application for some users
						//  (Yes, I had this active for everyone in the early days of S3AIR)
						if (EngineMain::getDelegate().useDeveloperFeatures())
						{
							Bitmap bitmap;
							VideoOut::instance().getScreenshot(bitmap);
							bitmap.save(L"screenshot.bmp");
							LogDisplay::instance().setLogDisplay("Screenshot saved in 'screenshot.bmp'");
						}
						break;
					}
				}
			}

			// Key repeat is fine for these
			switch (ev.key)
			{
				case SDLK_KP_PLUS:
				case SDLK_KP_MINUS:
				{
					int volume = roundToInt(Configuration::instance().mAudioVolume * 100.0f);
					volume = clamp((ev.key == SDLK_KP_PLUS) ? volume + 5 : volume - 5, 0, 100);
					Configuration::instance().mAudioVolume = (float)volume / 100.0f;
					LogDisplay::instance().setLogDisplay(String(0, "Audio volume: %d%%", volume));
					break;
				}

				case SDLK_KP_DIVIDE:
				case SDLK_KP_MULTIPLY:
				{
					// Resolution changes are potentially game breaking, hence developer-only
					if (EngineMain::getDelegate().useDeveloperFeatures())
					{
						VideoOut& videoOut = VideoOut::instance();
						uint32 width = videoOut.getScreenWidth();
						uint32 height = videoOut.getScreenHeight();

						width += (ev.key == SDLK_KP_MULTIPLY) ? 16 : -16;
						width = clamp(width, 320, 496);
						height = 224;

						videoOut.setScreenSize(width, height);
						LogDisplay::instance().setLogDisplay("Changed render resolution to " + std::to_string(width) + " x " + std::to_string(height) + " pixels");
					}
					break;
				}
			}
		}
	}
}

void Application::update(float timeElapsed)
{
	if (mIsVeryFirstFrameForLogging)
	{
		LOG_INFO("Start of first application update call");
	}

	// Global slow motion for debugging menu transitions etc.
	const bool isDeveloperMode = EngineMain::getDelegate().useDeveloperFeatures();
	if (isDeveloperMode && FTX::keyState(SDLK_RSHIFT))
	{
		timeElapsed /= 10.0f;
	}

	// Update loading
	if (mGameLoader->isLoading())
	{
		// Work in progress here
	#if defined(DEBUG)
		if (nullptr == mGameSetupScreen)
		{
			mGameSetupScreen = mGameView->createChild<GameSetupScreen>();
		}
	#endif

		updateLoading();
	}
	else
	{
		if (nullptr != mGameSetupScreen)
		{
			mGameView->deleteChild(mGameSetupScreen);
			mGameSetupScreen = nullptr;
		}
	}

	// Update input
	InputManager::instance().updateInput(timeElapsed);

	// Update simulation
	Profiling::pushRegion(ProfilingRegion::SIMULATION);
	mSimulation->update(timeElapsed);
	Profiling::popRegion(ProfilingRegion::SIMULATION);

	// Update game
	EngineMain::getDelegate().updateGame(timeElapsed);

	// Update audio
	Profiling::pushRegion(ProfilingRegion::AUDIO);
	EngineMain::instance().getAudioOut().realtimeUpdate(timeElapsed);
	Profiling::popRegion(ProfilingRegion::AUDIO);

	if (isDeveloperMode)
	{
		// Update debugging stuff
		Profiling::pushRegion(ProfilingRegion::SIMULATION);
		mSimulation->refreshDebugging();
		Profiling::popRegion(ProfilingRegion::SIMULATION);
	}

	// GUI
	LogDisplay& logDisplay = LogDisplay::instance();
	logDisplay.mLogDisplayTimeout = std::max(logDisplay.mLogDisplayTimeout - std::min(timeElapsed, 0.1f), 0.0f);

	GuiBase::update(timeElapsed);

	if (nullptr != mRemoveChild)
	{
		removeChild(mRemoveChild);
		mRemoveChild = nullptr;
	}

	if (FTX::mouseRel() != Vec2i())
	{
		mMouseHideTimer = 0.0f;
		SDL_ShowCursor(1);
	}
	else if (mMouseHideTimer < MOUSE_HIDE_TIME)
	{
		mMouseHideTimer += timeElapsed;
		if (mMouseHideTimer >= MOUSE_HIDE_TIME)
			SDL_ShowCursor(0);
	}

	if (mIsVeryFirstFrameForLogging)
	{
		LOG_INFO("End of first application render call");
	}
}

void Application::render()
{
	Profiling::pushRegion(ProfilingRegion::RENDERING);

	if (mIsVeryFirstFrameForLogging)
	{
		LOG_INFO("Start of first application render call");
	}

	Drawer& drawer = EngineMain::instance().getDrawer();
	drawer.setupRenderWindow(&EngineMain::instance().getSDLWindow());

	GuiBase::render();

	// TODO: This gets called too late
	mBackdropView->setGameViewRect(mGameView->getGameViewport());

	// Show log display output
	{
		LogDisplay& logDisplay = LogDisplay::instance();

		if (!logDisplay.mModeDisplayString.empty())
		{
			const Recti rect(0, 0, FTX::screenWidth(), 26);
			drawer.drawRect(rect, Color(0.4f, 0.4f, 0.4f, 0.4f));
			drawer.printText(mLogDisplayFont, Recti(5, 5, 0, 0), logDisplay.mModeDisplayString);
		}

		if (logDisplay.mLogDisplayTimeout > 0.0f)
		{
			drawer.printText(mLogDisplayFont, Recti(5, FTX::screenHeight() - 25, 0, 0), logDisplay.mLogDisplayString, 1, Color(1.0f, 1.0f, 1.0f, saturate(logDisplay.mLogDisplayTimeout / 0.25f)));
		}

		if (!logDisplay.mLogErrorStrings.empty())
		{
			Recti rect(5, FTX::screenHeight() - 30 - (int)logDisplay.mLogErrorStrings.size() * 20, 0, 0);
			for (const String& error : logDisplay.mLogErrorStrings)
			{
				drawer.printText(mLogDisplayFont, rect, error, 1, Color(1.0f, 0.2f, 0.2f));
				rect.y += 20;
			}
		}
	}

	drawer.performRendering();

	// Needed only for precise profiling
	//glFinish();

	Profiling::popRegion(ProfilingRegion::RENDERING);

	// Update profiling data & explicit buffer swap
	{
		Profiling::pushRegion(ProfilingRegion::FRAMESYNC);

		const float tickLengthMilliseconds = 1000.0f / mSimulation->getSimulationFrequency();
		const bool usingFramecap = (drawer.getType() != Drawer::Type::OPENGL || Configuration::instance().mFrameSync != 1);
		int delay = 0;
		if (usingFramecap)
		{
			const uint32 currentTicks = SDL_GetTicks();
			delay = (int)mNextRefreshTicks - currentTicks;
			if (delay < 0 || delay > (int)std::ceil(tickLengthMilliseconds))
			{
				mNextRefreshTicks = (float)currentTicks + tickLengthMilliseconds;
			}
			else
			{
				mNextRefreshTicks += tickLengthMilliseconds;
			}
		}
		else
		{
			// This should not be necessary if VSync is *really* on
			delay = 3 - (int)((float)Profiling::getRootRegion().mTimer.GetCurrentSeconds() * 1000.0f);
		}
		if (delay > 0 && delay <= (int)std::ceil(tickLengthMilliseconds))
			SDL_Delay(delay);

		if (mIsVeryFirstFrameForLogging)
		{
			LOG_INFO("First present screen call");
		}

		drawer.presentScreen();

	#if 0
		// Use a glFinish or glFlush here...?
		//  -> PRO
		//     - glFinish (but not glFlush) helps to give a precise measurement of frame sync time on Windows
		//     - It also prevents stutters (at least one when the first sound effect gets played, for whatever reason that happens)
		//  -> CONTRA
		//     - I previously found that glFinish seems to produce double output of the same frame here and there, but only when combined with the glFinish call above
		//     - glFinish introduces performance issues on Android and potentially on weak machines in general (the more lightweight glFlush does not)
		//  -> Conclusion:
		//     - Better leave both out; glFinish is too expensive, and glFlush doesn't really have much of an effect
		glFinish();
	#endif

		Profiling::popRegion(ProfilingRegion::FRAMESYNC);
		Profiling::nextFrame(mSimulation->getFrameNumber());
	}

	if (mIsVeryFirstFrameForLogging)
	{
		LOG_INFO("End of first application render call");
		LOG_INFO("Ready to go");
		mIsVeryFirstFrameForLogging = false;
	}
}

void Application::childClosed(GuiBase& child)
{
	if (mSimulation->isRunning())
	{
		mSimulation->setSpeed(mSimulation->getDefaultSpeed());
	}
	mRemoveChild = &child;
}

/*
bool Application::isFullscreen() const
{
	return (SDL_GetWindowFlags(FTX::Video->getMainWindow()) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}
*/

void Application::setWindowMode(WindowMode windowMode, bool force)
{
	if (mWindowMode == windowMode && !force)
		return;

	SDL_Window* window = FTX::Video->getMainWindow();
	const int displayIndex = updateWindowDisplayIndex();

	switch (windowMode)
	{
		default:
		case WindowMode::WINDOWED:
		{
			if (mWindowMode == WindowMode::EXCLUSIVE_FULLSCREEN)
			{
				SDL_SetWindowFullscreen(window, 0);
			}
			SDL_SetWindowSize(window, Configuration::instance().mWindowSize.x, Configuration::instance().mWindowSize.y);
			SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex));
			SDL_SetWindowResizable(window, SDL_TRUE);
			SDL_SetWindowBordered(window, SDL_TRUE);
			break;
		}

		case WindowMode::BORDERLESS_FULLSCREEN:
		{
			if (mWindowMode == WindowMode::EXCLUSIVE_FULLSCREEN)
			{
				// Exit exclusive fullscreen first
				SDL_SetWindowFullscreen(window, 0);
			}

			SDL_Rect rect;
			if (SDL_GetDisplayBounds(displayIndex, &rect) == 0)
			{
				SDL_SetWindowSize(window, rect.w, rect.h);
				SDL_SetWindowPosition(window, rect.x, rect.y);
				SDL_SetWindowResizable(window, SDL_FALSE);
				SDL_SetWindowBordered(window, SDL_FALSE);
			}
			else
			{
				SDL_DisplayMode dm;
				if (SDL_GetDesktopDisplayMode(displayIndex, &dm) == 0)
				{
					SDL_SetWindowSize(window, dm.w, dm.h);
					SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
					SDL_SetWindowResizable(window, SDL_FALSE);
					SDL_SetWindowBordered(window, SDL_FALSE);
				}
			}
			break;
		}

		case WindowMode::EXCLUSIVE_FULLSCREEN:
		{
			SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
			break;
		}
	}

	int width, height;
	SDL_GetWindowSize(window, &width, &height);
	FTX::Video->reshape(width, height);

	SDL_ShowCursor(windowMode == WindowMode::WINDOWED);

	mWindowMode = windowMode;
}

void Application::toggleFullscreen()
{
	if (getWindowMode() == WindowMode::WINDOWED)
	{
	#if defined(PLATFORM_LINUX)
		// Under Linux, the exclusive fullscreen works better, so that's the default
		setWindowMode(WindowMode::EXCLUSIVE_FULLSCREEN);
	#else
		setWindowMode(WindowMode::BORDERLESS_FULLSCREEN);
	#endif
	}
	else
	{
		setWindowMode(WindowMode::WINDOWED);
	}
}

bool Application::hasKeyboard() const
{
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_MAC) || defined(PLATFORM_LINUX)
	// It should be safe to assume that desktop platforms always have a keyboard
	return true;
#else
	// For other platforms, ask the input manager, as it tracks whether any key was ever pressed
	return InputManager::instance().hasKeyboard();
#endif
}

bool Application::hasVirtualGamepad() const
{
	return (EngineMain::instance().getPlatformFlags() & 0x0002) != 0;
}

int Application::updateWindowDisplayIndex()
{
	const int displayIndex = SDL_GetWindowDisplayIndex(FTX::Video->getMainWindow());
	if (displayIndex != -1)
	{
		Configuration::instance().mDisplayIndex = displayIndex;
		return displayIndex;
	}
	return std::max(0, Configuration::instance().mDisplayIndex);
}

void Application::setUnscaledWindow()
{
	// Cycle through the different scaling factors
	Vec2i desktopSize;
	{
		const int displayIndex = updateWindowDisplayIndex();
		SDL_Rect rect;
		if (SDL_GetDisplayBounds(displayIndex, &rect) == 0)
		{
			desktopSize.set(rect.w, rect.h);
		}
		else
		{
			SDL_DisplayMode dm;
			if (SDL_GetDesktopDisplayMode(displayIndex, &dm) == 0)
			{
				desktopSize.set(dm.w, dm.h);
			}
		}
	}

	const Vec2i gameScreenSize = VideoOut::instance().getScreenSize();
	int currentScale = 0;
	{
		const int maxScale = std::min(desktopSize.x / gameScreenSize.x, desktopSize.y / gameScreenSize.y);
		if (getWindowMode() == WindowMode::WINDOWED)
		{
			for (int scale = 1; scale < maxScale; ++scale)
			{
				if (Configuration::instance().mWindowSize == gameScreenSize * scale)
				{
					currentScale = scale;
					break;
				}
			}
		}
	}

	const int newScale = currentScale + 1;
	Configuration::instance().mWindowSize = gameScreenSize * newScale;
	setWindowMode(WindowMode::WINDOWED, true);
}

bool Application::updateLoading()
{
	while (true)
	{
		const GameLoader::UpdateResult updateResult = mGameLoader->updateLoading();
		switch (updateResult)
		{
			case GameLoader::UpdateResult::SUCCESS:
			{
				// The simulation startup may fail, and this should lead to the application not starting at all
				LOG_INFO("Simulation startup");
				if (!mSimulation->startup())
				{
					LOG_INFO("Simulation startup failed");

					// TODO: Handle this better
					FTX::System->quit();
					return false;
				}

				// Startup game
				EngineMain::getDelegate().startupGame();

				LOG_INFO("Adding game app instance");
				mGameApp = &EngineMain::getDelegate().createGameApp();
				addChild(mGameApp);
				break;
			}

			case GameLoader::UpdateResult::FAILURE:
			{
				// TODO: Handle this better
				FTX::System->quit();
				return false;
			}

			default:
				break;
		}

		// Return if no immediate update is requested
		if (updateResult != GameLoader::UpdateResult::CONTINUE_IMMEDIATE)
			break;
	}
	return true;
}
