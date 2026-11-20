/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

/*
 * Main.cpp
 *
 * Main program driver for the SDL port.
 *
 * Bugs and Issues to Address
 * --------------------------
 * - -gfx-state crashes when ENABLE_DEBUGGER is also enabled:
 *
 *    #0  0x00007ff6586392a0 in CSoundBoard::GetDSB (this=0x1128) at Src/Model3/SoundBoard.cpp:552
 *    #1  0x00007ff6587072cf in Debugger::CSupermodelDebugger::CreateDSBCPUDebug (model3=0x0) at Src/Debugger/SupermodelDebugger.cpp:269
 *    #2  0x00007ff6587076e9 in Debugger::CSupermodelDebugger::AddCPUs (this=0x185da75df40) at Src/Debugger/SupermodelDebugger.cpp:361
 *    #3  0x00007ff6586f91a6 in Debugger::CDebugger::Attach (this=0x185da75df40) at Src/Debugger/Debugger.cpp:263
 *    #4  0x00007ff658705312 in Debugger::CConsoleDebugger::Attach (this=0x185da75df40) at Src/Debugger/ConsoleDebugger.cpp:2707
 *    #5  0x00007ff65862c6fc in Supermodel (game=..., rom_set=0x6cc8dff0e0, Model3=0x185da7598d0, Inputs=0x185da752590, Outputs=0x0, Debugger=std::shared_ptr<Debugger::CDebugger> (use count 3, weak count 0) = {...})
 *        at Src/OSD/SDL/Main.cpp:978
 *    #6  0x00007ff658634ba3 in SDL_main (argc=3, argv=0x185da4e68a0) at Src/OSD/SDL/Main.cpp:2056
 *    #7  0x00007ff658720141 in main_getcmdline ()
 *    #8  0x00007ff6585b13b1 in __tmainCRTStartup () at C:/M/mingw-w64-crt-git/src/mingw-w64/mingw-w64-crt/crt/crtexe.c:321
 *    #9  0x00007ff6585b14e6 in mainCRTStartup () at C:/M/mingw-w64-crt-git/src/mingw-w64/mingw-w64-crt/crt/crtexe.c:202
 * - Need to address all the stale TO-DOs below and clear them out :)
 *
 * To Do Before Next Release
 * -------------------------
 * - Thoroughly test config system (do overrides work as expected? XInput
 *   force settings?)
 * - Remove all occurrences of "using namespace std" from Nik's code.
 * - Standardize variable naming (recently introduced vars_like_this should be
 *   converted back to varsLikeThis).
 * - Update save state file revision (strings > 1024 chars are now supported).
 * - Fix BlockFile.cpp to use fstream!
 * - Check to make sure save states use explicitly-sized types for 32/64-bit
 *   compatibility (i.e., size_t, int, etc. not allowed).
 * - Make sure quitting while paused works.
 * - Add UI keys for balance setting?
 * - 5.1 audio support?
 *
 * Compile-Time Options
 * --------------------
 * - SUPERMODEL_WIN32: Define this if compiling on Windows.
 * - SUPERMODEL_OSX: Define this if compiling on Mac OS X.
 * - SUPERMODEL_DEBUGGER: Enable the debugger.
 * - DEBUG: Debug mode (use with caution, produces large logs of game behavior)
 */

#include <new>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <memory>
#include <vector>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <array>
#include <limits>
#include <GL/glew.h>

#ifdef SUPERMODEL_WIN32
#include "DirectInputSystem.h"
#include "WinOutputs.h"
#endif
#include "NetOutputs.h"

#include "Supermodel.h"
#include "Util/Format.h"
#include "Util/DiagnosticFile.h"
#include "Util/NewConfig.h"
#include "Util/ConfigBuilders.h"
#include "OSD/FileSystemPath.h"
#include "GameLoader.h"
#include "SDLInputSystem.h"
#include "SDLIncludes.h"
#include "FrameProfile.h"
#include "DesktopPresentationClock.h"
#include "DesktopPresentLoadGuard.h"
#include "MouseLook.h"
#include "CinematicToggle.h"
#include "TheaterScreenControls.h"
#include "HeadsetFrameClock.h"
#include "HeadsetIdleWait.h"
#include "HeadsetRenderTest.h"
#include "BodyInteractionMode.h"
#include "BodyRenderTest.h"
#include "OSD/Windows/OpenXRSession.h"
#include "HeadsetPresentationPump.h"
#include "HeadsetWorkerTest.h"
#include "HeadsetActiveTimings.h"
#include "HeadsetCheckpointBuffer.h"
#include "Debugger/SupermodelDebugger.h"
#ifndef SUPERMODEL_OSX
#include "Graphics/Legacy3D/Legacy3D.h"
#endif
#include "Graphics/New3D/New3D.h"
#include "Model3/IEmulator.h"
#include "Model3/Model3.h"
#include "OSD/Audio.h"
#include "Graphics/New3D/VBO.h"
#include "Graphics/SuperAA.h"
#include "Sound/MPEG/MpegAudio.h"
#include "HarleyMenuIntegration.h"
#include "HarleyCabinetInput.h"
#include "HarleyCabinetFlow.h"

#include <iostream>
#include "Util/BMPFile.h"

#include "Crosshair.h"
#include "OSD/ConfigFile.h"
#include "Gui.h"


/******************************************************************************
 Global Run-time Config
******************************************************************************/

static const std::string s_analysisPath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Analysis);
static const std::string s_configFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Config) << "Supermodel.ini";
static const std::string s_gameXMLFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Config) << "Games.xml";
static const std::string s_musicXMLFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Config) << "Music.xml";
static const std::string s_logFilePath = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Log) << "Supermodel.log";

static Util::Config::Node s_runtime_config("Global");
static std::unique_ptr<HarleyMenuIntegration> s_userMenu;

// Bounded diagnostic runs are single-threaded. A frame number refers to one
// completed RunFrame() after reset/load; paused redraws never advance it.
static uint64_t s_emulatedFrame = 0;
static bool s_capturePending = false;
static bool s_captureCompleted = false;
static bool s_captureFailed = false;
static std::string s_diagnosticGame;
static CpuFrameProfile s_cpuProfile;
static std::unique_ptr<MouseLookState> s_mouseLook;
static bool s_headsetOwnsVideo = false;

struct DiagnosticInputEvent
{
  unsigned frame;
  std::string control;
  uint16_t value;
};

static std::vector<DiagnosticInputEvent> s_inputScriptEvents;
static size_t s_inputScriptNextEvent = 0;
static std::map<std::string, uint16_t> s_inputScriptHeld;
static const std::map<std::string, unsigned> s_inputScriptMaximum {
  { "Coin1", 1 }, { "Start1", 1 }, { "ViewChange", 1 }, { "TestA", 1 }, { "ServiceA", 1 },
  { "Accelerator", 255 }, { "Steering", 255 }, { "Brake", 255 },
  { "RearBrake", 255 }, { "GearShiftUp", 1 }, { "GearShiftDown", 1 }, { "MusicSelect", 1 }
};

static void ApplyDiagnosticInputs(CInputs *inputs, const Game &game, unsigned frame)
{
  if (s_runtime_config["InputScript"].ValueAs<std::string>().empty())
    return;

  // Only game inputs are overridden. Polling and all emulator UI controls stay
  // live. Remember the last emulated values, even if paused redraws polled the
  // physical controls in between, so Pressed()/Released() retain their meaning.
  for (unsigned i = 0; i < inputs->Count(); i++)
  {
    auto input = (*inputs)[i];
    if (input->IsUIInput() || !(input->gameFlags & game.inputs))
      continue;
    const uint16_t neutral = (input->flags & INPUT_FLAGS_AXIS) ? 128 : 0;
    auto held = s_inputScriptHeld.find(input->id);
    input->prevValue = held != s_inputScriptHeld.end() ? held->second : neutral;
    input->value = neutral;
  }
  while (s_inputScriptNextEvent < s_inputScriptEvents.size() && s_inputScriptEvents[s_inputScriptNextEvent].frame == frame)
  {
    const auto &event = s_inputScriptEvents[s_inputScriptNextEvent++];
    s_inputScriptHeld[event.control] = event.value;
    InfoLog("Diagnostic input: emulated_frame=%u %s=%u", frame, event.control.c_str(), unsigned(event.value));
  }
  for (const auto &held : s_inputScriptHeld)
    (*inputs)[held.first.c_str()]->value = held.second;
}


/******************************************************************************
 Display Management
******************************************************************************/

SDL_Window *s_window = nullptr;

/*
 * Position and size of rectangular region within OpenGL display to render to.
 * Unlike the config tree, these end up containing the actual resolution (and
 * computed offsets within the viewport) that will be rendered based on what
 * was obtained from SDL.
 */
static unsigned  xOffset, yOffset;      // offset of renderer output within OpenGL viewport
static unsigned  xRes, yRes;            // renderer output resolution (can be smaller than GL viewport)
static unsigned  totalXRes, totalYRes;  // total resolution (the whole GL viewport)
static int aaValue = 1;                 // default is 1 which is no aa
static CRTcolor CRTcolors = CRTcolor::None; // default to no gamma/color adaption being done

/*
 * Crosshair stuff
 */
static CCrosshair* s_crosshair = nullptr;

static Result SetGLGeometry(unsigned *xOffsetPtr, unsigned *yOffsetPtr, unsigned *xResPtr, unsigned *yResPtr, unsigned *totalXResPtr, unsigned *totalYResPtr, bool keepAspectRatio)
{
  // What resolution did we actually get?
  int actualWidth;
  int actualHeight;
  SDL_GetWindowSize(s_window, &actualWidth, &actualHeight);
  *totalXResPtr = actualWidth;
  *totalYResPtr = actualHeight;

  // If required, fix the aspect ratio of the resolution that the user passed to match Model 3 ratio
  float xResF = float(*xResPtr);
  float yResF = float(*yResPtr);
  if (keepAspectRatio)
  {
    float model3Ratio = float(496.0/384.0);
    if (yResF < (xResF/model3Ratio))
      xResF = yResF*model3Ratio;
    if (xResF < (yResF*model3Ratio))
      yResF = xResF/model3Ratio;
  }

  // Center the visible area
  *xOffsetPtr = (*xResPtr - (unsigned) xResF)/2;
  *yOffsetPtr = (*yResPtr - (unsigned) yResF)/2;

  // If the desired resolution is smaller than what we got, re-center again
  if (int(*xResPtr) < actualWidth)
    *xOffsetPtr += (actualWidth - *xResPtr)/2;
  if (int(*yResPtr) < actualHeight)
    *yOffsetPtr += (actualHeight - *yResPtr)/2;

  // OpenGL initialization
  glViewport(0,0,*xResPtr,*yResPtr);
  glClearColor(0.0,0.0,0.0,0.0);
  glClearDepth(1.0);
  glDepthFunc(GL_LESS);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  // Clear both buffers to ensure a black border
  for (int i = 0; i < 3; i++)
  {
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    SDL_GL_SwapWindow(s_window);
  }

  // Write back resolution parameters
  *xResPtr = (unsigned) xResF;
  *yResPtr = (unsigned) yResF;

  UINT32 correction = (UINT32)(((*yResPtr / 384.) * 2.) + 0.5); // due to the 2D layer compensation (2 pixels off)

  glEnable(GL_SCISSOR_TEST);

  // Scissor box (to clip visible area)
  if (s_runtime_config["WideScreen"].ValueAsDefault<bool>(false))
  {
    glScissor(0* aaValue, correction* aaValue, *totalXResPtr * aaValue, (*totalYResPtr - (correction * 2)) * aaValue);
  }
  else
  {
    glScissor((*xOffsetPtr + correction) * aaValue, (*yOffsetPtr + correction) * aaValue, (*xResPtr - (correction * 2)) * aaValue, (*yResPtr - (correction * 2)) * aaValue);
  }
  return Result::OKAY;
}

// static void GLAPIENTRY DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
// {
//     printf("OGLDebug:: 0x%X: %s\n", id, message);
// }

// In windows with an nvidia card (sorry not tested anything else) you can customise the resolution.
// This also allows you to set a totally custom refresh rate. Apparently you can drive most monitors at
// 57.5fps with no issues. Anyway this code will automatically pick up your custom refresh rate, and set it if it exists.
// If it doesn't exist, then it'll probably just default to 60 or whatever your refresh rate is.
static void SetFullScreenRefreshRate()
{
    if (s_runtime_config["DesktopPresentHz"].ValueAsDefault<double>(0) > 0) {
        // The game clock is not the monitor clock. Let SDL retain the desktop
        // refresh preference rather than requesting the arcade's 57/58Hz mode.
        SDL_SetWindowDisplayMode(s_window, nullptr);
        return;
    }
    float refreshRateHz = std::abs(s_runtime_config["RefreshRate"].ValueAs<float>());

    if (refreshRateHz > 57.f && refreshRateHz < 58.f) {

        int display_in_use = 0; /* Only using first display */

        int display_mode_count = SDL_GetNumDisplayModes(display_in_use);
        if (display_mode_count < 1) {
            return;
        }

        for (int i = 0; i < display_mode_count; ++i) {

            SDL_DisplayMode mode;

            if (SDL_GetDisplayMode(display_in_use, i, &mode) != 0) {
                return;
            }

            if (SDL_BITSPERPIXEL(mode.format) >= 24 && (unsigned)mode.w == totalXRes && (unsigned)mode.h == totalYRes) {
                if (mode.refresh_rate == 57 || mode.refresh_rate == 58) {       // nvidia is fairly flexible in what refresh rate windows will show, so we can match either 57 or 58,
                    int result = SDL_SetWindowDisplayMode(s_window, &mode);     // both are totally non standard frequencies and shouldn't be set incorrectly
                    if (result == 0) {
                        printf("Custom fullscreen mode set: %ix%i@57.524 Hz\n", mode.w, mode.h);
                    }
                    break;
                }
            }
        }
    }
}

/*
 * CreateGLScreen():
 *
 * Creates an OpenGL display surface of the requested size. xOffset and yOffset
 * are used to return a display surface offset (for OpenGL viewport commands)
 * because the actual drawing area may need to be adjusted to preserve the
 * Model 3 aspect ratio. The new resolution will be passed back as well -- both
 * the adjusted viewable area resolution and the total resolution.
 *
 * NOTE: keepAspectRatio should always be true. It has not yet been tested with
 * the wide screen hack.
 */
static int s_swapIntervalSetResult = 0;

static void ApplySwapInterval()
{
  const int requested = s_runtime_config["VSync"].ValueAsDefault<bool>(false) ? 1 : 0;
  const int result = s_swapIntervalSetResult = SDL_GL_SetSwapInterval(requested);
  const int actual = SDL_GL_GetSwapInterval();
  InfoLog("OpenGL swap interval: requested %d, reported %d, set result %d.", requested, actual, result);
  if (result != 0)
    InfoLog("Warning: OpenGL rejected the requested swap interval: %s. Actual presentation timing must be measured.", SDL_GetError());
  else if (actual != requested)
    InfoLog("Warning: the driver reports a different swap interval; unsynchronized swaps are not confirmed. Native game pacing remains enabled in independent presentation mode.");
}

static Result CreateGLScreen(bool coreContext, bool quadRendering, const std::string &caption, bool focusWindow, unsigned *xOffsetPtr, unsigned *yOffsetPtr, unsigned *xResPtr, unsigned *yResPtr, unsigned *totalXResPtr, unsigned *totalYResPtr, bool keepAspectRatio, bool fullScreen)
{
  GLenum err;

  // Call only once per program session (this is because of issues with
  // DirectInput when the window is destroyed and a new one created). Use
  // ResizeGLScreen() to change resolutions instead.
  if (s_window != nullptr)
  {
    return ErrorLog("Internal error: CreateGLScreen() called more than once");
  }

  // Initialize video subsystem
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
    return ErrorLog("Unable to initialize SDL video subsystem: %s\n", SDL_GetError());

  // Important GL attributes
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE,8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,8);
  if(s_runtime_config["New3DEngine"].ValueAsDefault<bool>(true))
  {
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
  }
  else
  {
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,8);
  }
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);

  if (coreContext) {
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

      // SteamVR's OpenXR OpenGL binding requires a newer context than the
      // desktop renderer's 4.1 baseline. Exercise that same context in the
      // off-headset eye diagnostic; ordinary desktop launches keep 4.1.
      const bool headsetContext = s_runtime_config["OpenXR"].ValueAsDefault<bool>(false) ||
          !s_runtime_config["HeadsetRenderTest"].ValueAsDefault<std::string>("").empty() ||
          !s_runtime_config["HeadsetWorkerTest"].ValueAsDefault<std::string>("").empty();
      if (quadRendering || headsetContext) {
          SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
          SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
          if (headsetContext)
              InfoLog("Requesting OpenGL 4.5 core for headset rendering; runtime requirements will be checked before session creation.");
      }
      else {
          SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
          SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
      }
  }

  // A bounded, muted diagnostic may create a hidden GL surface. This never
  // hides or moves a user's existing window and never takes desktop focus.
  const bool diagnosticHidden=s_runtime_config["DiagnosticHidden"].ValueAsDefault<bool>(false);
  const int windowPosition=diagnosticHidden?SDL_WINDOWPOS_UNDEFINED:SDL_WINDOWPOS_CENTERED;
  s_window = SDL_CreateWindow(caption.c_str(), windowPosition, windowPosition, *xResPtr, *yResPtr,
    SDL_WINDOW_OPENGL | (diagnosticHidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN) | (fullScreen ? SDL_WINDOW_FULLSCREEN : 0));
  if (nullptr == s_window)
  {
    ErrorLog("Unable to create an OpenGL display: %s\n", SDL_GetError());
    return Result::FAIL;
  }

  if (diagnosticHidden) {
    if (!(SDL_GetWindowFlags(s_window)&SDL_WINDOW_HIDDEN) || (SDL_GetWindowFlags(s_window)&SDL_WINDOW_SHOWN))
      return ErrorLog("Diagnostic GL surface unexpectedly visible.");
    InfoLog("Diagnostic surface: hidden, no focus requested; bounded muted run.");
  }
  if (focusWindow && !diagnosticHidden)
  {
    SDL_RaiseWindow(s_window);
  }

  // Create OpenGL context
  SDL_GLContext context = SDL_GL_CreateContext(s_window);
  if (nullptr == context)
  {
    ErrorLog("Unable to create OpenGL context: %s\n", SDL_GetError());
    return Result::FAIL;
  }

  // Set the context as the current window context
  if (SDL_GL_MakeCurrent(s_window, context) != 0) {
    ErrorLog("Unable to activate OpenGL context: %s", SDL_GetError());
    return Result::FAIL;
  }
  ApplySwapInterval();

  // Initialize GLEW, allowing us to use features beyond OpenGL 1.2
  err = glewInit();
  if (GLEW_OK != err)
  {
    ErrorLog("OpenGL initialization failed: %s\n", glewGetErrorString(err));
    return Result::FAIL;
  }

  // print some basic GPU info
  GLint profile = 0;
  glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);

  printf("GPU info: %s ", glGetString(GL_VERSION));

  if (profile & GL_CONTEXT_CORE_PROFILE_BIT) {
      printf("(core profile)");
  }

  if (profile & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) {
      printf("(compatibility profile)");
  }

  printf("\n\n");

  //glDebugMessageCallback(DebugCallback, NULL);
  //glDebugMessageControl(GL_DONT_CARE,GL_DONT_CARE,GL_DONT_CARE, 0, 0, GL_TRUE);
  //glEnable(GL_DEBUG_OUTPUT);

  return SetGLGeometry(xOffsetPtr, yOffsetPtr, xResPtr, yResPtr, totalXResPtr, totalYResPtr, keepAspectRatio);
}

static void DestroyGLScreen()
{
  if (s_window != nullptr)
  {
    SDL_GL_DeleteContext(SDL_GL_GetCurrentContext());
    SDL_DestroyWindow(s_window);
  }
}

static Result ResizeGLScreen(unsigned *xOffsetPtr, unsigned *yOffsetPtr, unsigned *xResPtr, unsigned *yResPtr, unsigned *totalXResPtr, unsigned *totalYResPtr, bool keepAspectRatio, bool fullScreen)
{
  // Set full screen mode
  if (SDL_SetWindowFullscreen(s_window, fullScreen ? SDL_WINDOW_FULLSCREEN : 0) < 0)
  {
    ErrorLog("Unable to enter %s mode: %s\n", fullScreen ? "fullscreen" : "windowed", SDL_GetError());
    return Result::FAIL;
  }

  // Window/display transitions may reset the driver's swap interval.
  ApplySwapInterval();
  const int display = SDL_GetWindowDisplayIndex(s_window);
  SDL_DisplayMode mode;
  if (display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0)
    InfoLog("Active display mode: %d x %d at %d Hz; game and desktop presentation clocks are separate.", mode.w, mode.h, mode.refresh_rate);

  return SetGLGeometry(xOffsetPtr, yOffsetPtr, xResPtr, yResPtr, totalXResPtr, totalYResPtr, keepAspectRatio);
}

/*
 * PrintGLInfo():
 *
 * Queries and prints OpenGL information. A full list of extensions can
 * optionally be printed.
 */
static void PrintGLInfo(bool createScreen, bool infoLog, bool printExtensions)
{
  unsigned xOffset, yOffset, xRes=496, yRes=384, totalXRes, totalYRes;
  if (createScreen)
  {
    if (Result::OKAY != CreateGLScreen(s_runtime_config["New3DEngine"].ValueAs<bool>(), s_runtime_config["QuadRendering"].ValueAs<bool>(), "Supermodel - Querying OpenGL Information...", false, &xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, false, false))
    {
      ErrorLog("Unable to query OpenGL.\n");
      return;
    }
  }

  GLint value;
  if (infoLog)  InfoLog("OpenGL information:");
  else             puts("OpenGL information:\n");
  const GLubyte *str = glGetString(GL_VENDOR);
  if (infoLog)  InfoLog("  Vendor                   : %s", str);
  else           printf("  Vendor                   : %s\n", str);
  str = glGetString(GL_RENDERER);
  if (infoLog)  InfoLog("  Renderer                 : %s", str);
  else           printf("  Renderer                 : %s\n", str);
  str = glGetString(GL_VERSION);
  if (infoLog)  InfoLog("  Version                  : %s", str);
  else           printf("  Version                  : %s\n", str);
  str = glGetString(GL_SHADING_LANGUAGE_VERSION);
  if (infoLog)  InfoLog("  Shading Language Version : %s", str);
  else           printf("  Shading Language Version : %s\n", str);
  glGetIntegerv(GL_MAX_ELEMENTS_VERTICES, &value);
  if (infoLog)  InfoLog("  Maximum Vertex Array Size: %d vertices", value);
  else           printf("  Maximum Vertex Array Size: %d vertices\n", value);
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
  if (infoLog)  InfoLog("  Maximum Texture Size     : %d texels", value);
  else           printf("  Maximum Texture Size     : %d texels\n", value);
  glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &value);
  if (infoLog)  InfoLog("  Maximum Vertex Attributes: %d", value);
  else           printf("  Maximum Vertex Attributes: %d\n", value);
  glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &value);
  if (infoLog)  InfoLog("  Maximum Vertex Uniforms  : %d", value);
  else           printf("  Maximum Vertex Uniforms  : %d\n", value);
  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &value);
  if (infoLog)  InfoLog("  Maximum Texture Img Units: %d", value);
  else           printf("  Maximum Texture Img Units: %d\n", value);
  if (printExtensions)
  {
    str = glGetString(GL_EXTENSIONS);
    char *strLocal = (char *) malloc((strlen((char *) str)+1)*sizeof(char));
    if (NULL == strLocal)
    {
      if (infoLog)  InfoLog("  Supported Extensions     : %s", str);
      else           printf("  Supported Extensions     : %s\n", str);
    }
    else
    {
      strcpy(strLocal, (char *) str);
      if (infoLog)  InfoLog("  Supported Extensions     : %s", (strLocal = strtok(strLocal, " \t\n")));
      else           printf("  Supported Extensions     : %s\n", (strLocal = strtok(strLocal, " \t\n")));
      char* strLocalTmp = strLocal;
      while ((strLocalTmp = strtok(NULL, " \t\n")) != NULL)
      {
        if (infoLog)  InfoLog("                             %s", strLocalTmp);
        else           printf("                             %s\n", strLocalTmp);
      }
    }
    free(strLocal);
  }
  if (infoLog)  InfoLog("");
  else      printf("\n");
}

#ifdef DEBUG
static void PrintBAT(unsigned regu, unsigned regl)
{
  uint32_t batu = ppc_read_spr(regu);
  uint32_t batl = ppc_read_spr(regl);
  uint32_t bepi = batu >> (31 - 14);
  uint32_t bl = (batu >> (31 - 29)) & 0x7ff;
  bool vs = batu & 2;
  bool vp = batu & 1;
  uint32_t brpn = batl >> (31 - 14);
  uint32_t wimg = (batl >> (31 - 28)) & 0xf;
  uint32_t pp = batl & 3;
  uint32_t size = (bl + 1) * 128 * 1024;
  uint32_t ea_base = bepi << (31 - 14);
  uint32_t ea_limit = ea_base + size - 1;
  uint32_t pa_base = brpn << (31 - 14);
  uint32_t pa_limit = pa_base + size - 1;
  printf("%08X-%08X -> %08X-%08X ", ea_base, ea_limit, pa_base, pa_limit);
  printf("%c%c%c%c ", (wimg&8)?'W':'-', (wimg&4)?'I':'-', (wimg&2)?'M':'-', (wimg&1)?'G':'-');
  printf("PP=");
  if (pp == 0)
    printf("NA");
  else if (pp == 2)
    printf("RW");
  else
    printf("RO");
  printf(" Vs=%d Vp=%d", vs, vp);
}
#endif

#ifdef DEBUG
static void DumpPPCRegisters(IBus *bus)
{
  for (int i = 0; i < 32; i += 4)
  {
    printf("R%d=%08X\tR%d=%08X\tR%d=%08X\tR%d=%08X\n",
           i + 0, ppc_get_gpr(i + 0),
           i + 1, ppc_get_gpr(i + 1),
           i + 2, ppc_get_gpr(i + 2),
           i + 3, ppc_get_gpr(i + 3));
  }
  printf("PC =%08X\n", ppc_get_pc());
  printf("LR =%08X\n", ppc_get_lr());
  printf("DBAT0U=%08X\tIBAT0U=%08X\n", ppc_read_spr(SPR603E_DBAT0U), ppc_read_spr(SPR603E_IBAT0U));
  printf("DBAT0L=%08X\tIBAT0L=%08X\n", ppc_read_spr(SPR603E_DBAT0L), ppc_read_spr(SPR603E_IBAT0L));
  printf("DBAT1U=%08X\tIBAT1U=%08X\n", ppc_read_spr(SPR603E_DBAT1U), ppc_read_spr(SPR603E_IBAT1U));
  printf("DBAT1L=%08X\tIBAT1L=%08X\n", ppc_read_spr(SPR603E_DBAT1L), ppc_read_spr(SPR603E_IBAT1L));
  printf("DBAT2U=%08X\tIBAT2U=%08X\n", ppc_read_spr(SPR603E_DBAT2U), ppc_read_spr(SPR603E_IBAT2U));
  printf("DBAT2L=%08X\tIBAT2L=%08X\n", ppc_read_spr(SPR603E_DBAT2L), ppc_read_spr(SPR603E_IBAT2L));
  printf("DBAT3U=%08X\tIBAT3U=%08X\n", ppc_read_spr(SPR603E_DBAT3U), ppc_read_spr(SPR603E_IBAT3U));
  printf("DBAT3L=%08X\tIBAT3L=%08X\n", ppc_read_spr(SPR603E_DBAT3L), ppc_read_spr(SPR603E_IBAT3L));
  for (int i = 0; i < 10; i++)
    printf("SR%d =%08X VSID=%06X\n", i, ppc_read_sr(i), ppc_read_sr(i) & 0x00ffffff);
  for (int i = 10; i < 16; i++)
    printf("SR%d=%08X VSID=%06X\n", i, ppc_read_sr(i), ppc_read_sr(i) & 0x00ffffff);
  printf("SDR1=%08X\n", ppc_read_spr(SPR603E_SDR1));
  printf("\n");
  printf("DBAT0: "); PrintBAT(SPR603E_DBAT0U, SPR603E_DBAT0L); printf("\n");
  printf("DBAT1: "); PrintBAT(SPR603E_DBAT1U, SPR603E_DBAT1L); printf("\n");
  printf("DBAT2: "); PrintBAT(SPR603E_DBAT2U, SPR603E_DBAT2L); printf("\n");
  printf("DBAT3: "); PrintBAT(SPR603E_DBAT3U, SPR603E_DBAT3L); printf("\n");
  printf("IBAT0: "); PrintBAT(SPR603E_IBAT0U, SPR603E_IBAT0L); printf("\n");
  printf("IBAT1: "); PrintBAT(SPR603E_IBAT1U, SPR603E_IBAT1L); printf("\n");
  printf("IBAT2: "); PrintBAT(SPR603E_IBAT2U, SPR603E_IBAT2L); printf("\n");
  printf("IBAT3: "); PrintBAT(SPR603E_IBAT3U, SPR603E_IBAT3L); printf("\n");
  printf("\n");
  /*
  printf("First PTEG:\n");
  uint32_t ptab = ppc_read_spr(SPR603E_SDR1) & 0xffff0000;
  for (int i = 0; i < 65536/8; i++)
  {
    uint64_t pte = bus->Read64(ptab + i*8);
    uint32_t vsid = (pte >> (32 + (31 - 24))) & 0x00ffffff;
    uint32_t rpn = pte & 0xfffff000;
    int wimg = (pte >> 3) & 0xf;
    bool v = pte & 0x8000000000000000ULL;
    printf("  %d: %016llX V=%d VSID=%06X RPN=%08X WIMG=%c%c%c%c\n", i, pte, v, vsid, rpn, (wimg&8)?'W':'-', (wimg&4)?'I':'-', (wimg&2)?'M':'-', (wimg&1)?'G':'-');
  }
  */
}
#endif

static void SaveFrameBuffer(const std::string& file)
{
    std::shared_ptr<uint8_t> pixels(new uint8_t[totalXRes * totalYRes * 4], std::default_delete<uint8_t[]>());
    glReadPixels(0, 0, totalXRes, totalYRes, GL_RGBA, GL_UNSIGNED_BYTE, pixels.get());
    Util::WriteSurfaceToBMP<Util::RGBA8>(file, pixels.get(), totalXRes, totalYRes, true);
}

static void ReleaseMouseLook(bool requireRelease = true)
{
  if (!s_mouseLook) return;
  if (requireRelease) s_mouseLook->Release();
  SDL_SetRelativeMouseMode(SDL_FALSE);
  SDL_GetRelativeMouseState(nullptr, nullptr);
  SDL_ShowCursor(SDL_ENABLE);
}

struct MouseLookCleanup
{
  ~MouseLookCleanup() { ReleaseMouseLook(); s_mouseLook.reset(); }
};

static void UpdateMouseLook(bool synthetic, uint64_t frame)
{
  if (!s_mouseLook) return;
  if (synthetic) {
    // Bounded regression input, never an OS mouse injection or pointer grab.
    // Frames 3..5 look right/up, 6 recenters, 10..13 exercise focus/rearming.
    const bool focused = frame != 10;
    const bool down = frame == 2 || frame == 3 || (frame >= 8 && frame <= 11) || frame == 13 || frame == 14;
    const int dx = frame == 3 ? 250 : frame == 9 ? -125 : frame == 14 ? 125 : 500;
    const int dy = frame == 3 ? -100 : frame == 9 ? 50 : frame == 14 ? -50 : 500;
    s_mouseLook->Update(focused, down, dx, dy, frame == 6 || frame == 15);
    return;
  }
  int dx = 0, dy = 0;
  SDL_GetRelativeMouseState(&dx, &dy);
  const bool focused = SDL_GetKeyboardFocus() == s_window &&
    !(SDL_GetWindowFlags(s_window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN));
  const bool rightDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_RMASK) != 0;
  const bool recenter = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_HOME] != 0;
  const bool wasCaptured = s_mouseLook->Captured();
  s_mouseLook->Update(focused, rightDown, dx, dy, recenter);
  if (s_mouseLook->Captured() != wasCaptured) {
    if (s_mouseLook->Captured()) {
      if (SDL_SetRelativeMouseMode(SDL_TRUE) != 0) {
        ErrorLog("Mouse look could not capture the pointer: %s", SDL_GetError());
        ReleaseMouseLook();
      }
    }
    else ReleaseMouseLook(false);
    // Discard the first delta after changing relative mode (including warps).
    SDL_GetRelativeMouseState(nullptr, nullptr);
  }
}

static bool CaptureDiagnosticFrame(const std::string& prefixOverride = "")
{
  const std::string prefix = prefixOverride.empty() ? s_runtime_config["CapturePrefix"].ValueAs<std::string>() : prefixOverride;
  const std::string imagePath = prefix + ".bmp";
  std::vector<uint8_t> pixels(size_t(totalXRes) * totalYRes * 4);
  GLint readFBO, readBuffer, packAlignment, packRowLength, packSkipRows, packSkipPixels, packBuffer;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFBO);
  glGetIntegerv(GL_READ_BUFFER, &readBuffer);
  glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
  glGetIntegerv(GL_PACK_ROW_LENGTH, &packRowLength);
  glGetIntegerv(GL_PACK_SKIP_ROWS, &packSkipRows);
  glGetIntegerv(GL_PACK_SKIP_PIXELS, &packSkipPixels);
  glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glReadBuffer(GL_BACK);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  GLenum priorError = glGetError();
  if (priorError != GL_NO_ERROR)
    ErrorLog("OpenGL error before diagnostic readback: 0x%x", unsigned(priorError));
  glReadPixels(0, 0, totalXRes, totalYRes, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  GLenum readError = glGetError();
  glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
  glReadBuffer(readBuffer);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
  glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
  glPixelStorei(GL_PACK_ROW_LENGTH, packRowLength);
  glPixelStorei(GL_PACK_SKIP_ROWS, packSkipRows);
  glPixelStorei(GL_PACK_SKIP_PIXELS, packSkipPixels);
  if (readError != GL_NO_ERROR)
  {
    ErrorLog("Diagnostic framebuffer read failed: OpenGL error 0x%x", unsigned(readError));
    return false;
  }
  if (Util::WriteSurfaceToBMP<Util::RGBA8>(imagePath, pixels.data(), totalXRes, totalYRes, true))
    return false;

  std::ofstream metadata(prefix + ".txt");
  metadata << "Supermodel desktop camera diagnostic\n"
           << "Version=" SUPERMODEL_VERSION "\n"
           << "Game=" << s_diagnosticGame << '\n'
           << "EmulatedFrame=" << s_emulatedFrame << '\n'
           << "FrameSemantics=RunFrame calls after reset/load; presentation redraws excluded\n"
           << "PresentationOnly=" << (!prefixOverride.empty()) << '\n'
           << "Readback=default framebuffer GL_BACK before swap\n"
           << "Width=" << totalXRes << "\nHeight=" << totalYRes << '\n'
           << "OpenGLVendor=" << reinterpret_cast<const char *>(glGetString(GL_VENDOR)) << '\n'
           << "OpenGLRenderer=" << reinterpret_cast<const char *>(glGetString(GL_RENDERER)) << '\n';
  for (const char *key : { "UserMenu", "UserMenuStart", "UserMenuRestart", "UserMenuCalibrate", "UserComfortStrength", "StereoDiagnostic", "StereoEyeSeparation", "StereoYaw", "StereoPitch",
                           "StereoCullBypass", "StereoLodDistance", "StereoFullBike", "StereoStableChase", "StereoChaseHeight", "StereoChaseBack", "StereoUI", "StereoCinematicVignette", "StereoMenuComfort", "StereoFlatIntro", "StereoTheater", "StereoTheaterIntro", "StereoTheaterScreenAngle", "StereoTheaterDepth", "DiagnosticHidden", "MultiThreaded", "GPUMultiThreaded", "New3DEngine",
                           "QuadRendering", "Supersampling", "CRTcolors", "WideScreen", "Stretch",
                           "RunFrames", "InitStateFile", "CaptureState", "InputScript", "FrameProfile", "FrameProfileStart", "FrameProfileFrames", "ProfileThreaded", "StereoHorizonLock", "StereoRiderAnchor",
                           "StereoHorizonFov", "StereoRiderUp", "StereoRiderBack", "StereoRiderComfort", "StereoVignetteScale", "StereoCameraTrace", "StereoCameraTraceStart", "StereoCameraTraceFrames" })
    metadata << key << '=' << s_runtime_config[key].ValueAs<std::string>() << '\n';
  metadata << "StereoUnits=game units; eye separation is experimental, not meters\n"
           << "StereoLayers=" << (s_runtime_config["StereoUI"].ValueAs<bool>() ? "native bottom/top tiles and 3D UI included per eye" : "3D only when StereoDiagnostic=true; 2D tile layers omitted") << '\n'
           << (prefixOverride.empty() ? "CaptureStateSemantics=snapshot BEFORE captured RunFrame; replay using load-state and capture-frame=1\n" :
               "CaptureStateSemantics=presentation test states bracket redraws AFTER the native RunFrame; these are mutation checks, not render-replay snapshots\n")
           << "ReplayComparison=compare fresh runs from the same snapshot and same frame count; repeat the reference\n"
           << "ReplayLimitation=renderer line-of-sight feedback is not saved; original capture and first replay may differ\n";
  metadata << "InputScriptFormat=frame control value; 1-based RunFrame count; values hold until next event; # comments\n"
           << "InputScriptNeutral=all Harley game inputs off, steering centered at 128; emulator UI remains live\n"
           << "InputScriptEventCount=" << s_inputScriptEvents.size() << '\n';
  for (const auto &event : s_inputScriptEvents)
    metadata << "InputScriptEvent=" << event.frame << ' ' << event.control << ' ' << event.value << '\n';
  for (const auto &held : s_inputScriptHeld)
    metadata << "InputScriptHeldAtCapture=" << held.first << ' ' << held.second << '\n';
  metadata << "InputScriptSnapshotNote=inputs are not saved; restore held values at replay frame 1 or replay the original timeline\n";
  metadata << "MouseLook=" << bool(s_mouseLook) << '\n'
           << "MouseLookSynthetic=" << s_runtime_config["MouseLookTest"].ValueAs<bool>() << '\n'
           << "MouseLookYaw=" << (s_mouseLook ? s_mouseLook->Yaw() : 0) << '\n'
           << "MouseLookPitch=" << (s_mouseLook ? s_mouseLook->Pitch() : 0) << '\n'
           << "StereoMonoPreview=" << s_runtime_config["StereoMonoPreview"].ValueAs<bool>() << '\n';
  metadata.close();
  if (!metadata)
  {
    ErrorLog("Unable to write diagnostic metadata '%s.txt'.", prefix.c_str());
    return false;
  }
  printf("Captured emulated frame %llu to '%s'.\n", (unsigned long long)s_emulatedFrame, imagePath.c_str());
  InfoLog("Captured emulated frame %llu to '%s' before buffer swap.", (unsigned long long)s_emulatedFrame, imagePath.c_str());
  return true;
}

void Screenshot()
{
    // Make a screenshot
    time_t now = std::time(nullptr);
    tm* ltm = std::localtime(&now);
    std::string file = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Screenshots)
        << "Screenshot_"
        << std::setfill('0') << std::setw(4) << (1900 + ltm->tm_year)
        << '-'
        << std::setw(2) << (1 + ltm->tm_mon)
        << '-'
        << std::setw(2) << ltm->tm_mday
        << "_("
        << std::setw(2) << ltm->tm_hour
        << '-'
        << std::setw(2) << ltm->tm_min
        << '-'
        << std::setw(2) << ltm->tm_sec
        << ").bmp";

    std::cout << "Screenshot created: " << file << std::endl;
    SaveFrameBuffer(file);
}

/******************************************************************************
 Render State Analysis
******************************************************************************/

#ifdef DEBUG

#include "Model3/Model3GraphicsState.h"
#include "OSD/SDL/PolyAnalysis.h"
#include <fstream>

static std::string s_gfxStatePath;
static const std::string k_gfxAnalysisPath = "GraphicsAnalysis/";

static std::string GetFileBaseName(const std::string &file)
{
  std::string base = file;
  size_t pos = file.find_last_of('/');
  if (pos != std::string::npos)
    base = file.substr(pos + 1);
  pos = file.find_last_of('\\');
  if (pos != std::string::npos)
    base = file.substr(pos + 1);
  return base;
}

static void TestPolygonHeaderBits(IEmulator *Emu)
{
  const static std::vector<uint32_t> unknownPolyBits
  {
    0xffffffff,
    0x000000ab, // actual color
    0x000000fc,
    0x000000c0,
    0x000000a0,
    0xffffff60,
    0xff0300ff  // contour, luminous, etc.
  };

  const std::vector<uint32_t> unknownCullingNodeBits
  {
    0xffffffff,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000
  };

  GLint readBuffer;
  glGetIntegerv(GL_READ_BUFFER, &readBuffer);
  glReadBuffer(GL_FRONT);

  // Render separate image for each unknown bit
  s_runtime_config.Set("Debug/ForceFlushModels", true);
  for (int idx = 0; idx < 7; idx++)
  {
    for (int bit = 0; bit < 32; bit++)
    {
      uint32_t mask = 1 << bit;
      s_runtime_config.Set("Debug/HighlightPolyHeaderIdx", idx);
      s_runtime_config.Set("Debug/HighlightPolyHeaderMask", mask);
      if ((unknownPolyBits[idx] & mask))
      {
        Emu->RenderFrame();
        std::string file = Util::Format() << k_gfxAnalysisPath << GetFileBaseName(s_gfxStatePath) << "." << "poly" << "." << idx << "_" << Util::Hex(mask) << ".bmp";
        SaveFrameBuffer(file);
      }
    }
  }

  for (int idx = 0; idx < 10; idx++)
  {
    for (int bit = 0; bit < 32; bit++)
    {
      uint32_t mask = 1 << bit;
      s_runtime_config.Set("Debug/HighlightCullingNodeIdx", idx);
      s_runtime_config.Set("Debug/HighlightCullingNodeMask", mask);
      if ((unknownCullingNodeBits[idx] & mask))
      {
        Emu->RenderFrame();
        std::string file = Util::Format() << k_gfxAnalysisPath << GetFileBaseName(s_gfxStatePath) << "." << "culling" << "." << idx << "_" << Util::Hex(mask) << ".bmp";
        SaveFrameBuffer(file);
      }
    }
  }

  glReadBuffer(readBuffer);

  // Generate the HTML GUI
  std::string file = Util::Format() << k_gfxAnalysisPath << "_" << GetFileBaseName(s_gfxStatePath) << ".html";
  std::ofstream fs(file);
  if (!fs.good())
    ErrorLog("Unable to open '%s' for writing.", file.c_str());
  else
  {
    std::string contents = s_polyAnalysisHTMLPrologue;
    contents += "    var g_file_base_name = '" + GetFileBaseName(s_gfxStatePath) + "';\n";
    contents += "    var g_unknown_poly_bits = [" + std::string(Util::Format(",").Join(unknownPolyBits)) + "];\n";
    contents += "    var g_unknown_culling_bits = [" + std::string(Util::Format(",").Join(unknownCullingNodeBits)) + "];\n";
    contents += s_polyAnalysisHTMLEpilogue;
    fs << contents;
    printf("Produced: %s\n", file.c_str());
  }
}

#endif


/******************************************************************************
 Save States and NVRAM

 Save states and NVRAM use the same basic format. When anything changes that
 breaks compatibility with previous versions of Supermodel, the save state
 and NVRAM version numbers must be incremented as needed.

 Header block name: "Supermodel Save State" or "Supermodel NVRAM State"
 Data: Save state file version (4-byte integer), ROM set ID (up to 9 bytes,
 including terminating \0).

 Different subsystems output their own blocks.
******************************************************************************/

static const int STATE_FILE_VERSION = 6;  // save state file version
static const int NVRAM_FILE_VERSION = 0;  // NVRAM file version
static unsigned s_saveSlot = 0;           // save state slot #

static bool SaveState(IEmulator *Model3, std::string file_path = std::string())
{
  CBlockFile  SaveState;

  if (file_path.empty())
    file_path = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Saves) << Model3->GetGame().name << ".st" << s_saveSlot;
  if (Result::OKAY != SaveState.Create(file_path, "Supermodel Save State", "Supermodel Version " SUPERMODEL_VERSION))
  {
    ErrorLog("Unable to save state to '%s'.", file_path.c_str());
    return false;
  }

  // Write file format version and ROM set ID to header block
  int32_t fileVersion = STATE_FILE_VERSION;
  SaveState.Write(&fileVersion, sizeof(fileVersion));
  SaveState.Write(Model3->GetGame().name);

  // Save state
  Model3->SaveState(&SaveState);
  SaveState.Close();
  printf("Saved state to '%s'.\n", file_path.c_str());
  InfoLog("Saved state to '%s'.", file_path.c_str());
  return true;
}

static bool LoadState(IEmulator *Model3, std::string file_path = std::string())
{
  CBlockFile  SaveState;

  // Generate file path
  if (file_path.empty())
    file_path = Util::Format() << FileSystemPath::GetPath(FileSystemPath::Saves) << Model3->GetGame().name << ".st" << s_saveSlot;

  // Open and check to make sure format is correct
  if (Result::OKAY != SaveState.Load(file_path))
  {
    ErrorLog("Unable to load state from '%s'.", file_path.c_str());
    return false;
  }

  if (Result::OKAY != SaveState.FindBlock("Supermodel Save State"))
  {
    ErrorLog("'%s' does not appear to be a valid save state file.", file_path.c_str());
    return false;
  }

  int32_t fileVersion = -1;
  if (SaveState.Read(&fileVersion, sizeof(fileVersion)) != sizeof(fileVersion) || fileVersion != STATE_FILE_VERSION)
  {
    ErrorLog("'%s' is incompatible with this version of Supermodel.", file_path.c_str());
    return false;
  }

  // Verify the ROM revision before applying a snapshot to the current game.
  std::string savedGame;
  char gameChar = '\0';
  bool terminated = false;
  for (unsigned i = 0; i < 128; i++)
  {
    if (SaveState.Read(&gameChar, 1) != 1)
      break;
    if (gameChar == '\0')
    {
      terminated = true;
      break;
    }
    savedGame += gameChar;
  }
  if (!terminated || savedGame != Model3->GetGame().name)
  {
    ErrorLog("Save state '%s' is for '%s', not the loaded ROM set '%s'.", file_path.c_str(), savedGame.c_str(), Model3->GetGame().name.c_str());
    return false;
  }

  // Load
  Model3->LoadState(&SaveState);
  SaveState.Close();
  printf("Loaded state from '%s'.\n", file_path.c_str());
  InfoLog("Loaded state from '%s'.", file_path.c_str());
  return true;
}

static void SaveNVRAM(IEmulator *Model3)
{
  CBlockFile  NVRAM;

  std::string file_path = Util::Format() << FileSystemPath::GetPath(FileSystemPath::NVRAM) << Model3->GetGame().name << ".nv";
  if (Result::OKAY != NVRAM.Create(file_path, "Supermodel NVRAM State", "Supermodel Version " SUPERMODEL_VERSION))
  {
    ErrorLog("Unable to save NVRAM to '%s'. Make sure directory exists!", file_path.c_str());
    return;
  }

  // Write file format version and ROM set ID to header block
  int32_t fileVersion = NVRAM_FILE_VERSION;
  NVRAM.Write(&fileVersion, sizeof(fileVersion));
  NVRAM.Write(Model3->GetGame().name);

  // Save NVRAM
  Model3->SaveNVRAM(&NVRAM);
  NVRAM.Close();
  InfoLog("Saved NVRAM to '%s'.", file_path.c_str());
}

static void LoadNVRAM(IEmulator *Model3)
{
  CBlockFile  NVRAM;

  // Generate file path
  std::string file_path = Util::Format() << FileSystemPath::GetPath(FileSystemPath::NVRAM) << Model3->GetGame().name << ".nv";

  // Open and check to make sure format is correct
  if (Result::OKAY != NVRAM.Load(file_path))
  {
    //ErrorLog("Unable to restore NVRAM from '%s'.", filePath);
    return;
  }

  if (Result::OKAY != NVRAM.FindBlock("Supermodel NVRAM State"))
  {
    ErrorLog("'%s' does not appear to be a valid NVRAM file.", file_path.c_str());
    return;
  }

  int32_t fileVersion;
  NVRAM.Read(&fileVersion, sizeof(fileVersion));
  if (fileVersion != NVRAM_FILE_VERSION)
  {
    ErrorLog("'%s' is incompatible with this version of Supermodel.", file_path.c_str());
    return;
  }

  // Load
  Model3->LoadNVRAM(&NVRAM);
  NVRAM.Close();
  InfoLog("Loaded NVRAM from '%s'.", file_path.c_str());
}


/*
static void PrintGLError(GLenum error)
{
  switch (error)
  {
  case GL_INVALID_ENUM:       printf("invalid enum\n"); break;
  case GL_INVALID_VALUE:      printf("invalid value\n"); break;
  case GL_INVALID_OPERATION:  printf("invalid operation\n"); break;
  case GL_STACK_OVERFLOW:     printf("stack overflow\n"); break;
  case GL_STACK_UNDERFLOW:    printf("stack underflow\n"); break;
  case GL_OUT_OF_MEMORY:      printf("out of memory\n"); break;
  case GL_TABLE_TOO_LARGE:    printf("table too large\n"); break;
  case GL_NO_ERROR:           break;
  default:                    printf("unknown error\n"); break;
  }
}
*/

/******************************************************************************
 Video Callbacks
******************************************************************************/

static CInputs *videoInputs = NULL;
static uint32_t currentInputs = 0;
static uint64_t s_perfCounterFrequency = 0;

// All fields, including the profile buffer, belong to the GL owner thread.
// Workers can still be running during Pump(): never poll semantic inputs,
// change configuration, inspect worker timings, or touch save-state APIs here.
class DesktopPresentationSession;
static DesktopPresentationSession* s_desktopPresentation = nullptr;

class DesktopPresentationSession
{
public:
  struct Sample
  {
    bool nativeTiming = false, frozen = false, sceneValid = false, unchanged = true;
    const char* source = "native";
    uint64_t frame = 0, drawStart = 0, swapStart = 0, swapEnd = 0;
    IRender3D::PresentationState before, after;
    std::array<uint32_t, 4> losBefore = {}, losAfter = {};
  };

  ~DesktopPresentationSession() { Stop(); Close(); }

  bool Open(const Util::Config::Node& config)
  {
    targetHz = config["DesktopPresentHz"].ValueAs<double>();
    if (targetHz == 0) return true;
    frequency = SDL_GetPerformanceFrequency();
    origin = SDL_GetPerformanceCounter();
    if (!clock.Reset(frequency, targetHz, origin)) return false;
    sampleStart = config["FrameProfileStart"].ValueAs<unsigned>();
    sampleCount = config["FrameProfileFrames"].ValueAs<unsigned>();
    width = config["XResolution"].ValueAs<unsigned>();
    height = config["YResolution"].ValueAs<unsigned>();
    mono = config["StereoMonoPreview"].ValueAs<bool>();
    widescreen = config["WideScreen"].ValueAs<bool>();
    const std::string path = config["PresentationProfile"].ValueAs<std::string>();
    if (!path.empty()) {
      // Extra capacity covers pause/jitter without allocating inside RunFrame.
      // Reaching this explicit bound fails the profile instead of growing it.
      capacity = size_t(sampleCount) * (size_t(std::ceil(targetHz / DesktopPresentationClock::NativeHz)) + 2) + 2048;
      try { samples.reserve(capacity); }
      catch (const std::bad_alloc&) { ErrorLog("Cannot reserve desktop presentation profile buffer."); return false; }
      report = Util::OpenExclusiveDiagnostic(path);
      if (!report) return false;
    }
    return true;
  }

  void Start(IEmulator* emulator, IRender3D* render)
  {
    if (targetHz == 0) return;
    model = emulator;
    renderer = render;
    origin = SDL_GetPerformanceCounter();
    swapInterval = SDL_GL_GetSwapInterval();
    swapSetResult = s_swapIntervalSetResult;
    SDL_GetWindowSize(s_window, &actualWidth, &actualHeight);
    SDL_DisplayMode mode;
    const int display = SDL_GetWindowDisplayIndex(s_window);
    if (display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0) displayRefreshHz = mode.refresh_rate;
    clock.Reset(frequency, targetHz, origin);
    lastDrawTicks = frequency / 1000;
    lastCostUpdate = origin;
    s_desktopPresentation = this;
    model->SetFrameWaitCallback(WorkerWait, this);
  }

  void Stop()
  {
    if (model) model->SetFrameWaitCallback(nullptr, nullptr);
    if (s_desktopPresentation == this) s_desktopPresentation = nullptr;
    model = nullptr;
    renderer = nullptr;
  }

  static bool WorkerWait(void* context)
  {
    auto* session = static_cast<DesktopPresentationSession*>(context);
    session->Pump("workerWait", session->nativeDeadline);
    return false; // Preserve the desktop presentation wait policy.
  }

  void Pump(const char* source, uint64_t nativeDeadline = 0);

  void BeginNativeDraw() { nativeDrawStart = SDL_GetPerformanceCounter(); }

  void BeginNativeFrame(uint64_t begin)
  {
    nativeDeadline = begin + uint64_t(std::ceil(double(frequency) / DesktopPresentationClock::NativeHz));
  }

  bool BeginSwap(uint64_t now)
  {
    return frozen || clock.Take(now);
  }

  void Swapped(uint64_t beforeSwap, uint64_t afterSwap)
  {
    if (!frozen || std::strcmp(frozenSample.source, "paused") != 0) {
      loadGuard.ObserveSwap(afterSwap - beforeSwap, frequency, frozen);
      ReportLoadGuard();
    }
    ++totalPresents;
    if (frozen) ++frozenPresents; else ++nativePresents;
    if (!Sampling()) return;
    Sample sample = frozen ? frozenSample : Sample();
    sample.frame = s_emulatedFrame;
    sample.frozen = frozen;
    sample.drawStart = frozen ? frozenSample.drawStart : nativeDrawStart;
    sample.swapStart = beforeSwap;
    sample.swapEnd = afterSwap;
    if (!frozen) {
      sample.sceneValid = renderer->GetPresentationState(sample.before);
      sample.after = sample.before;
      sample.losBefore = sample.losAfter = Los();
    }
    Add(sample);
  }

  void NativeFrame(uint64_t begin, uint64_t end)
  {
    loadGuard.ObserveNativeFrame(end - begin, frequency);
    ReportLoadGuard();
    nativeDeadline = 0;
    completedNativeFrames = s_emulatedFrame;
    if (!Sampling()) return;
    Sample sample;
    sample.nativeTiming = true;
    sample.frame = s_emulatedFrame;
    sample.drawStart = begin;
    sample.swapEnd = end;
    Add(sample);
  }

  bool Close()
  {
    if (!report) return !failed;
    fprintf(report, "{\"type\":\"header\",\"schemaVersion\":1,\"frequencyHz\":%llu,\"originTicks\":%llu,\"targetHz\":%.9g,\"nativeHz\":%.9g,\"startFrame\":%llu,\"frameCount\":%llu,\"width\":%u,\"height\":%u,\"actualWidth\":%d,\"actualHeight\":%d,\"displayRefreshHz\":%d,\"swapInterval\":%d,\"swapSetResult\":%d,\"requestedSwapApplied\":%s,\"threadingRequested\":true,\"gpuThreadingRequested\":true,\"vsyncRequested\":false,\"stereoDiagnostic\":true,\"monoPreview\":%s,\"wideScreen\":%s}\n",
      U(frequency), U(origin), targetHz, DesktopPresentationClock::NativeHz, U(sampleStart), U(sampleCount), width, height, actualWidth, actualHeight, displayRefreshHz, swapInterval, swapSetResult, (swapSetResult == 0 && swapInterval == 0) ? "true" : "false", mono ? "true" : "false", widescreen ? "true" : "false");
    uint64_t sampledPresents = 0, sampledFrames = 0;
    for (const auto& sample : samples) {
      if (sample.nativeTiming) {
        ++sampledFrames;
        fprintf(report, "{\"type\":\"nativeFrame\",\"nativeFrame\":%llu,\"startTicks\":%llu,\"endTicks\":%llu,\"workMs\":%.9g}\n",
          U(sample.frame), U(sample.drawStart - origin), U(sample.swapEnd - origin), Ms(sample.swapEnd - sample.drawStart));
        continue;
      }
      ++sampledPresents;
      const auto& before = sample.before;
      const auto& after = sample.after;
      fprintf(report, "{\"type\":\"present\",\"kind\":\"%s\",\"source\":\"%s\",\"nativeFrame\":%llu,\"sceneValid\":%s,\"sceneFrame\":%llu,\"sceneEpoch\":%llu,\"menuComfortTicks\":%llu,\"verticalOffset\":%.17g,\"vignetteStrength\":%.17g,\"menuVignetteStrength\":%.17g,\"drawStartTicks\":%llu,\"swapStartTicks\":%llu,\"swapEndTicks\":%llu,\"drawMs\":%.9g,\"swapMs\":%.9g,\"stateUnchanged\":%s,\"afterSceneFrame\":%llu,\"afterSceneEpoch\":%llu,\"afterMenuComfortTicks\":%llu,\"afterVerticalOffset\":%.17g,\"afterVignetteStrength\":%.17g,\"afterMenuVignetteStrength\":%.17g,\"losBits\":[%u,%u,%u,%u],\"afterLosBits\":[%u,%u,%u,%u]}\n",
        sample.frozen ? "frozen" : "native", sample.source, U(sample.frame), sample.sceneValid ? "true" : "false",
        U(before.sceneFrame), U(before.sceneEpoch), U(before.menuComfortTicks), before.verticalOffset, before.vignetteStrength, before.menuVignetteStrength,
        U(sample.drawStart - origin), U(sample.swapStart - origin), U(sample.swapEnd - origin), Ms(sample.swapStart - sample.drawStart), Ms(sample.swapEnd - sample.swapStart), sample.unchanged ? "true" : "false",
        U(after.sceneFrame), U(after.sceneEpoch), U(after.menuComfortTicks), after.verticalOffset, after.vignetteStrength, after.menuVignetteStrength,
        sample.losBefore[0], sample.losBefore[1], sample.losBefore[2], sample.losBefore[3], sample.losAfter[0], sample.losAfter[1], sample.losAfter[2], sample.losAfter[3]);
    }
    failed |= truncated || invariantFailures != 0 || sampledFrames != sampleCount || sampledPresents == 0;
    fprintf(report, "{\"type\":\"result\",\"sampleCount\":%llu,\"sampledNativeFrames\":%llu,\"totalPresents\":%llu,\"nativePresents\":%llu,\"frozenPresents\":%llu,\"frozenAttempts\":%llu,\"rejectedRedraws\":%llu,\"invariantFailures\":%llu,\"truncated\":%s,\"completedNativeFrames\":%llu,\"passed\":%s}\n",
      U(sampledPresents), U(sampledFrames), U(totalPresents), U(nativePresents), U(frozenPresents), U(frozenAttempts), U(rejectedRedraws), U(invariantFailures), truncated ? "true" : "false", U(completedNativeFrames), failed ? "false" : "true");
    failed |= std::ferror(report) != 0;
    failed |= std::fclose(report) != 0;
    report = nullptr;
    if (failed) ErrorLog("Desktop presentation profile failed or did not complete its bounded sample window.");
    return !failed;
  }

  bool Failed() const { return failed; }
  uint64_t Presents() const { return totalPresents; }
  bool Guarded() const { return loadGuard.Guarded(); }

private:
  void ReportLoadGuard()
  {
    if (loadGuard.Guarded() == guardReported) return;
    guardReported = loadGuard.Guarded();
    InfoLog(guardReported ? "Desktop swaps are stalling: prioritizing native game speed; optional redraws temporarily reduced." :
      "Desktop swap timing recovered: independent redraws restored.");
  }
  static unsigned long long U(uint64_t value) { return static_cast<unsigned long long>(value); }
  double Ms(uint64_t ticks) const { return double(ticks) * 1000.0 / double(frequency); }
  bool Sampling() const { return report && s_emulatedFrame >= sampleStart && s_emulatedFrame - sampleStart < sampleCount; }
  std::array<uint32_t, 4> Los() const
  {
    std::array<uint32_t, 4> bits;
    for (unsigned i = 0; i < bits.size(); ++i) {
      const float value = renderer->GetLosValue(i);
      std::memcpy(&bits[i], &value, sizeof(value));
    }
    return bits;
  }
  void Add(const Sample& sample)
  {
    if (samples.size() == capacity) { truncated = failed = true; return; }
    samples.push_back(sample);
  }
  IEmulator* model = nullptr;
  IRender3D* renderer = nullptr;
  DesktopPresentationClock clock;
  DesktopPresentLoadGuard loadGuard;
  bool guardReported = false;
  FILE* report = nullptr;
  std::vector<Sample> samples;
  size_t capacity = 0;
  Sample frozenSample;
  double targetHz = 0;
  uint64_t frequency = 0, origin = 0, sampleStart = 0, sampleCount = 0;
  uint64_t nativeDrawStart = 0, lastDrawTicks = 0, nativeDeadline = 0, lastCostUpdate = 0;
  uint64_t totalPresents = 0, nativePresents = 0, frozenPresents = 0;
  uint64_t frozenAttempts = 0, rejectedRedraws = 0, invariantFailures = 0, completedNativeFrames = 0;
  unsigned width = 0, height = 0;
  int swapInterval = 0, swapSetResult = 0, actualWidth = 0, actualHeight = 0, displayRefreshHz = 0;
  bool frozen = false, failed = false, truncated = false, mono = false, widescreen = false;
};

static LiveFrameTrace::OwnerWorkSpan s_nativePreparationTrace;

bool BeginFrameVideo()
{
  s_nativePreparationTrace.Begin(s_emulatedFrame);
  if (s_desktopPresentation) s_desktopPresentation->BeginNativeDraw();
  return true;
}

void EndFrameVideo()
{
  s_nativePreparationTrace.End("game.native_prepare");
  // OpenXR owns presentation. A monitor swap here can block on the desktop's
  // refresh and must never delay the headset's frame submission.
  if (s_headsetOwnsVideo) return;
  // Show crosshairs for light gun games
  if (videoInputs)
    s_crosshair->Update(currentInputs, videoInputs, xOffset, yOffset, xRes, yRes);

  // The cached menu texture is prepared by the owner loop. Desktop capture
  // and display both see the same final panel; hidden menus do no GL work.
  if (s_userMenu && s_userMenu->IsOpen()) s_userMenu->DrawDesktop(s_window);

  uint64_t captureTicks = 0;
  if (s_capturePending)
  {
    const uint64_t beforeCapture = s_cpuProfile.Active() ? SDL_GetPerformanceCounter() : 0;
    s_captureCompleted = CaptureDiagnosticFrame();
    if (s_cpuProfile.Active()) captureTicks = SDL_GetPerformanceCounter() - beforeCapture;
    s_captureFailed = !s_captureCompleted;
    s_capturePending = false;
  }

  // The native scene is still prepared every game frame. Only display swaps
  // are paced independently; skipped swaps never skip emulation or native LOS.
  const uint64_t beforeSwap = (s_cpuProfile.Active() || s_desktopPresentation) ? SDL_GetPerformanceCounter() : 0;
  if (s_desktopPresentation && !s_desktopPresentation->BeginSwap(beforeSwap)) return;
  SDL_GL_SwapWindow(s_window);
  const uint64_t afterSwap = (s_cpuProfile.Active() || s_desktopPresentation) ? SDL_GetPerformanceCounter() : 0;
  if (s_cpuProfile.Active()) s_cpuProfile.Video(captureTicks, afterSwap - beforeSwap);
  if (s_desktopPresentation) s_desktopPresentation->Swapped(beforeSwap, afterSwap);
}

void DesktopPresentationSession::Pump(const char* source, uint64_t nativeDeadline)
{
  if (!model || failed) return;
  SDL_PumpEvents();
  UpdateMouseLook(false, s_emulatedFrame);
  const uint64_t now = SDL_GetPerformanceCounter();
  if (!clock.Due(now)) return;
  // A single driver stall must not permanently suppress every future redraw.
  // While no draw is admitted, cautiously age its estimate every quarter second.
  // The clock still admits only one due redraw; there is no backlog to replay.
  if (now - lastCostUpdate >= frequency / 4 && lastDrawTicks > frequency / 500) {
    lastDrawTicks = (std::max)(frequency / 500, lastDrawTicks / 2);
    lastCostUpdate = now;
  }
  // Do not start an optional redraw in the final sliver before a native tick.
  if (nativeDeadline && (now >= nativeDeadline || nativeDeadline - now <= lastDrawTicks)) return;
  if (!loadGuard.AllowFrozen(std::strcmp(source, "paused") == 0)) return;
  if (!clock.Take(now)) return;
  ++frozenAttempts;
  Sample sample;
  sample.frozen = true;
  sample.source = source;
  sample.sceneValid = renderer->GetPresentationState(sample.before);
  if (!sample.sceneValid) { ++rejectedRedraws; return; }
  sample.losBefore = Los();
  sample.drawStart = SDL_GetPerformanceCounter();
  const IRender3D::PresentationPose pose = s_mouseLook ? s_mouseLook->Pose() : IRender3D::PresentationPose();
  if (!model->RenderPresentation(pose)) { ++rejectedRedraws; return; }
  sample.losAfter = Los();
  sample.unchanged = renderer->GetPresentationState(sample.after) &&
    sample.before.sceneFrame == sample.after.sceneFrame && sample.before.sceneEpoch == sample.after.sceneEpoch &&
    sample.before.menuComfortTicks == sample.after.menuComfortTicks &&
    sample.before.verticalOffset == sample.after.verticalOffset &&
    sample.before.vignetteStrength == sample.after.vignetteStrength &&
    sample.before.menuVignetteStrength == sample.after.menuVignetteStrength && sample.losBefore == sample.losAfter;
  if (!sample.unchanged) {
    ++invariantFailures;
    failed = true;
    ErrorLog("Frozen desktop redraw changed prepared scene, native LOS, or comfort state.");
  }
  frozenSample = sample;
  frozen = true;
  EndFrameVideo();
  frozen = false;
  // The deadline guard budgets the entire owner-thread redraw, verification
  // and swap, including any driver stall. Honor slower samples immediately.
  lastCostUpdate = SDL_GetPerformanceCounter();
  const uint64_t workTicks = lastCostUpdate - sample.drawStart;
  lastDrawTicks = (std::max)(workTicks, (lastDrawTicks * 3 + workTicks) / 4);
}


/******************************************************************************
 Frame Timing
******************************************************************************/

static uint64_t GetDesiredRefreshRateMilliHz()
{
  // The refresh rate is expressed as mHz (millihertz -- Hz * 1000) in order to
  // be expressable as an integer. E.g.: 57.524 Hz -> 57524 mHz.
  float refreshRateHz = std::abs(s_runtime_config["RefreshRate"].ValueAs<float>());
  uint64_t refreshRateMilliHz = uint64_t(1000.0 * refreshRateHz);
  return refreshRateMilliHz;
}

static void SuperSleepUntil(const uint64_t target)
{
  uint64_t time = SDL_GetPerformanceCounter();

  // If we're ahead of the target, we're done
  if (time >= target)
  {
    return;
  }

  // Because OS sleep is not accurate,
  // we actually sleep until a maximum of 2 milliseconds are left.
  while (int64_t(target - time) * 1000 > 2 * int64_t(s_perfCounterFrequency))
  {
    if (s_desktopPresentation) {
      s_desktopPresentation->Pump("throttleWait", target);
      time = SDL_GetPerformanceCounter();
      if (time >= target) return;
      if (int64_t(target - time) * 1000 <= 2 * int64_t(s_perfCounterFrequency)) break;
    }
    SDL_Delay(1);
    time = SDL_GetPerformanceCounter();
  }

  // Spin until requested time
  int64_t remain;
  do
  {
    // according to all available processor documentation for x86 and arm,
    // spinning should pause the processor for a short while for better
    // power efficiency and (surprisingly) overall faster system performance
    #ifdef SDL_CPUPauseInstruction
    SDL_CPUPauseInstruction();
    #endif
    remain = target - SDL_GetPerformanceCounter();
  } while (remain > 0);
}


// Fixed, bounded pose sequence used to prove that presentation is independent
// of emulation. This is a desktop diagnostic, not an OpenXR session or scheduler.
static const std::array<const char*, 11> s_presentationNames = {
  "identity", "left", "right", "up", "down", "roll", "lean", "rise", "forward", "return", "after-sweep"
};

static IRender3D::PresentationPose PresentationRotation(int axis, double degrees)
{
  IRender3D::PresentationPose pose;
  const double halfAngle = degrees * 3.14159265358979323846 / 360.0;
  pose.orientation[axis] = float(std::sin(halfAngle));
  pose.orientation[3] = float(std::cos(halfAngle));
  return pose;
}

static bool SameDiagnosticFile(const std::string& first, const std::string& second)
{
  std::ifstream a(first, std::ios::binary), b(second, std::ios::binary);
  if (!a || !b) return false;
  std::array<char, 65536> ab, bb;
  do {
    a.read(ab.data(), ab.size()); b.read(bb.data(), bb.size());
    if (a.gcount() != b.gcount() || std::memcmp(ab.data(), bb.data(), size_t(a.gcount()))) return false;
  } while (a && b);
  return a.eof() && b.eof() && !a.bad() && !b.bad();
}

static bool RunPresentationDiagnostic(IEmulator* model, IRender3D* renderer, const std::string& prefix)
{
  IRender3D::PresentationState before;
  if (!renderer->GetPresentationState(before)) {
    ErrorLog("Presentation test requires a prepared scene at the selected frame.");
    return false;
  }
  FILE* report = Util::OpenExclusiveDiagnostic(prefix + ".jsonl");
  if (!report) return false;
  auto los = [renderer]() {
    std::array<uint32_t, 4> bits;
    for (int i = 0; i < 4; ++i) {
      const float value = renderer->GetLosValue(i);
      std::memcpy(&bits[i], &value, sizeof(value));
    }
    return bits;
  };
  const auto originalLos = los();
  auto unchanged = [&]() {
    IRender3D::PresentationState now;
    return renderer->GetPresentationState(now) && now.sceneFrame == before.sceneFrame &&
      now.sceneEpoch == before.sceneEpoch && now.verticalOffset == before.verticalOffset &&
      now.vignetteStrength == before.vignetteStrength &&
      now.menuVignetteStrength == before.menuVignetteStrength &&
      now.menuComfortTicks == before.menuComfortTicks && los() == originalLos;
  };
  fprintf(report, "{\"type\":\"scene\",\"emulatedFrame\":%llu,\"sceneFrame\":%llu,\"sceneEpoch\":%llu,\"verticalOffset\":%.17g,\"vignetteStrength\":%.17g,\"menuVignetteStrength\":%.17g,\"menuComfortTicks\":%llu,\"losBits\":[%u,%u,%u,%u]}\n",
    (unsigned long long)s_emulatedFrame, (unsigned long long)before.sceneFrame,
    (unsigned long long)before.sceneEpoch, before.verticalOffset, before.vignetteStrength, before.menuVignetteStrength, (unsigned long long)before.menuComfortTicks,
    originalLos[0], originalLos[1], originalLos[2], originalLos[3]);
  bool okay = SaveState(model, prefix + "-before.st0");
  std::array<IRender3D::PresentationPose, 11> poses;
  poses[1] = PresentationRotation(1, 30); poses[2] = PresentationRotation(1, -30);
  poses[3] = PresentationRotation(0, 15); poses[4] = PresentationRotation(0, -15);
  poses[5] = PresentationRotation(2, 15);
  poses[6].position[0] = .5f; poses[7].position[1] = .5f; poses[8].position[2] = -.5f;
  for (size_t i = 0; okay && i < poses.size(); ++i) {
    // The frozen-scene sweep includes redraw and desktop swap cost. Its swap
    // interval is recorded; this does not measure headset/live-game performance.
    if (i == poses.size() - 1) {
      glFinish();
      const auto start = SDL_GetPerformanceCounter();
      for (unsigned n = 0; okay && n < 180; ++n) {
        const auto pose = PresentationRotation(1, 30 * std::sin(n * 6.283185307179586 / 180));
        okay = model->RenderPresentation(pose) && unchanged();
        if (okay) EndFrameVideo();
      }
      glFinish();
      const double elapsed = double(SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency();
      fprintf(report, "{\"type\":\"frozenSweep\",\"draws\":180,\"completed\":%s,\"wallMs\":%.6f,\"swapInterval\":%d,\"stateUnchanged\":%s}\n",
        okay ? "true" : "false", elapsed, SDL_GL_GetSwapInterval(), unchanged() ? "true" : "false");
      if (!okay) break;
    }
    const auto start = SDL_GetPerformanceCounter();
    const bool accepted = model->RenderPresentation(poses[i]);
    const double submitMs = double(SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency();
    okay = accepted && unchanged() && glGetError() == GL_NO_ERROR;
    fprintf(report, "{\"type\":\"pose\",\"name\":\"%s\",\"accepted\":%s,\"stateUnchanged\":%s,\"submitMs\":%.6f,\"orientation\":[%.9g,%.9g,%.9g,%.9g],\"position\":[%.9g,%.9g,%.9g]}\n",
      s_presentationNames[i], accepted ? "true" : "false", unchanged() ? "true" : "false", submitMs,
      poses[i].orientation[0], poses[i].orientation[1], poses[i].orientation[2], poses[i].orientation[3],
      poses[i].position[0], poses[i].position[1], poses[i].position[2]);
    if (okay) okay = CaptureDiagnosticFrame(prefix + "-" + s_presentationNames[i]);
    if (okay) EndFrameVideo();
  }
  // Invalid external tracking data must be rejected without changing state.
  std::array<IRender3D::PresentationPose, 4> invalid;
  invalid[0].orientation[3] = 0;
  invalid[1].orientation[0] = std::numeric_limits<float>::quiet_NaN();
  invalid[2].position[0] = std::numeric_limits<float>::infinity();
  invalid[3].position[2] = 1001;
  for (size_t i = 0; okay && i < invalid.size(); ++i) {
    const bool rejected = !model->RenderPresentation(invalid[i]);
    okay = rejected && unchanged() && glGetError() == GL_NO_ERROR;
    fprintf(report, "{\"type\":\"invalidPose\",\"index\":%u,\"rejected\":%s,\"stateUnchanged\":%s}\n",
      unsigned(i), rejected ? "true" : "false", unchanged() ? "true" : "false");
  }
  // UI protection may be changed while paused. Exercise actual redraws from
  // the same uploaded scene, without introducing synthetic OS key events.
  if (okay && s_runtime_config["StereoUI"].ValueAs<bool>()) {
    const bool originalCinematic = s_runtime_config["StereoCinematicVignette"].ValueAs<bool>();
    const std::array<const char*, 3> names = { "cinema-off", "cinema-on", "cinema-restored" };
    const std::array<bool, 3> enabled = { false, true, originalCinematic };
    for (size_t i = 0; okay && i < names.size(); ++i) {
      s_runtime_config.Set("StereoCinematicVignette", enabled[i]);
      const bool accepted = model->RenderPresentation(poses[0]);
      okay = accepted && unchanged() && glGetError() == GL_NO_ERROR;
      fprintf(report, "{\"type\":\"cinematicToggle\",\"name\":\"%s\",\"enabled\":%s,\"accepted\":%s,\"stateUnchanged\":%s}\n",
        names[i], enabled[i] ? "true" : "false", accepted ? "true" : "false", unchanged() ? "true" : "false");
      if (okay) okay = CaptureDiagnosticFrame(prefix + "-" + names[i]);
      if (okay) EndFrameVideo();
    }
    s_runtime_config.Set("StereoCinematicVignette", originalCinematic);
    if (okay) okay = SameDiagnosticFile(prefix + "-identity.bmp", prefix + "-cinema-restored.bmp");
  }
  const bool saved = okay && SaveState(model, prefix + "-after.st0");
  const bool stateEqual = saved && SameDiagnosticFile(prefix + "-before.st0", prefix + "-after.st0");
  const bool imageEqual = okay && SameDiagnosticFile(prefix + "-identity.bmp", prefix + "-return.bmp") &&
    SameDiagnosticFile(prefix + "-identity.bmp", prefix + "-after-sweep.bmp");
  renderer->InvalidatePresentation();
  IRender3D::PresentationState invalidated;
  const bool staleRejected = !renderer->GetPresentationState(invalidated) && !model->RenderPresentation(poses[0]) &&
    glGetError() == GL_NO_ERROR;
  okay = okay && stateEqual && imageEqual && staleRejected;
  fprintf(report, "{\"type\":\"result\",\"passed\":%s,\"saveStateUnchanged\":%s,\"identityPixelsUnchanged\":%s,\"invalidatedSceneRejected\":%s}\n",
    okay ? "true" : "false", stateEqual ? "true" : "false", imageEqual ? "true" : "false", staleRejected ? "true" : "false");
  const bool writeOkay = std::ferror(report) == 0;
  const bool closeOkay = std::fclose(report) == 0;
  if (!okay || !writeOkay || !closeOkay) {
    ErrorLog("Presentation diagnostic failed; inspect '%s.jsonl'.", prefix.c_str());
    return false;
  }
  InfoLog("Presentation diagnostic passed: %u redraws, unchanged emulation/LOS/comfort state.", s_runtime_config["StereoUI"].ValueAs<bool>() ? 194u : 191u);
  return true;
}

/******************************************************************************
 Main Program Loop
******************************************************************************/

#ifdef SUPERMODEL_DEBUGGER
int Supermodel(const Game &game, ROMSet *rom_set, IEmulator *Model3, CInputs *Inputs, COutputs *Outputs, std::shared_ptr<Debugger::CDebugger> Debugger)
{
  std::shared_ptr<CLogger> oldLogger;
#else
int Supermodel(const Game &game, ROMSet *rom_set, IEmulator *Model3, CInputs *Inputs, COutputs *Outputs)
{
#endif // SUPERMODEL_DEBUGGER
  std::string initialState = s_runtime_config["InitStateFile"].ValueAs<std::string>();
  uint64_t    prevFPSTicks;
  unsigned    fpsFramesElapsed;
  bool        gameHasLightguns = false;
  bool        quit = false;
  bool        paused = false;
  bool        dumpTimings = false;
  const uint64_t runFrames = s_runtime_config["RunFrames"].ValueAs<unsigned>();
  const uint64_t captureFrame = s_runtime_config["CaptureFrame"].ValueAs<unsigned>();
  const std::string captureState = s_runtime_config["CaptureState"].ValueAs<std::string>();
  const bool cameraTrace = !s_runtime_config["StereoCameraTrace"].ValueAs<std::string>().empty();
  const bool frameProfile = !s_runtime_config["FrameProfile"].ValueAs<std::string>().empty();
  const bool independentPresentation = s_runtime_config["DesktopPresentHz"].ValueAs<double>() > 0;
  const bool headsetEnabled = s_runtime_config["OpenXR"].ValueAs<bool>();
  const bool bodyInteraction = s_runtime_config["BodyInteraction"].ValueAs<bool>();
  BodyInteractionMode::Controller bodyMode;
  const std::string headsetRenderTest = s_runtime_config["HeadsetRenderTest"].ValueAs<std::string>();
  const bool headsetTestEnabled = !headsetRenderTest.empty();
  bool headsetTestCompleted = !headsetTestEnabled;
  const std::string headsetWorkerPrefix = s_runtime_config["HeadsetWorkerTest"].ValueAs<std::string>();
  const bool headsetWorkerEnabled = !headsetWorkerPrefix.empty();
  HeadsetWorkerTest headsetWorkerTest;
  LiveFrameTrace::Capture liveFrameCapture; // Outlives the XR worker on every exit path.
  OpenXRSession headset;
  HeadsetPresentationPump<OpenXRSession> headsetPresentation(headset, paused);
  headsetPresentation.SetLateWorkerEnabled(s_runtime_config["OpenXRLateWorkerPresent"].ValueAs<bool>());
  HeadsetPresentBudget headsetBudget;
  HeadsetActiveTimings headsetActiveTimings;
  HeadsetFrameClock headsetClock;
  HeadsetIdleWait headsetIdleWait;
  bool headsetSuspended = false, headsetRecenterHeld = false;
  bool headsetNative = false;
  bool headsetMirror = s_runtime_config["OpenXRMirror"].ValueAs<bool>();
  uint64_t headsetNativeTicks = 0, headsetNativeSamples = 0, headsetNativeMax = 0;
  uint64_t headsetPpcMs = 0, headsetSyncMs = 0, headsetRenderMs = 0;
  uint64_t headsetCheckpointFrame = 0;
  HeadsetCheckpointBuffer headsetCheckpoints;
  // Keep file formatting and writes off the active frame-delivery path. Both
  // cleanup paths drain only after OpenXR has stopped; forced exits lose these
  // bounded records, while important errors still use the ordinary logger.
  const auto flushHeadsetCheckpoints = [&]() {
    if (!headsetEnabled) return;
    const size_t retained = headsetCheckpoints.Count();
    headsetCheckpoints.Drain([](const HeadsetCheckpointBuffer::Record& record) {
      InfoLog("OpenXR active checkpoint: frame %llu, seconds %.6f, native %llu, stereo %llu, segments %llu.",
        (unsigned long long)record.frame, record.activeSeconds,
        (unsigned long long)record.native, (unsigned long long)record.stereo,
        (unsigned long long)record.segments);
      InfoLog("OpenXR budget checkpoint: frame %llu, estimated_ms %.3f, samples %llu, spikes %llu, overruns %llu.",
        (unsigned long long)record.frame, record.estimatedMs,
        (unsigned long long)record.samples, (unsigned long long)record.spikes,
        (unsigned long long)record.overruns);
    });
    InfoLog("OpenXR checkpoint buffer: flushed %llu retained checkpoints after headset shutdown; %llu older or unavailable checkpoints dropped; capacity %llu. Records were deferred in memory during play; forced termination can lose them.",
      (unsigned long long)retained, (unsigned long long)headsetCheckpoints.Dropped(),
      (unsigned long long)HeadsetCheckpointBuffer::Capacity);
  };
  DesktopPresentationSession desktopPresentation;
  uint64_t previousPresentCount = 0, previousNativeCount = 0;
  const std::string presentationTest = s_runtime_config["PresentationTest"].ValueAs<std::string>();
  const uint64_t presentationFrame = s_runtime_config["PresentationTestFrame"].ValueAs<unsigned>();
  bool presentationCompleted = presentationTest.empty();
  const bool mouseLookTest = s_runtime_config["MouseLookTest"].ValueAs<bool>();
  const bool cinematicHotkey = s_runtime_config["MouseLook"].ValueAs<bool>() && s_runtime_config["StereoUI"].ValueAs<bool>() && runFrames == 0;
  CinematicToggleState cinematicToggle(s_runtime_config["StereoCinematicVignette"].ValueAs<bool>());
  const bool theaterHotkeys = s_runtime_config["StereoTheater"].ValueAs<bool>() && runFrames == 0;
  TheaterScreenControls theaterScreen(s_runtime_config["StereoTheaterScreenAngle"].ValueAs<float>());
  MouseLookCleanup mouseLookCleanup;
  const bool userMenuInteractive = runFrames == 0 && captureFrame == 0 &&
    !headsetTestEnabled && !headsetWorkerEnabled && presentationTest.empty() && !mouseLookTest &&
    !s_runtime_config["BodyRenderTest"].ValueAs<bool>() &&
    s_runtime_config["InputScript"].ValueAs<std::string>().empty();
  const bool userMenuEnabled = s_runtime_config["UserMenu"].ValueAsDefault<bool>(false) &&
    (game.name == "harley" || game.name == "harleyb") && s_runtime_config["New3DEngine"].ValueAs<bool>();
  bool menuStartPending = userMenuEnabled && userMenuInteractive && s_runtime_config["UserMenuStart"].ValueAsDefault<bool>(false);
  bool menuStartup = menuStartPending;
  HarleyCabinetFlow::Controller cabinetFlow;
  int menuRestartCode = 0;
  bool menuCalibrationPending = userMenuEnabled && userMenuInteractive &&
    s_runtime_config["UserMenuCalibrate"].ValueAsDefault<bool>(false);
  bool menuCalibrationPullback = false;
  s_userMenu.reset();
  if (userMenuEnabled) {
    // Load this narrow sidecar before OpenXR creates eye targets.
    // Game music remains controlled by explicit config/CLI or cabinet settings.
    // No original INI/NVRAM edits.
    s_userMenu=std::make_unique<HarleyMenuIntegration>(s_runtime_config,Inputs,
      FileSystemPath::GetPath(FileSystemPath::Config)+"HarleyVRMenu.ini",userMenuInteractive,headsetEnabled);
    theaterScreen=TheaterScreenControls(s_runtime_config["StereoTheaterScreenAngle"].ValueAs<float>());
    InfoLog("HarleyVR menu: click left stick, or hold left side grip and click right stick (Tab on desktop). PS/Home stays with SteamVR. A/Cross selects; B/Circle returns; Graphics has startup quality presets.");
  }
  if (s_runtime_config["MouseLook"].ValueAs<bool>())
    s_mouseLook = std::make_unique<MouseLookState>(s_runtime_config["MouseLookSensitivity"].ValueAs<double>());
  if (!s_cpuProfile.Open(s_runtime_config, game.name)) return 1;
  if (!desktopPresentation.Open(s_runtime_config)) return 1;
  s_emulatedFrame = 0;
  s_capturePending = s_captureCompleted = s_captureFailed = false;
  s_diagnosticGame = game.name;
  s_inputScriptNextEvent = 0;
  s_inputScriptHeld.clear();

  // Initialize and load ROMs
  if (Result::OKAY != Model3->Init())
    return 1;
  if (Model3->LoadGame(game, *rom_set) != Result::OKAY)
    return 1;
  if (s_userMenu) {
    const auto* nativeModel3 = dynamic_cast<const CModel3*>(Model3);
    s_userMenu->SetDreamRideSupported(nativeModel3 && nativeModel3->HarleyFreeRideSupported());
    const auto artworkRom=rom_set->get_rom("banked_crom");
    s_userMenu->LoadOriginalArtwork(artworkRom.data.get(),artworkRom.size);
  }
  *rom_set = ROMSet();  // free up this memory we won't need anymore

  // Customized music for games with MPEG boards
  MpegDec::LoadCustomTracks(s_musicXMLFilePath, game);

  // Load NVRAM
  LoadNVRAM(Model3);

  // Set the video mode
  char baseTitleStr[128];
  char titleStr[128];
  totalXRes = xRes = s_runtime_config["XResolution"].ValueAs<unsigned>();
  totalYRes = yRes = s_runtime_config["YResolution"].ValueAs<unsigned>();
  snprintf(baseTitleStr, sizeof(baseTitleStr), "Supermodel%s - %s",
           cinematicHotkey ? (cinematicToggle.Enabled() ? " Menus | RMB: look | C: force mask ON" : " Menus | RMB: look | C: force mask OFF") :
           s_mouseLook ? " Mouse Look | Hold RMB | Home: center" :
           s_runtime_config["StereoDiagnostic"].ValueAs<bool>() ? " Stereo Diagnostic (3D-only)" : "", game.title.c_str());
  if (theaterHotkeys)
    snprintf(baseTitleStr, sizeof(baseTitleStr), "Supermodel | RMB: look | Home: center | PgUp/Dn: screen %.0f deg - %s",
             theaterScreen.Angle(), game.title.c_str());
  SDL_SetWindowTitle(s_window, baseTitleStr);
  SDL_SetWindowSize(s_window, totalXRes, totalYRes);

  int xpos = s_runtime_config["WindowXPosition"].ValueAsDefault<int>(SDL_WINDOWPOS_CENTERED);
  int ypos = s_runtime_config["WindowYPosition"].ValueAsDefault<int>(SDL_WINDOWPOS_CENTERED);
  SDL_SetWindowPosition(s_window, xpos, ypos);

  if (s_runtime_config["BorderlessWindow"].ValueAs<bool>())
  {
    SDL_SetWindowBordered(s_window, SDL_FALSE);
  }

  SetFullScreenRefreshRate();

  bool stretch = s_runtime_config["Stretch"].ValueAs<bool>();
  bool fullscreen = s_runtime_config["FullScreen"].ValueAs<bool>();
  if (Result::OKAY != ResizeGLScreen(&xOffset, &yOffset ,&xRes, &yRes, &totalXRes, &totalYRes, !stretch, fullscreen))
    return 1;

  // Info log GL information
  PrintGLInfo(false, true, false);

  // Initialize audio system
  SetAudioType(game.audio);
  if (Result::OKAY != OpenAudio(s_runtime_config))
    return 1;

  // Hide mouse if fullscreen, enable crosshairs for gun games
  Inputs->GetInputSystem()->SetMouseVisibility(!s_runtime_config["FullScreen"].ValueAs<bool>());
  gameHasLightguns = !!(game.inputs & (Game::INPUT_GUN1|Game::INPUT_GUN2));
  gameHasLightguns |= game.name == "lostwsga";
  currentInputs = game.inputs;
  if (gameHasLightguns)
    videoInputs = Inputs;
  else
    videoInputs = nullptr;

  // Attach the inputs to the emulator
  Model3->AttachInputs(Inputs);

  // Attach the outputs to the emulator
  if (Outputs != nullptr)
    Model3->AttachOutputs(Outputs);

  // Frame timing
  s_perfCounterFrequency = SDL_GetPerformanceFrequency();
  uint64_t perfCountPerFrame = independentPresentation ?
    uint64_t(std::ceil(double(s_perfCounterFrequency) / DesktopPresentationClock::NativeHz)) :
    s_perfCounterFrequency * 1000 / GetDesiredRefreshRateMilliHz();
  uint64_t nextTime = 0;

  // Initialize the renderers
  SuperAA* superAA = new SuperAA(aaValue, CRTcolors);
  superAA->Init(totalXRes, totalYRes);  // pass actual frame sizes here
  CRender2D *Render2D = new CRender2D(s_runtime_config);
#ifndef SUPERMODEL_OSX
  IRender3D *Render3D = s_runtime_config["New3DEngine"].ValueAs<bool>() ? ((IRender3D *) new New3D::CNew3D(s_runtime_config, Model3->GetGame().name)) : ((IRender3D *) new Legacy3D::CLegacy3D(s_runtime_config));
#else
  // Legacy renderer is not supported on Mac, always use new engine
  IRender3D *Render3D = (IRender3D *) new New3D::CNew3D(s_runtime_config, Model3->GetGame().name);
#endif

  UpscaleMode upscaleMode = (UpscaleMode)s_runtime_config["UpscaleMode"].ValueAs<int>();

  if (Result::OKAY != Render2D->Init(xOffset*aaValue, yOffset*aaValue, xRes*aaValue, yRes*aaValue, totalXRes*aaValue, totalYRes*aaValue, superAA->GetTargetID(), upscaleMode))
    goto QuitError;
  if (Result::OKAY != Render3D->Init(xOffset*aaValue, yOffset*aaValue, xRes*aaValue, yRes*aaValue, totalXRes*aaValue, totalYRes*aaValue, superAA->GetTargetID()))
    goto QuitError;

  if (headsetEnabled || headsetTestEnabled || headsetWorkerEnabled) Render3D->SetHeadsetPresentation(true);
  if (headsetEnabled) {
    if (!headsetCheckpoints.Initialize())
      ErrorLog("OpenXR checkpoint buffer allocation failed; gameplay continues without periodic checkpoints. Final session summaries remain available.");
    else
      InfoLog("OpenXR compact checkpoints buffered in fixed memory before gameplay; written after headset shutdown. Forced termination can lose buffered checkpoints.");
    if (!liveFrameCapture.Start(true)) {
      ErrorLog("Cannot initialize requested live frame capture; refusing an uncaptured test.");
      goto QuitError;
    }
    if (LiveFrameTrace::Enabled()) InfoLog("Live frame capture enabled: bounded memory buffer, flushed after OpenXR shutdown.");
    if (!headset.Initialize(s_runtime_config["OpenXRResolutionScale"].ValueAs<float>(), s_runtime_config["OpenXRUnitsPerMeter"].ValueAs<float>(),
        s_runtime_config["OpenXRArraySwapchain"].ValueAs<bool>(),bodyInteraction,
        s_runtime_config["OpenXRReadyWake"].ValueAs<bool>(), s_runtime_config["OpenXRVisibilityMask"].ValueAs<bool>()))
      goto QuitError;
    // Allocate fixed eye resources before active frame delivery. This does not
    // advance emulation, acquire runtime images, or submit a hidden frame.
    {
      int targetWidth = 0, targetHeight = 0;
      auto* headsetRenderer = dynamic_cast<New3D::CNew3D*>(Render3D);
      if (!headsetRenderer || !headset.GetRenderTargetSize(targetWidth, targetHeight) ||
          !headsetRenderer->PrepareHeadsetTargets(targetWidth, targetHeight)) {
        ErrorLog("Unable to prepare the OpenXR renderer's eye targets before gameplay.");
        goto QuitError;
      }
      InfoLog("OpenXR renderer eye resources prepared before native emulation (%dx%d).", targetWidth, targetHeight);
    }
    if (s_userMenu) {
      int eyeWidth=0, eyeHeight=0;
      if (headset.GetRenderTargetSize(eyeWidth,eyeHeight))
        s_userMenu->SetCurrentEyeSize(unsigned(eyeWidth),unsigned(eyeHeight));
    }
    InfoLog("OpenXR native-deadline idle timer: %s.", headsetIdleWait.Initialize() ?
      "high-resolution bounded wait" : "deadline-aware SDL fallback");
    s_headsetOwnsVideo = true;
    if (headsetMirror && SDL_GL_GetSwapInterval() != 0) {
      headsetMirror = false;
      InfoLog("OpenXR desktop mirror disabled because monitor swaps are synchronized; headset submission retains runtime pacing.");
    }
    snprintf(baseTitleStr, sizeof(baseTitleStr), "HarleyVR | L3 or left grip + R3: menu | R3: center - %s", game.title.c_str());
    SDL_SetWindowTitle(s_window, baseTitleStr);
  }

  Model3->AttachRenderers(Render2D,Render3D, superAA);
  if (s_userMenu) s_userMenu->SetRenderer(Render3D);
  if (headsetWorkerEnabled) {
    s_headsetOwnsVideo = true; // diagnostic never waits on monitor swaps
    if (!headsetWorkerTest.Initialize(Render3D, s_runtime_config["HeadsetWorkerEnabled"].ValueAs<bool>())) goto QuitError;
  }

  // Reset emulator
  Model3->Reset();

  // Load initial save state if requested
  if (!initialState.empty())
  {
    if (!LoadState(Model3, initialState))
    {
      CloseAudio();
      goto QuitError;
    }
  }

#ifdef SUPERMODEL_DEBUGGER
  // If debugger was supplied, set it as logger and attach it to system
  oldLogger = GetLogger();
  if (Debugger != NULL)
  {
    SetLogger(Debugger);
    Debugger->Attach();
  }
#endif // SUPERMODEL_DEBUGGER

  // Emulate!
  desktopPresentation.Start(Model3, Render3D);
  if (headsetWorkerEnabled) headsetWorkerTest.Install(Model3);
  if (headsetEnabled) {
    headsetPresentation.SetRenderer(Render3D);
    headsetPresentation.SetBudget(&headsetBudget);
    Model3->SetFrameWaitCallback(HeadsetPresentationPump<OpenXRSession>::WorkerWait, &headsetPresentation);
    InfoLog("OpenXR native-deadline scheduling: headset work can use idle time up to the next native update; overdue native updates retain priority. Physical performance remains subject to runtime validation.");
  }
  fpsFramesElapsed = 0;
  prevFPSTicks = SDL_GetPerformanceCounter();
  if (independentPresentation) nextTime = prevFPSTicks + perfCountPerFrame;
  quit = false;
  paused = false;
  dumpTimings = false;
#ifdef DEBUG
  if (dynamic_cast<CModel3GraphicsState *>(Model3))
  {
    TestPolygonHeaderBits(Model3);
    quit = true;
  }
#endif
  while (!quit)
  {
    LiveFrameTrace::Scope traceLoop("main.loop", s_emulatedFrame);
    if (headsetEnabled) headset.SetHapticsPaused(paused);
    // Poll the inputs
    const auto traceInputStart = LiveFrameTrace::Now();
    if (!Inputs->Poll(&game, xOffset, yOffset, xRes, yRes, !headsetEnabled))
    {
      ReleaseMouseLook();
      break;
    }
    UpdateMouseLook(mouseLookTest, s_emulatedFrame + 1);
    LiveFrameTrace::Event("main.inputs", traceInputStart, LiveFrameTrace::Now(), s_emulatedFrame);
    bool menuHandledThisLoop=false;
    bool menuCabinetCompleted=false;
    auto setMenuPaused = [&](bool value) {
      paused=value;
      if (headsetEnabled) {
        headsetPresentation.Controls().SetPaused(value); headset.SetHapticsPaused(value); headsetClock.Reset();
        if (value) headsetSuspended=true;
      }
      if (value) { Model3->PauseThreads(); SetAudioEnabled(false); }
      else if (!headsetEnabled) { Model3->ResumeThreads(); SetAudioEnabled(true); }
      if (Outputs) Outputs->SetValue(OutputPause,value);
    };
    auto closeUserMenu = [&](bool leaveStartup = true, bool calibrate = false) {
      if (!s_userMenu || !s_userMenu->IsOpen()) return;
      // Returning to the cabinet is not Enter game and must not apply a
      // pending startup restart while TEST/SERVICE controls are still mapped.
      if (s_userMenu->ArcadeControls()) leaveStartup=false;
      // Only the dedicated launcher opts into this bounded restart protocol.
      // No driving session is restarted, and failed preference writes leave
      // the settings screen open rather than silently losing the selection.
      if (leaveStartup && menuStartup && s_userMenu->PendingRestart() &&
          s_runtime_config["UserMenuRestart"].ValueAsDefault<bool>(false)) {
        if (!s_userMenu->FlushPreferences()) return;
        StopMenuSoundPreview(); menuRestartCode=s_userMenu->StartupRestartCode(calibrate); quit=true;
        menuHandledThisLoop=true;
        InfoLog("HarleyVR startup settings saved; requesting one launcher restart before entering the game.");
        return;
      }
      s_userMenu->Close();
      // Camera settings require rebuilding the immutable scene while workers
      // remain paused. This does not advance PPC, timers or game progression.
      if (s_userMenu->CommitPending()) Model3->RenderFrame();
      if (leaveStartup) menuStartup=false;
      setMenuPaused(false); menuHandledThisLoop=true;
    };
    auto toggleUserMenu = [&]() {
      if (!s_userMenu || bodyMode.Exploring()) return;
      menuHandledThisLoop=true;
      if (s_userMenu->IsOpen()) { closeUserMenu(); return; }
      IRender3D::PresentationState state;
      if (s_emulatedFrame==0 || (headsetEnabled && !Render3D->GetPresentationState(state))) {
        menuStartPending=true; return;
      }
      menuStartPending=false; setMenuPaused(true); ReleaseMouseLook();
      IRender3D::HapticState rider;
      const bool haveRider=Render3D->GetHapticState(rider);
      if (haveRider && rider.driving && !s_userMenu->ArcadeControls()) menuStartup=false;
      const bool liveCamera=haveRider && rider.driving && rider.anchorValid;
      s_userMenu->SetContext(menuStartup,liveCamera);
      s_userMenu->Open();
    };
    auto returnFromArcade = [&](bool restartCabinet) {
      if (!s_userMenu || !s_userMenu->ArcadeControls()) return;
      // Explicit restart and observed native EXIT share the same safe input
      // transition. Neither clears backup RAM, loads a save, or edits NVRAM.
      setMenuPaused(true);
      s_userMenu->SetArcade(false); cabinetFlow.Cancel();
      s_userMenu->Close(); menuCalibrationPending=false;
      Inputs->test[0]->value=Inputs->service[0]->value=0;
      Inputs->start[0]->value=Inputs->coin[0]->value=0;
      if (headsetEnabled) headsetPresentation.Controls().Suspend();
      menuStartup=true; menuHandledThisLoop=true;
      if (restartCabinet) {
        Model3->Reset(); Model3->RenderFrame();
#ifdef SUPERMODEL_DEBUGGER
        if (Debugger) Debugger->Reset();
#endif
        // Let boot produce a usable theater frame before reopening the panel.
        menuStartPending=true; setMenuPaused(false);
        InfoLog("HarleyVR: confirmed cabinet restart; returning to startup. Saved cabinet data and player save slots were preserved.");
      } else {
        // Native EXIT already committed its own settings. Restore the regular
        // mapping BEFORE another Cross press can re-enter TEST instead of Start.
        menuStartPending=false;
        s_userMenu->SetContext(true,false); s_userMenu->Open();
        if (!s_userMenu->Publish()) { s_captureFailed=true; quit=true; }
        InfoLog("HarleyVR: native cabinet EXIT detected; startup menu restored with regular game controls.");
      }
    };
    auto processUserMenu = [&](const OpenXRSession::Controls& controls, bool xrFocused) {
      if (!s_userMenu || !s_userMenu->IsOpen()) return;
      menuHandledThisLoop=true;
      const bool keyboard=SDL_GetKeyboardFocus()==s_window &&
        !(SDL_GetWindowFlags(s_window)&(SDL_WINDOW_MINIMIZED|SDL_WINDOW_HIDDEN));
      if (!(headsetEnabled?xrFocused:keyboard)) StopMenuSoundPreview();
      const auto* keys=SDL_GetKeyboardState(nullptr);
      HarleyMenu::Navigation navigation;
      navigation.up=(keyboard && keys[SDL_SCANCODE_UP]) || (xrFocused && controls.rightActive && controls.gearShiftUp);
      navigation.down=(keyboard && keys[SDL_SCANCODE_DOWN]) || (xrFocused && controls.rightActive && controls.gearShiftDown);
      navigation.left=(keyboard && keys[SDL_SCANCODE_LEFT]) || (xrFocused && controls.leftActive && controls.steering<-.55f);
      navigation.right=(keyboard && keys[SDL_SCANCODE_RIGHT]) || (xrFocused && controls.leftActive && controls.steering>.55f);
      navigation.accept=(keyboard && (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER])) || (xrFocused && controls.rightActive && controls.select);
      navigation.back=(keyboard && (keys[SDL_SCANCODE_BACKSPACE] || keys[SDL_SCANCODE_ESCAPE])) || (xrFocused && controls.rightActive && controls.music);
      const uint64_t now=uint64_t(double(SDL_GetPerformanceCounter())*1000000.0/double(s_perfCounterFrequency));
      const auto event=s_userMenu->Menu().Update(navigation,now,headsetEnabled?xrFocused:keyboard);
      using A=HarleyMenu::Action;
      switch(event.action) {
        case A::Resume: closeUserMenu(); break;
        case A::StartGame: s_userMenu->QueueStart(); closeUserMenu(); break;
        case A::CalibrateRide:
          menuCalibrationPending=true;
          menuCalibrationPullback=event.value>0;
          closeUserMenu(true,true);
          InfoLog("Seat setup: use the normal coin/start and rider-selection controls. The ride will pause at the first usable riding view for actual-bike camera adjustment.");
          break;
        case A::InsertCoin: s_userMenu->QueueCoin(); closeUserMenu(false); break;
        case A::EnterArcade:
          menuCalibrationPending=false; menuStartup=true;
          cabinetFlow.Begin(); s_userMenu->SetArcade(true); closeUserMenu(false); break;
        case A::ReturnStartup: returnFromArcade(true); break;
        case A::LeaveArcade: break; // Retired: dropping the mapping alone traps the user in TEST.
        case A::Recenter:
          if (headsetEnabled) headset.Recenter();
          else if (s_mouseLook) { s_mouseLook->Update(true,false,0,0,false); s_mouseLook->Update(true,false,0,0,true); }
          s_userMenu->Reanchor(); break;
        case A::Exit: s_userMenu->FlushPreferences(); quit=true; break;
        case A::None: break;
        default:
          s_userMenu->ApplySetting(event);
          if (event.action==A::TheaterAngle) theaterScreen=TheaterScreenControls(float(event.value));
          // Workers are paused here. Rebuild the frozen scene immediately so
          // camera adjustments can be judged through the clear preview panel,
          // without advancing the race, sound CPU, timers or collision state.
          if (s_userMenu->CommitPending()) Model3->RenderFrame();
          break;
      }
      if (s_userMenu->IsOpen() && !s_userMenu->Publish()) { s_captureFailed=true; quit=true; }
    };
    if (s_userMenu) {
      const bool focused=SDL_GetKeyboardFocus()==s_window &&
        !(SDL_GetWindowFlags(s_window)&(SDL_WINDOW_MINIMIZED|SDL_WINDOW_HIDDEN));
      const bool toggle=s_userMenu->TabPressed(focused,SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB]!=0) ||
        (focused && Inputs->uiPause->Pressed()) ||
        (focused && !s_userMenu->IsOpen() && Inputs->uiExit->Pressed());
      if (toggle || menuStartPending) toggleUserMenu();
    }
    auto processBody = [&](bool focused, const OpenXRSession::Controls& controls) {
      if (!bodyInteraction || (s_userMenu && s_userMenu->IsOpen())) return;
      IRender3D::BodyStatus status;
      Render3D->GetBodyStatus(status);
      const auto* keys=SDL_GetKeyboardState(nullptr);
      const bool keyboard=SDL_GetKeyboardFocus()==s_window;
      BodyInteractionMode::Sample sample;
      sample.nowMs=HeadsetPresentationPump<OpenXRSession>::NowMs(); sample.focused=focused;
      sample.exploreToggle=controls.exploreToggle || (keyboard&&keys[SDL_SCANCODE_F2]);
      sample.returnToSeat=keyboard&&keys[SDL_SCANCODE_F3];
      sample.moveX=controls.moveX; sample.moveY=controls.moveY; sample.turnX=controls.turnX;
      if (keyboard && bodyMode.Exploring()) {
        sample.moveX+=float(keys[SDL_SCANCODE_D]!=0)-float(keys[SDL_SCANCODE_A]!=0);
        sample.moveY+=float(keys[SDL_SCANCODE_W]!=0)-float(keys[SDL_SCANCODE_S]!=0);
        sample.turnX+=float(keys[SDL_SCANCODE_E]!=0)-float(keys[SDL_SCANCODE_Q]!=0);
      }
      sample.eligible=status.eligible; sample.paused=paused;
      sample.sceneEpoch=status.sceneEpoch; sample.sceneFrame=status.sceneFrame;
      const auto request=bodyMode.Update(sample);
      if (request.pauseChanged) {
        paused=request.pauseValue;
        if (headsetEnabled) { headsetPresentation.Controls().SetPaused(paused); headset.SetHapticsPaused(paused); headsetClock.Reset(); }
        else if (paused) { Model3->PauseThreads(); SetAudioEnabled(false); }
        else { Model3->ResumeThreads(); SetAudioEnabled(true); }
        if (Outputs) Outputs->SetValue(OutputPause,paused);
      }
      if (!Render3D->SetBodyMode(request.armsEnabled,request.exploring,s_runtime_config["OpenXRUnitsPerMeter"].ValueAs<float>())) {
        if (request.exploring) {
          bodyMode=BodyInteractionMode::Controller{}; paused=true;
          Render3D->SetBodyMode(true,false,s_runtime_config["OpenXRUnitsPerMeter"].ValueAs<float>());
          InfoLog("Inspection unavailable: no validated local ground/scene. Returned to seat and left paused; Options/Alt+P resumes.");
        }
      } else if (request.moveRightMeters || request.moveForwardMeters || request.snapYawDegrees) {
        // Continuous walking can reach a boundary every presentation. Log only
        // the transition into rejection, not hundreds of repeated messages.
        static bool bodyMoveBlocked=false;
        const bool blocked=!Render3D->MoveBody(request.moveRightMeters,request.moveForwardMeters,-request.snapYawDegrees);
        if (blocked && !bodyMoveBlocked)
          InfoLog("Walking stopped at the edge of known local ground or an obstruction.");
        bodyMoveBlocked=blocked;
      }
      Render3D->SetBodyGripButtons(controls.grabLeft,controls.grabRight,focused&&controls.active);
      Render3D->SetBodyBlackout(false);
      Render3D->SetBodyVignette(request.vignetteStrength);
      if (request.entered) InfoLog("Walking entered: game frozen. Left stick walks with a responsive vignette; right stick snap-turns 30 degrees without adding a mask. R3 returns to the seat; F3 emergency seat.");
      if (request.exited || request.cancelled) InfoLog("Inspection ended%s.",request.cancelled ? " after scene invalidation; game remains paused" : "");
    };
    if (headsetEnabled) {
      const double nowMs = HeadsetPresentationPump<OpenXRSession>::NowMs();
      const int64_t nowNs = int64_t(double(SDL_GetPerformanceCounter()) * 1.0e9 / double(s_perfCounterFrequency));
      const bool inactive = paused || !headsetPresentation.Controls().Active();
      const bool fitsBeforeNative = headsetBudget.CanPresentBeforeNative(nowMs, double(headsetClock.Remaining(nowNs)) / 1.0e6);
      // xrBeginFrame can block on this OpenGL thread. A ready xrWaitFrame is
      // not permission to spend the next native update's time on extra draws.
      if (inactive || nowMs - headsetPresentation.LastFrameMs() >= 50.0 ||
          fitsBeforeNative)
        headsetPresentation.Pump();
      auto& latched = headsetPresentation.Controls();
      processBody(latched.LatestFrame().focused && latched.LatestFrame().tracked && latched.LatestFrame().render,latched.LatestControls());
      if (headsetPresentation.Failed() || headset.WantsExit()) {
        s_captureFailed |= headsetPresentation.Failed();
        break;
      }
      if (latched.TakePause() && !bodyMode.Exploring()) {
        if (s_userMenu) toggleUserMenu();
        else {
          paused = !paused; latched.SetPaused(paused); headset.SetHapticsPaused(paused); headsetClock.Reset();
          if (Outputs != nullptr) Outputs->SetValue(OutputPause, paused);
        }
      }
      if (s_userMenu) processUserMenu(latched.LatestControls(),latched.LatestFrame().focused && latched.LatestFrame().tracked && latched.LatestFrame().render);
      const bool recenter = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_HOME] != 0;
      if ((latched.TakeRecenter() || (recenter && !headsetRecenterHeld)) && !bodyMode.Exploring()) {
        headset.Recenter(); if (s_userMenu) s_userMenu->Reanchor();
      }
      headsetRecenterHeld = recenter;
      if (latched.ExitRequested()) quit = true;
      const bool active = !paused && !quit && latched.Active();
      headsetActiveTimings.Sample(SDL_GetPerformanceCounter(), active, headset.Presents(), s_emulatedFrame);
      headsetPresentation.ObserveActivity(active);
      if (headsetSuspended != !active) {
        headsetSuspended = !active;
        if (headsetSuspended) { Model3->PauseThreads(); SetAudioEnabled(false); }
        else { Model3->ResumeThreads(); SetAudioEnabled(true); }
      }
      // Native deadlines must not wait for the next headset display boundary.
      // Between RunFrame calls the owner polls briefly; inside RunFrame its
      // existing worker-wait seam renders headset frames without stepping time.
      const int64_t nativeNow = int64_t(double(SDL_GetPerformanceCounter()) * 1.0e9 / double(s_perfCounterFrequency));
      headsetNative = headsetClock.Tick(nativeNow, active);
      traceLoop.a = active; traceLoop.b = headsetNative;
      traceLoop.c = int64_t(headsetClock.Remaining(nativeNow));
      traceLoop.d = int64_t(headsetBudget.EstimatedCostMs() * 1000.0);
      if (headsetNative) {
        // Keyboard analog ramps advance once per native update, independent
        // of how often the owner polls for a completed runtime timing wait.
        Inputs->PollGameInputs(&game);
        const auto controls = latched.ConsumeGame();
        if (controls.leftActive) Inputs->steering->value = uint16_t(std::lround(128.0f + std::clamp(controls.steering,-1.0f,1.0f)*127.0f));
        if (controls.rightActive) Inputs->accelerator->value = std::max(Inputs->accelerator->value, uint16_t(std::lround(std::clamp(controls.throttle,0.0f,1.0f)*255.0f)));
        if (controls.leftActive) Inputs->brake->value = std::max(Inputs->brake->value, uint16_t(std::lround(std::clamp(controls.brake,0.0f,1.0f)*255.0f)));
        const auto cabinet = HarleyCabinetInput::Map(s_userMenu && s_userMenu->ArcadeControls(),controls.coin,controls.select);
        if (cabinet.coin) Inputs->coin[0]->value = 1;
        if (cabinet.start) { Inputs->start[0]->value = 1; Inputs->accelerator->value = 255; }
        if (cabinet.service) Inputs->service[0]->value = 1;
        if (cabinet.test) Inputs->test[0]->value = 1;
        if (controls.view) Inputs->viewChange->value = 1;
        if (controls.music) Inputs->musicSelect->value = 1;
        if (controls.gearShiftUp) Inputs->gearShiftUp->value = 1;
        if (controls.gearShiftDown) Inputs->gearShiftDown->value = 1;
      }
    }
    if (!headsetEnabled) processBody(SDL_GetKeyboardFocus()==s_window,{});
    if (!headsetEnabled && s_userMenu) processUserMenu({},false);
    if (theaterHotkeys) {
      const bool focused = SDL_GetKeyboardFocus() == s_window &&
        !(SDL_GetWindowFlags(s_window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN));
      const Uint8* keys = SDL_GetKeyboardState(nullptr);
      if (theaterScreen.Update(focused, keys[SDL_SCANCODE_PAGEUP] != 0, keys[SDL_SCANCODE_PAGEDOWN] != 0)) {
        s_runtime_config.Set("StereoTheaterScreenAngle", theaterScreen.Angle());
        snprintf(baseTitleStr, sizeof(baseTitleStr), "Supermodel | RMB: look | Home: center | PgUp/Dn: screen %.0f deg - %s",
                 theaterScreen.Angle(), game.title.c_str());
        SDL_SetWindowTitle(s_window, baseTitleStr);
        InfoLog("Virtual screen width %.0f degrees; PageUp enlarges, PageDown shrinks (40..100).", theaterScreen.Angle());
      }
    }
    if (cinematicHotkey) {
      const bool focused = (SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
      const bool keyDown = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_C] != 0;
      if (cinematicToggle.Update(focused, keyDown)) {
        s_runtime_config.Set("StereoCinematicVignette", cinematicToggle.Enabled());
        snprintf(baseTitleStr, sizeof(baseTitleStr), "Supermodel Menus | RMB: look | C: force mask %s - %s",
                 cinematicToggle.Enabled() ? "ON" : "OFF", game.title.c_str());
        if (theaterHotkeys)
          snprintf(baseTitleStr, sizeof(baseTitleStr), "Supermodel | PgUp/Dn: screen %.0f deg | C: mask %s - %s",
                   theaterScreen.Angle(), cinematicToggle.Enabled() ? "ON" : "OFF", game.title.c_str());
        SDL_SetWindowTitle(s_window, baseTitleStr);
        InfoLog("Desktop cinematic vignette %s by C key; driving comfort unchanged.", cinematicToggle.Enabled() ? "enabled" : "disabled");
      }
    }
    if (s_mouseLook && !Render3D->SetPresentationPose(s_mouseLook->Pose())) {
      ErrorLog("Renderer rejected the mouse-look pose.");
      s_captureFailed = true;
      break;
    }

    // Render if paused, otherwise run a frame
    if (paused && !headsetEnabled) {
      if (independentPresentation) desktopPresentation.Pump("paused");
      else if (s_mouseLook && Model3->RenderPresentation(s_mouseLook->Pose()))
        EndFrameVideo();
      else Model3->RenderFrame();
    }
    else if (!paused && (!headsetEnabled || headsetNative))
    {
      if (s_userMenu) s_userMenu->FilterAndPulse(headsetEnabled?headsetPresentation.Controls().LatestControls():OpenXRSession::Controls{},SDL_GetKeyboardFocus()==s_window);
      // A player may insert a coin, close startup setup and start via the
      // original arcade controls. Those paths must lose restart permission too.
      if (s_userMenu && !s_userMenu->ArcadeControls() &&
          (Inputs->start[0]->value || Inputs->accelerator->value)) menuStartup=false;
      s_emulatedFrame++;
      ApplyDiagnosticInputs(Inputs, game, unsigned(s_emulatedFrame));
      // Submit intent before the GPU snapshot for this frame is presented.
      // Semantic game inputs include keyboard, controllers and diagnostic scripts.
      Render3D->SubmitMenuInput(Inputs->steering->value, (*Inputs)["Start1"]->value != 0);
      if (cameraTrace)
        s_runtime_config.Set("StereoCameraTraceFrameId", unsigned(s_emulatedFrame));
      s_capturePending = captureFrame != 0 && s_emulatedFrame == captureFrame;
      if (s_capturePending && !captureState.empty())
      {
        // Saving before this frame permits a replay with exactly one RunFrame.
        if (!SaveState(Model3, captureState))
        {
          s_captureFailed = true;
          break;
        }
        InfoLog("Diagnostic snapshot is immediately before emulated frame %llu.", (unsigned long long)s_emulatedFrame);
      }
      if (frameProfile) {
        s_runtime_config.Set("FrameProfileFrameId", unsigned(s_emulatedFrame));
        s_cpuProfile.Begin(s_emulatedFrame);
      }
      const uint64_t nativeFrameStart = (independentPresentation || headsetEnabled || headsetWorkerEnabled) ? SDL_GetPerformanceCounter() : 0;
      if (independentPresentation) desktopPresentation.BeginNativeFrame(nativeFrameStart);
      if (headsetEnabled) {
        const double beganMs = HeadsetPresentationPump<OpenXRSession>::NowMs();
        // The worker may finish before the next game update is due. Use that
        // real deadline, not its predicted completion time, so a stereo draw
        // can finish in otherwise idle time without delaying the next update.
        // Sample this clock after beganMs: a preemption between clocks must
        // shorten the available window, never move its deadline into the future.
        const int64_t beganNs = int64_t(double(SDL_GetPerformanceCounter()) * 1.0e9 / double(s_perfCounterFrequency));
        const double remainingMs = double(headsetClock.Remaining(beganNs)) / 1.0e6;
        headsetBudget.BeginNative(beganMs, std::max(0.0, remainingMs - .25));
        headsetPresentation.BeginNative(beganMs);
      }
      const auto traceNativeStart = LiveFrameTrace::Now();
      Model3->RunFrame();
      // RunFrame has joined the PPC worker. Check only during a requested
      // cabinet session; no RAM inspection or extra work enters ordinary riding.
      if (s_userMenu && s_userMenu->ArcadeControls() && cabinetFlow.Active()) {
        if (auto* model=dynamic_cast<CModel3*>(Model3)) {
          const auto sample=HarleyCabinetFlow::ObserveRevisionB(game.name == "harley" || game.name == "harleyb",
            [model](uint32_t address) { return model->Read32(address); });
          menuCabinetCompleted=cabinetFlow.Observe(sample);
        }
      }
      const auto traceNativeEnd = LiveFrameTrace::Now();
      if (headsetEnabled) headset.ObserveNativeHaptics(Render3D);
      if (headsetEnabled) { headsetBudget.EndNative(); headsetPresentation.EndNative(); }
      if (headsetWorkerEnabled) {
        headsetWorkerTest.ObserveNativeFrame();
        if (headsetWorkerTest.Failed()) s_captureFailed = true;
      }
      if (headsetEnabled || headsetWorkerEnabled) {
        const uint64_t elapsed = SDL_GetPerformanceCounter() - nativeFrameStart;
        headsetNativeTicks += elapsed; ++headsetNativeSamples;
        headsetNativeMax = std::max(headsetNativeMax, elapsed);
        if (auto* model = dynamic_cast<CModel3*>(Model3)) {
          const auto timings = model->GetTimings();
          headsetPpcMs += timings.ppcTicks; headsetSyncMs += timings.syncTicks; headsetRenderMs += timings.renderTicks;
          if (headsetEnabled) headsetBudget.RecordNativeWork(HeadsetPresentationPump<OpenXRSession>::NowMs(), timings.ppcTicks);
          LiveFrameTrace::Event("game.native", traceNativeStart, traceNativeEnd, s_emulatedFrame,
            timings.ppcTicks, timings.syncTicks, timings.renderTicks, headsetBudget.NativePresents());
          const auto& traceAdmission = headsetBudget.Admissions();
          LiveFrameTrace::Event("budget.worker", traceNativeEnd, traceNativeEnd, s_emulatedFrame,
            traceAdmission.nativeAllowed, traceAdmission.nativeDeniedCap, traceAdmission.nativeDeniedRemaining, traceAdmission.nativeDeniedInvalid);
          LiveFrameTrace::Event("budget.outer", traceNativeEnd, traceNativeEnd, s_emulatedFrame,
            traceAdmission.outsideAllowed, traceAdmission.outsideDeniedDeadline, traceAdmission.outsideDeniedRemaining, traceAdmission.outsideDeniedInvalid);
          LiveFrameTrace::Event("budget.recovery", traceNativeEnd, traceNativeEnd, s_emulatedFrame,
            traceAdmission.nativePredictedAllowed, traceAdmission.nativeRescueAllowed, headsetBudget.RescuedPresents(), headsetBudget.WorkerOverruns());
          LiveFrameTrace::Event("budget.borrow", traceNativeEnd, traceNativeEnd, s_emulatedFrame,
            traceAdmission.nativeBorrowedAllowed, headsetBudget.BorrowedPresents(), headsetBudget.NativeOverruns(), 2000);
          const auto& tracePolls = headsetPresentation.Polls();
          LiveFrameTrace::Event("pump.counts", traceNativeEnd, traceNativeEnd, s_emulatedFrame,
            tracePolls.attempted, tracePolls.begun, tracePolls.pendingFocused, tracePolls.pendingOther);
        }
        // Requests sampled during the immutable redraws are applied only once
        // the native workers finish; never mutate emulator state in the callback.
        if (headsetPresentation.Controls().ExitRequested() || headset.WantsExit()) quit = true;
        if (headsetPresentation.Failed()) { s_captureFailed = true; quit = true; }
      }
      if (independentPresentation) {
        desktopPresentation.NativeFrame(nativeFrameStart, SDL_GetPerformanceCounter());
        if (desktopPresentation.Failed()) s_captureFailed = true;
      }
      if (frameProfile) s_cpuProfile.End();
      if (s_capturePending)
      {
        ErrorLog("Frame %llu finished without the diagnostic video callback.", (unsigned long long)s_emulatedFrame);
        s_captureFailed = true;
        s_capturePending = false;
      }
      if (!s_captureFailed && !presentationCompleted && s_emulatedFrame == presentationFrame) {
        presentationCompleted = RunPresentationDiagnostic(Model3, Render3D, presentationTest);
        if (!presentationCompleted) s_captureFailed = true;
      }
      if (!s_captureFailed && !headsetTestCompleted && s_emulatedFrame == s_runtime_config["HeadsetRenderTestFrame"].ValueAs<unsigned>()) {
        Model3->PauseThreads();
        headsetTestCompleted = s_runtime_config["BodyRenderTest"].ValueAs<bool>() ?
          RunBodyRenderDiagnostic(Render3D,headsetRenderTest,[Model3](const std::string& path) { return SaveState(Model3,path); }) :
          RunHeadsetRenderDiagnostic(Render3D, headsetRenderTest,
          [Model3](const std::string& path) { return SaveState(Model3, path); },
          s_runtime_config["HeadsetTestTheater"].ValueAs<bool>(),
          s_runtime_config["HeadsetTestPerformance"].ValueAs<bool>(),
          s_runtime_config["HeadsetTestCompact"].ValueAs<bool>(),
          s_runtime_config["OpenXRVisibilityMask"].ValueAs<bool>());
        Model3->ResumeThreads();
        if (!headsetTestCompleted) s_captureFailed = true;
        // The renderer diagnostic ends by invalidating its frozen scene to
        // verify rejection. RunFrames is a bound, not permission to resume
        // native emulation after that destructive diagnostic-only check.
        quit = true;
      }
      if (s_captureFailed || (runFrames != 0 && s_emulatedFrame >= runFrames))
        quit = true;
    }

    if (menuCabinetCompleted) returnFromArcade(false);

    // Seat fitting starts from real ROM-driven riding geometry, not a shipped
    // save state or substitute model. Stop as soon as the selected bike has a
    // validated riding anchor; emulation stays frozen throughout adjustment.
    if (menuCalibrationPending && !paused && s_userMenu) {
      IRender3D::HapticState rider;
      if (Render3D->GetHapticState(rider) && rider.driving && rider.anchorValid) {
        menuStartup=false;
        toggleUserMenu();
        if (s_userMenu->IsOpen()) {
          s_userMenu->Menu().ShowCameraPreview(menuCalibrationPullback);
          if (!s_userMenu->Publish()) { s_captureFailed=true; quit=true; }
          menuCalibrationPending=false;
          InfoLog("Seat setup ready: actual selected bike and frozen riding camera; adjust height/pullback, then resume.");
        }
      }
    }

#ifdef SUPERMODEL_DEBUGGER
    bool processUI = true;
    if (Debugger != NULL)
    {
      Debugger->Poll();

      // Check if debugger requests exit or pause
      if (Debugger->CheckExit())
      {
        quit = true;
        processUI = false;
      }
      else if (Debugger->CheckPause())
      {
        paused = true;
        processUI = false;
      }
    }
    if (processUI)
    {
#endif // SUPERMODEL_DEBUGGER

    if (headsetEnabled) {
      auto& latched = headsetPresentation.Controls();
      // Handle a controller pause independently of the keyboard UI else-if
      // chain, so a simultaneous save/reset cannot swallow that request.
      if (latched.TakePause() && !bodyMode.Exploring()) {
        if (s_userMenu) toggleUserMenu();
        else {
          paused = !paused; latched.SetPaused(paused); headset.SetHapticsPaused(paused); headsetClock.Reset();
          if (Outputs != nullptr) Outputs->SetValue(OutputPause, paused);
        }
      }
      const bool active = !paused && !quit && latched.Active();
      headsetActiveTimings.Sample(SDL_GetPerformanceCounter(), active, headset.Presents(), s_emulatedFrame);
      headsetPresentation.ObserveActivity(active);
      // Buffer compact checkpoints during real play as well as bounded tests.
      // They distinguish a delivery collapse from ordinary native progression.
      // This fresh activity boundary includes the just-completed native work;
      // remember the frame so idle owner-loop polls cannot repeat a checkpoint.
      if (s_emulatedFrame > 0 && s_emulatedFrame % 120 == 0 &&
          s_emulatedFrame != headsetCheckpointFrame) {
        headsetCheckpointFrame = s_emulatedFrame;
        headsetCheckpoints.Append({s_emulatedFrame,
          double(headsetActiveTimings.ticks) / double(s_perfCounterFrequency),
          headsetActiveTimings.native, headsetActiveTimings.stereo, headsetActiveTimings.segments,
          headsetBudget.EstimatedCostMs(), headsetBudget.Samples(), headsetBudget.Spikes(),
          headsetBudget.NativeOverruns()});
      }
      if (headsetSuspended != !active) {
        headsetSuspended = !active;
        if (headsetSuspended) { Model3->PauseThreads(); SetAudioEnabled(false); }
        else { Model3->ResumeThreads(); SetAudioEnabled(true); }
      }
    }
    // Menu saves/reset must not resume a headset session suspended by tracking
    // loss or the runtime dashboard.
    const bool resumeAfterUI = !paused && !(headsetEnabled && headsetSuspended);
    // Check UI controls
    if (s_userMenu && (s_userMenu->IsOpen() || menuHandledThisLoop))
    {
      // Menu navigation/close edges cannot also activate emulator hotkeys.
    }
    else if (Inputs->uiExit->Pressed())
    {
      // Quit emulator
      quit = true;
    }
    else if (Inputs->uiReset->Pressed())
    {
      if (resumeAfterUI)
      {
        Model3->PauseThreads();
        SetAudioEnabled(false);
      }

      // Reset emulator
      Model3->Reset();
      if (headsetEnabled) { headsetClock.Reset(); headsetPresentation.Controls().Suspend(); Model3->RenderFrame(); }

#ifdef SUPERMODEL_DEBUGGER
      // If debugger was supplied, reset it too
      if (Debugger != NULL)
        Debugger->Reset();
#endif // SUPERMODEL_DEBUGGER

      if (resumeAfterUI)
      {
        Model3->ResumeThreads();
        SetAudioEnabled(true);
      }

      puts("Model 3 reset.");
    }
    else if (!s_userMenu && Inputs->uiPause->Pressed() && !bodyMode.Exploring())
    {
      // Toggle emulator paused flag
      paused = !paused;
      if (headsetEnabled) headset.SetHapticsPaused(paused);

      if (paused)
      {
        if (!headsetEnabled) { Model3->PauseThreads(); SetAudioEnabled(false); }
        snprintf(titleStr, sizeof(titleStr), "%s (Paused)", baseTitleStr);
        SDL_SetWindowTitle(s_window, titleStr);
      }
      else
      {
        if (!headsetEnabled) { Model3->ResumeThreads(); SetAudioEnabled(true); }
        SDL_SetWindowTitle(s_window, baseTitleStr);
      }

      // Send paused value as output
      if (Outputs != NULL)
        Outputs->SetValue(OutputPause, paused);
    }
    else if (Inputs->uiFullScreen->Pressed())
    {
      if (headsetEnabled) {
        InfoLog("Fullscreen changes are disabled during an OpenXR session to preserve its graphics context.");
      } else {
      ReleaseMouseLook();
      if (s_userMenu) s_userMenu->ReleaseGraphics();
      // Toggle emulator fullscreen
      s_runtime_config.Get("FullScreen").SetValue(!s_runtime_config["FullScreen"].ValueAs<bool>());

      // Delete renderers and recreate them afterwards since GL context will most likely be lost when switching from/to fullscreen
      delete Render2D;
      Render2D = nullptr;

      // Resize screen
      totalXRes = xRes = s_runtime_config["XResolution"].ValueAs<unsigned>();
      totalYRes = yRes = s_runtime_config["YResolution"].ValueAs<unsigned>();
      bool stretchc = s_runtime_config["Stretch"].ValueAs<bool>();
      bool fullscreenc = s_runtime_config["FullScreen"].ValueAs<bool>();
      if (Result::OKAY != ResizeGLScreen(&xOffset,&yOffset,&xRes,&yRes,&totalXRes,&totalYRes,!stretchc,fullscreenc))
        goto QuitError;

      // Recreate renderers and attach to the emulator
      superAA->Init(totalXRes, totalYRes);
      Render2D = new CRender2D(s_runtime_config);

      if (Result::OKAY != Render2D->Init(xOffset * aaValue, yOffset * aaValue, xRes * aaValue, yRes * aaValue, totalXRes * aaValue, totalYRes * aaValue, superAA->GetTargetID(), upscaleMode))
        goto QuitError;
      if (Result::OKAY != Render3D->Init(xOffset * aaValue, yOffset * aaValue, xRes * aaValue, yRes * aaValue, totalXRes * aaValue, totalYRes * aaValue, superAA->GetTargetID()))
        goto QuitError;

      Model3->AttachRenderers(Render2D, Render3D, superAA);
      if (s_userMenu) s_userMenu->SetRenderer(Render3D);

      Inputs->GetInputSystem()->SetMouseVisibility(!s_runtime_config["FullScreen"].ValueAs<bool>());
      }
    }
    else if (Inputs->uiSaveState->Pressed())
    {
      if (resumeAfterUI)
      {
        Model3->PauseThreads();
        SetAudioEnabled(false);
      }

      // Save game state
      SaveState(Model3);

      if (resumeAfterUI)
      {
        Model3->ResumeThreads();
        SetAudioEnabled(true);
      }
    }
    else if (Inputs->uiChangeSlot->Pressed())
    {
      // Change save slot
      ++s_saveSlot;
      s_saveSlot %= 10; // clamp to [0,9]
      printf("Save slot: %d\n", s_saveSlot);
    }
    else if (Inputs->uiLoadState->Pressed())
    {
      if (resumeAfterUI)
      {
        Model3->PauseThreads();
        SetAudioEnabled(false);
      }

      // Load game state
      LoadState(Model3);
      if (headsetEnabled) { headsetClock.Reset(); headsetPresentation.Controls().Suspend(); Model3->RenderFrame(); }

#ifdef SUPERMODEL_DEBUGGER
      // If debugger was supplied, reset it after loading state
      if (Debugger != NULL)
        Debugger->Reset();
#endif // SUPERMODEL_DEBUGGER

      if (resumeAfterUI)
      {
        Model3->ResumeThreads();
        SetAudioEnabled(true);
      }
    }
    else if (Inputs->uiMusicVolUp->Pressed())
    {
      // Increase music volume by 10%
      if (!Model3->GetGame().mpeg_board.empty())
      {
        int vol = (std::min)(200, s_runtime_config["MusicVolume"].ValueAs<int>() + 10);
        s_runtime_config.Get("MusicVolume").SetValue(vol);
        printf("Music volume: %d%%", vol);
        if (200 == vol)
          puts(" (maximum)");
        else
          printf("\n");
      }
      else
        puts("This game does not have an MPEG music board.");
    }
    else if (Inputs->uiMusicVolDown->Pressed())
    {
      // Decrease music volume by 10%
      if (!Model3->GetGame().mpeg_board.empty())
      {
        int vol = (std::max)(0, s_runtime_config["MusicVolume"].ValueAs<int>() - 10);
        s_runtime_config.Get("MusicVolume").SetValue(vol);
        printf("Music volume: %d%%", vol);
        if (0 == vol)
          puts(" (muted)");
        else
          printf("\n");
      }
      else
        puts("This game does not have an MPEG music board.");
    }
    else if (Inputs->uiSoundVolUp->Pressed())
    {
      // Increase sound volume by 10%
    int vol = (std::min)(200, s_runtime_config["SoundVolume"].ValueAs<int>() + 10);
      s_runtime_config.Get("SoundVolume").SetValue(vol);
      printf("Sound volume: %d%%", vol);
      if (200 == vol)
        puts(" (maximum)");
      else
        printf("\n");
    }
    else if (Inputs->uiSoundVolDown->Pressed())
    {
      // Decrease sound volume by 10%
      int vol = (std::max)(0, s_runtime_config["SoundVolume"].ValueAs<int>() - 10);
      s_runtime_config.Get("SoundVolume").SetValue(vol);
      printf("Sound volume: %d%%", vol);
      if (0 == vol)
        puts(" (muted)");
      else
        printf("\n");
    }
#ifdef SUPERMODEL_DEBUGGER
    else if (Inputs->uiDumpInpState->Pressed())
    {
      // Dump input states
      Inputs->DumpState(&game);
    }
    else if (Inputs->uiDumpTimings->Pressed())
    {
      dumpTimings = !dumpTimings;
    }
#endif
    else if (Inputs->uiSelectCrosshairs->Pressed() && gameHasLightguns)
    {
      int crosshairs = (s_runtime_config["Crosshairs"].ValueAs<unsigned>() + 1) & 3;
      s_runtime_config.Get("Crosshairs").SetValue(crosshairs);
      switch (crosshairs)
      {
      case 0: puts("Crosshairs disabled.");             break;
      case 3: puts("Crosshairs enabled.");              break;
      case 1: puts("Showing Player 1 crosshair only."); break;
      case 2: puts("Showing Player 2 crosshair only."); break;
      }
    }
    else if (Inputs->uiClearNVRAM->Pressed())
    {
      // Clear NVRAM
      Model3->ClearNVRAM();
      puts("NVRAM cleared.");
    }
    else if (Inputs->uiToggleFrLimit->Pressed())
    {
      if (headsetEnabled)
        puts("OpenXR keeps the arcade simulation at its native 57.524Hz clock.");
      else if (independentPresentation)
        puts("Desktop presentation keeps the game at its native 57.524Hz clock.");
      else {
        s_runtime_config.Get("Throttle").SetValue(!s_runtime_config["Throttle"].ValueAs<bool>());
        printf("Frame limiting: %s\n", s_runtime_config["Throttle"].ValueAs<bool>() ? "On" : "Off");
      }
    }
    else if (Inputs->uiScreenshot->Pressed())
    {
      // Make a screenshot
      Screenshot();
    }
#ifdef SUPERMODEL_DEBUGGER
      else if (Debugger != NULL && Inputs->uiEnterDebugger->Pressed())
      {
        // Break execution and enter debugger
        Debugger->ForceBreak(true);
      }
    }
#endif // SUPERMODEL_DEBUGGER

    // Refresh rate (frame limiting)
    if (headsetEnabled) {
      headsetPresentation.Controls().SetPaused(paused);
      headset.SetHapticsPaused(paused || quit);
      const bool active = !paused && !quit && headsetPresentation.Controls().Active();
      headsetActiveTimings.Sample(SDL_GetPerformanceCounter(), active, headset.Presents(), s_emulatedFrame);
      headsetPresentation.ObserveActivity(active);
      if (headsetSuspended != !active) {
        headsetSuspended = !active;
        if (headsetSuspended) { Model3->PauseThreads(); SetAudioEnabled(false); }
        else { Model3->ResumeThreads(); SetAudioEnabled(true); }
      }
      const uint64_t presentedBefore = headset.Presents();
      const double nowMs = HeadsetPresentationPump<OpenXRSession>::NowMs();
      const int64_t nowNs = int64_t(double(SDL_GetPerformanceCounter()) * 1.0e9 / double(s_perfCounterFrequency));
      const bool fitsBeforeNative = headsetBudget.CanPresentBeforeNative(nowMs, double(headsetClock.Remaining(nowNs)) / 1.0e6);
      if (!quit && (!active || nowMs - headsetPresentation.LastFrameMs() >= 50.0 ||
          fitsBeforeNative))
        headsetPresentation.Pump(headsetMirror, int(totalXRes), int(totalYRes));
      if (headsetPresentation.Failed()) { s_captureFailed = true; quit = true; }
      if (headsetMirror && headset.Presents() != presentedBefore) SDL_GL_SwapWindow(s_window);
      if (!headsetNative && !quit) {
        // Pump may have crossed the native deadline. Re-read time after it,
        // and do not add another whole millisecond to an already-due update.
        const auto idleStart = LiveFrameTrace::Now();
        const uint64_t idleCounter = SDL_GetPerformanceCounter();
        const int64_t idleNow = int64_t(double(idleCounter) * 1.0e9 / double(s_perfCounterFrequency));
        const int64_t remaining = active ? headsetClock.Remaining(idleNow) : 1000000;
        // Wake only inside an admissible idle window. A ready frame that no
        // longer fits must not create a spin loop. Fresh admission still runs
        // on the next owner loop; this event never grants permission to draw.
        const double idleCostMs = headsetBudget.EstimatedCostMs();
        void* readySignal = active && remaining > 0 && std::isfinite(idleCostMs) &&
          idleCostMs > 0 && double(remaining) / 1.0e6 >= idleCostMs ?
          headset.TimingReadySignal() : nullptr;
        bool wokeReady = false;
        if (!active) SDL_Delay(1);
        else if (!headsetIdleWait.WaitFrom(idleCounter, s_perfCounterFrequency, remaining, readySignal, &wokeReady))
          SDL_Delay(!active || remaining >= 2000000 ? 1 : 0);
        LiveFrameTrace::Event("main.idle_wait", idleStart, LiveFrameTrace::Now(), s_emulatedFrame,
          remaining, HeadsetIdleWait::Duration(remaining), active, (readySignal ? 1 : 0) | (wokeReady ? 2 : 0));
      }
    }
    else if ((!quit || !independentPresentation) && (paused || s_runtime_config["Throttle"].ValueAs<bool>()))
    {
        SuperSleepUntil(nextTime);
        nextTime = SDL_GetPerformanceCounter() + perfCountPerFrame;
    }

    // Measure frame rate
    uint64_t currentFPSTicks = SDL_GetPerformanceCounter();
    if (s_runtime_config["ShowFrameRate"].ValueAs<bool>())
    {
      fpsFramesElapsed += 1;
      uint64_t measurementTicks = currentFPSTicks - prevFPSTicks;
      if (measurementTicks >= s_perfCounterFrequency) // update FPS every 1 second (s_perfCounterFrequency is how many perf ticks in one second)
      {
        double fps = double(fpsFramesElapsed) / (double(measurementTicks) / double(s_perfCounterFrequency));
        if (headsetEnabled) {
          const double seconds = double(measurementTicks) / double(s_perfCounterFrequency);
          snprintf(titleStr, sizeof(titleStr), "%1.1f XR / %1.1f game FPS%s | %.85s",
            double(headset.Presents() - previousPresentCount) / seconds,
            double(s_emulatedFrame - previousNativeCount) / seconds, (paused || headsetSuspended) ? " (Paused)" : "", baseTitleStr);
          previousPresentCount = headset.Presents(); previousNativeCount = s_emulatedFrame;
        }
        else if (independentPresentation) {
          const double seconds = double(measurementTicks) / double(s_perfCounterFrequency);
          snprintf(titleStr, sizeof(titleStr), "%1.1f present / %1.1f game FPS%s | %.82s",
            double(desktopPresentation.Presents() - previousPresentCount) / seconds,
            double(s_emulatedFrame - previousNativeCount) / seconds, paused ? " (Paused)" : desktopPresentation.Guarded() ? " (game-priority)" : "", baseTitleStr);
          previousPresentCount = desktopPresentation.Presents();
          previousNativeCount = s_emulatedFrame;
        }
        else snprintf(titleStr, sizeof(titleStr), "%s - %1.3f FPS%s", baseTitleStr, fps, paused ? " (Paused)" : "");
        SDL_SetWindowTitle(s_window, titleStr);
        prevFPSTicks = currentFPSTicks;   // reset tick count
        fpsFramesElapsed = 0;             // reset frame count
      }
    }

    if (dumpTimings && !paused)
    {
      CModel3 *M = dynamic_cast<CModel3 *>(Model3);
      if (M)
        M->DumpTimings();
    }
  }

  // Make sure all threads are paused before shutting down
  desktopPresentation.Stop();
  if (headsetEnabled || headsetWorkerEnabled) {
    Model3->SetFrameWaitCallback(nullptr, nullptr);
    headsetActiveTimings.Sample(SDL_GetPerformanceCounter(), false, headset.Presents(), s_emulatedFrame);
    const double activeSeconds = double(headsetActiveTimings.ticks) / double(s_perfCounterFrequency);
    InfoLog("OpenXR active timing: seconds %.6f, native %llu, stereo %llu, native_hz %.3f, stereo_hz %.3f, segments %llu.",
      activeSeconds, (unsigned long long)headsetActiveTimings.native, (unsigned long long)headsetActiveTimings.stereo,
      activeSeconds > 0 ? headsetActiveTimings.native / activeSeconds : 0,
      activeSeconds > 0 ? headsetActiveTimings.stereo / activeSeconds : 0,
      (unsigned long long)headsetActiveTimings.segments);
    InfoLog("OpenXR session finished: %llu submitted stereo frames, %llu native game frames.",
      (unsigned long long)headset.Presents(), (unsigned long long)s_emulatedFrame);
    const double samples = double(std::max<uint64_t>(1, headsetNativeSamples));
    InfoLog("OpenXR native work: %llu samples, mean %.3f ms, max %.3f ms; PPC %.3f ms, snapshot sync %.3f ms, native render %.3f ms; %llu stereo frames delivered during worker waits. Native wall time includes concurrent headset work; these averages are not additive.",
      (unsigned long long)headsetNativeSamples, 1000.0*headsetNativeTicks/double(s_perfCounterFrequency)/samples,
      1000.0*headsetNativeMax/double(s_perfCounterFrequency), headsetPpcMs/samples, headsetSyncMs/samples,
      headsetRenderMs/samples, (unsigned long long)headsetPresentation.WorkerPresents());
    const auto stats = headset.Stats();
    const auto motors = headset.HapticsStats();
    InfoLog("OpenXR haptics summary: native samples %llu, valid driving anchors %llu, output-allowed samples %llu, sequence discontinuities %llu; detected crash/landing pulses %llu, API-accepted pulses %llu, expired queued pulses %llu, stop events %llu; left attempts %llu accepted %llu, right attempts %llu accepted %llu. Every native scene is sampled independently of headset cadence. API acceptance is not physical vibration confirmation.",
      (unsigned long long)motors.nativeSamples, (unsigned long long)motors.validSamples,
      (unsigned long long)motors.allowedSamples, (unsigned long long)motors.discontinuities,
      (unsigned long long)motors.detectedPulses, (unsigned long long)motors.deliveredPulses,
      (unsigned long long)motors.expiredPulses, (unsigned long long)motors.stopEvents,
      (unsigned long long)motors.handAttempts[0], (unsigned long long)motors.handAccepted[0],
      (unsigned long long)motors.handAttempts[1], (unsigned long long)motors.handAccepted[1]);
    const double frames = double(std::max<uint64_t>(1, stats.frames));
    InfoLog("OpenXR delivery means: wait %.3f ms (timing thread, not owner CPU work), begin %.3f ms, swapchain wait %.3f ms, late pose %.3f ms, stereo render CPU %.3f ms, submit %.3f ms. Max wait %.3f / stereo CPU %.3f / submit %.3f ms; %llu eye pairs drawn. GPU/compositor timing still comes from the runtime.",
      stats.waitMs/double(std::max<uint64_t>(1, stats.waits)), stats.beginMs/frames,
      stats.swapchainWaitMs/frames, stats.lateLocateMs/frames,
      stats.eyeRenderMs/double(std::max<uint64_t>(1, stats.eyeDraws)), stats.endMs/frames,
      stats.maxWaitMs, stats.maxEyeRenderMs, stats.maxEndMs, (unsigned long long)stats.eyeDraws);
    InfoLog("OpenXR ready timing residence: %llu consumed worker results, mean %.3f / max %.3f ms after xrWaitFrame returned; %llu exceeded one predicted period; %llu invalid ages. Includes all focus states. This is owner-consumption delay, not measured display lateness or headset FPS.",
      (unsigned long long)stats.readyWaits, stats.readyWaitResidenceMs/double(std::max<uint64_t>(1, stats.readyWaits)),
      stats.maxReadyWaitResidenceMs, (unsigned long long)stats.readyWaitsOverPeriod,
      (unsigned long long)stats.invalidReadyWaitAges);
    const auto& cadence = headsetPresentation.Cadence();
    const auto& gaps = cadence.submissionGaps;
    InfoLog("OpenXR submission cadence: %llu active submissions, %llu intervals, period %.3f ms; gap mean %.3f / p50 %.3f / p95 %.3f / p99 %.3f / max %.3f ms; short %llu, long %llu, predicted skipped %llu, predicted nonincreasing %llu. CPU submission timing, not compositor display timing; short <0.5 period, long >1.5 periods; quantiles rounded up 0.25 ms and capped at 256 ms.",
      (unsigned long long)cadence.submitted, (unsigned long long)gaps.samples, cadence.periodMs,
      gaps.Mean(), gaps.Percentile(.50), gaps.Percentile(.95), gaps.Percentile(.99), gaps.maximum,
      (unsigned long long)cadence.shortGaps, (unsigned long long)cadence.longGaps,
      (unsigned long long)cadence.predictedSkipped, (unsigned long long)cadence.predictedNonIncreasing);
    const auto& nativeGaps = cadence.nativeGaps;
    const auto& phase = cadence.nativePhase;
    InfoLog("OpenXR native cadence: %llu intervals, mean %.3f / p95 %.3f / p99 %.3f / max %.3f ms; submissions inside %llu / outside %llu; inside-native completion offset mean %.3f / p95 %.3f / max %.3f ms.",
      (unsigned long long)nativeGaps.samples, nativeGaps.Mean(), nativeGaps.Percentile(.95), nativeGaps.Percentile(.99), nativeGaps.maximum,
      (unsigned long long)cadence.insideNative, (unsigned long long)cadence.outsideNative,
      phase.Mean(), phase.Percentile(.95), phase.maximum);
    const auto& admission = headsetBudget.Admissions();
    const auto& polls = headsetPresentation.Polls();
    InfoLog("OpenXR second-frame borrowing: probes %llu, begun frames %llu; 2.000 ms admission allowance against the original native deadline. No borrowing during spike backoff; native clock and two-frame cap unchanged.",
      (unsigned long long)admission.nativeBorrowedAllowed, (unsigned long long)headsetBudget.BorrowedPresents());
    InfoLog("OpenXR worker recovery: predicted-window probes %llu, rescue probes %llu, rescued begun frames %llu, worker-window overruns %llu. Native clock and ordinary two-frame cap retained; predictions do not prove worker completion or physical FPS.",
      (unsigned long long)admission.nativePredictedAllowed, (unsigned long long)admission.nativeRescueAllowed,
      (unsigned long long)headsetBudget.RescuedPresents(), (unsigned long long)headsetBudget.WorkerOverruns());
    InfoLog("OpenXR admission probes: worker allowed %llu, denied cap %llu / invalid-or-outside %llu / remaining-time %llu; outer allowed %llu, denied deadline %llu / remaining-time %llu / invalid-or-inside %llu. Runtime polls attempted %llu, begun %llu, failed %llu, no-frame focused %llu / other %llu. Probe counts include inactive sessions and freshness overrides; these are attempts, not unique missed displays or ready frames.",
      (unsigned long long)admission.nativeAllowed, (unsigned long long)admission.nativeDeniedCap,
      (unsigned long long)admission.nativeDeniedInvalid, (unsigned long long)admission.nativeDeniedRemaining,
      (unsigned long long)admission.outsideAllowed, (unsigned long long)admission.outsideDeniedDeadline,
      (unsigned long long)admission.outsideDeniedRemaining, (unsigned long long)admission.outsideDeniedInvalid,
      (unsigned long long)polls.attempted, (unsigned long long)polls.begun, (unsigned long long)polls.failed,
      (unsigned long long)polls.pendingFocused, (unsigned long long)polls.pendingOther);
  }
  headset.Close();
  flushHeadsetCheckpoints();
  if (!liveFrameCapture.Close()) { ErrorLog("Live frame capture flush failed."); s_captureFailed = true; }
  s_headsetOwnsVideo = false;
  ReleaseMouseLook();
  Model3->PauseThreads();
  if (headsetWorkerEnabled) {
    headsetWorkerTest.Detach(Model3);
    s_captureFailed |= !headsetWorkerTest.Finish(headsetWorkerPrefix,
      [Model3](const std::string& path) { return SaveState(Model3, path); });
    headsetWorkerTest.Close();
  }

#ifdef SUPERMODEL_DEBUGGER
  // If debugger was supplied, detach it from system and restore old logger
  if (Debugger != NULL)
  {
    Debugger->Detach();
    SetLogger(oldLogger);
  }
#endif // SUPERMODEL_DEBUGGER

  // Save NVRAM
  SaveNVRAM(Model3);

  // Close audio
  CloseAudio();

  // Shut down renderers
  if (s_userMenu) { s_userMenu->FlushPreferences(); s_userMenu->ReleaseGraphics(); s_userMenu.reset(); }
  delete Render2D;
  delete Render3D;
  delete superAA;

  s_captureFailed |= !s_cpuProfile.Close();
  s_captureFailed |= !desktopPresentation.Close();
  if (runFrames != 0)
    InfoLog("Bounded run finished after %llu emulated frames.", (unsigned long long)s_emulatedFrame);
  if (captureFrame != 0 && !s_captureCompleted)
  {
    ErrorLog("Requested diagnostic capture at frame %llu was not completed.", (unsigned long long)captureFrame);
    return 1;
  }
  if (!presentationCompleted) {
    ErrorLog("Requested presentation diagnostic did not complete.");
    return 1;
  }
  if (!headsetTestCompleted) { ErrorLog("Headset rendering diagnostic did not complete."); return 1; }
  return s_captureFailed ? 1 : menuRestartCode;

  // Quit with an error
QuitError:
  if (headsetWorkerEnabled) { headsetWorkerTest.Detach(Model3); headsetWorkerTest.Close(); }
  if (headsetEnabled) Model3->SetFrameWaitCallback(nullptr, nullptr);
  desktopPresentation.Stop();
  headset.Close();
  flushHeadsetCheckpoints();
  if (!liveFrameCapture.Close()) ErrorLog("Live frame capture flush failed during error cleanup.");
  s_headsetOwnsVideo = false;
  Model3->PauseThreads();
  CloseAudio();
  ReleaseMouseLook();
  s_cpuProfile.Close();
  if (s_userMenu) { s_userMenu->FlushPreferences(); s_userMenu->ReleaseGraphics(); s_userMenu.reset(); }
  delete Render2D;
  delete Render3D;
  delete superAA;

  return 1;
}


/******************************************************************************
 Entry Point and Command Line Processing
******************************************************************************/

// Configuration file is generated whenever it is not present. We no longer
// distribute it with Supermodel to make it easier to upgrade Supermodel from
// .zip archives without accidentally overwriting the configuration.
static void WriteDefaultConfigurationFileIfNotPresent()
{
    // Test whether file exists by opening it
    FILE* fp = fopen(s_configFilePath.c_str(), "r");
    if (fp)
    {
        fclose(fp);
        return;
    }

    // Write config
    fp = fopen(s_configFilePath.c_str(), "w");
    if (!fp)
    {
        ErrorLog("Unable to write default configuration file to %s", s_configFilePath.c_str());
        return;
    }
    fputs(DefaultConfigFileContents().c_str(), fp);
    fclose(fp);
    InfoLog("Wrote default configuration file to %s", s_configFilePath.c_str());
}

// Create and configure inputs
static Result ConfigureInputs(CInputs *Inputs, Util::Config::Node *fileConfig, Util::Config::Node *runtimeConfig, const Game &game, bool configure)
{
  Inputs->LoadFromConfig(*runtimeConfig);

  // If the user wants to configure the inputs, do that now
  if (configure)
  {
    std::string title("Supermodel - ");
    if (game.name.empty())
      title.append("Configuring Default Inputs...");
    else
      title.append(Util::Format() << "Configuring Inputs for: " << game.title);
    SDL_SetWindowTitle(s_window, title.c_str());

    // Extract the relevant INI section (which will be the global section if no
    // game was specified, otherwise the game's node) in the file config, which
    // will be written back to disk
    Util::Config::Node *fileConfigRoot = game.name.empty() ? fileConfig : fileConfig->TryGet(game.name);
    if (fileConfigRoot == nullptr)
    {
      fileConfigRoot = &fileConfig->Add(game.name);
    }

    // Configure the inputs
    if (Inputs->ConfigureInputs(game, xOffset, yOffset, xRes, yRes))
    {
      // Write input configuration and input system settings to config file
      Inputs->StoreToConfig(fileConfigRoot);
      Util::Config::WriteINIFile(s_configFilePath, *fileConfig, TimestampedConfigFileComment("Updated from input configuration."));

      // Also save to runtime configuration in case we proceed and play
      Inputs->StoreToConfig(runtimeConfig);
    }
    else
      puts("Configuration aborted...");
    puts("");
  }

  return Result::OKAY;
}

// Print game list
static void PrintGameList(const std::string &xml_file, const std::map<std::string, Game> &games)
{
  if (games.empty())
  {
    puts("No games defined.");
    return;
  }
  printf("Games defined in %s:\n", xml_file.c_str());
  puts("");
  puts("    ROM Set          Title");
  puts("    -------          -----");
  for (auto &v: games)
  {
    const Game &game = v.second;
    printf("    %s", game.name.c_str());
    for (size_t i = game.name.length(); i < 10; i++)  // pad for alignment
      printf(" ");
    if (!game.version.empty())
      printf("       %s (%s)\n", game.title.c_str(), game.version.c_str());
    else
      printf("       %s\n", game.title.c_str());
  }
}

static void LogConfig(const Util::Config::Node &config)
{
  InfoLog("Runtime configuration:");
  for (auto &child: config)
  {
    if (child.Empty())
      InfoLog("  %s=<empty>", child.Key().c_str());
    else
      InfoLog("  %s=%s", child.Key().c_str(), child.ValueAs<std::string>().c_str());
  }
  InfoLog("");
}

Util::Config::Node DefaultConfig()
{
  Util::Config::Node config("Global");
  
  config.Set("GameXMLFile", s_gameXMLFilePath);
  config.Set("InitStateFile", "");
  config.Set("RunFrames", 0u);
  config.Set("DiagnosticHidden", false);
  config.Set("CaptureFrame", 0u);
  config.Set("CapturePrefix", "Diagnostic");
  config.Set("CaptureState", "");
  config.Set("PresentationTest", "");
  config.Set("PresentationTestFrame", 1u);
  config.Set("MouseLook", false);
  config.Set("MouseLookTest", false);
  config.Set("MouseLookSensitivity", 0.12f);
  config.Set("StereoMonoPreview", false);
  config.Set("InputScript", "");
  config.Set("FrameProfile", "");
  config.Set("FrameProfileStart", 121u);
  config.Set("FrameProfileFrames", 600u);
  config.Set("FrameProfileFrameId", 0u);
  config.Set("ProfileThreaded", false);
  config.Set("DesktopPresentHz", 0.0f);
  config.Set("PresentationProfile", "");
  config.Set("OpenXR", false, "Video");
  config.Set("OpenXRMirror", false, "Video");
  config.Set("OpenXRResolutionScale", 0.75f, "Video", 0.5f, 1.5f);
  config.Set("OpenXRArraySwapchain", false);
  config.Set("OpenXRLateWorkerPresent", false);
  config.Set("OpenXRPrePreparePresent", false);
  config.Set("OpenXRReadyWake", false);
  config.Set("OpenXRVisibilityMask", false);
  config.Set("HeadsetSceneInterpolation", false);
  config.Set("HeadsetOwnedHUDState", false);
  config.Set("HeadsetHUDFlush", false);
  config.Set("BodyInteraction", false);
  config.Set("BodyRenderTest", false);
  config.Set("OpenXRUnitsPerMeter", 16.0f, "Video", 1.0f, 100.0f);
  config.Set("HeadsetRenderTest", "");
  config.Set("HeadsetRenderTestFrame", 1u);
  config.Set("HeadsetWorkerTest", "");
  config.Set("HeadsetWorkerEnabled", false);
  config.Set("HeadsetTestTheater", false);
  config.Set("HeadsetTestPerformance", false);
  config.Set("HeadsetTestCompact", false);
  // CModel3
  config.Set("PowerPCFrequency", 0u, "Core", 0u, 200u);
  config.Set("MultiThreaded", true,"Core");
  config.Set("GPUMultiThreaded", true, "Core");
  config.Set("IdleSkip", false, "Core"); // isolated optimization remains opt-in pending differential validation
  // 2D and 3D graphics engines
#ifndef SUPERMODEL_OSX
  config.Set("MultiTexture", false, "Legacy3D");
  config.Set<std::string>("VertexShader", "", "Legacy3D", "", "");
  config.Set<std::string>("FragmentShader", "", "Legacy3D", "", "");
#endif
  
  // CSoundBoard
  config.Set("EmulateSound", true, "Sound");
  config.Set("Balance", 0.0f, "Sound", -100.f, 100.f);
  config.Set("BalanceLeftRight", 0.0f, "Sound", -100.f, 100.f);
  config.Set("BalanceFrontRear", 0.0f, "Sound", -100.f, 100.f);
  config.Set("NbSoundChannels", 4, "Sound", 0, 0, { 1,2,4 });
  config.Set("SoundFreq", 57.6f, "Sound", 0.0f, 0.0f, { 57.524160f, 60.f }); // 60.0f? 57.524160f?
  // CDSB
  
  config.Set("EmulateDSB", true, "Sound");
  config.Set("SoundVolume", 100, "Sound", 0, 200);
  config.Set("MusicVolume", 100, "Sound", 0, 200);
  // Other sound options
  config.Set("LegacySoundDSP", false, "Sound"); // New config option for games that do not play correctly with MAME's SCSP sound core.
  // CDriveBoard
  config.Set("ForceFeedback", false, "ForceFeedback");
  
  // Platform-specific/UI
  config.Set("New3DEngine", true, "Video");
  config.Set("UserMenu", false, "Interface");
  config.Set("UserMenuStart", false, "Interface");
  config.Set("UserMenuRestart", false, "Interface");
  config.Set("UserMenuCalibrate", false, "Interface");
  config.Set("UserMenuBackground", "");
  config.Set("UserComfortStrength", 1.0f, "Video", 0.0f, 1.0f);
  config.Set("StereoDiagnostic", false, "Video");
  config.Set("StereoEyeSeparation", 1.0f, "Video", -1000.0f, 1000.0f);
  config.Set("StereoYaw", 0.0f, "Video", -180.0f, 180.0f);
  config.Set("StereoPitch", 0.0f, "Video", -89.0f, 89.0f);
  config.Set("StereoCullBypass", false, "Video");
  config.Set("StereoLodDistance", 1.0f, "Video", 1.0f, 4.0f);
  config.Set("StereoFullBike", false, "Video");
  config.Set("StereoStableChase", false, "Video");
  config.Set("StereoChaseHeight", 20.0f, "Video", 0.0f, 60.0f);
  config.Set("StereoChaseBack", 40.0f, "Video", 10.0f, 100.0f);
  config.Set("StereoUI", false, "Video");
  config.Set("StereoCinematicVignette", false, "Video");
  config.Set("StereoMenuComfort", false, "Video");
  config.Set("StereoFlatIntro", false, "Video");
  config.Set("StereoTheaterIntro", false, "Video");
  config.Set("StereoTheater", false, "Video");
  config.Set("StereoTheaterDepth", 0, "Video", 0, 2);
  config.Set("StereoTheaterScreenAngle", TheaterScreenControls::DefaultAngle, "Video",
             TheaterScreenControls::MinimumAngle, TheaterScreenControls::MaximumAngle);
  config.Set("StereoVRHUD", false, "Video");
  config.Set("StereoBikeHUD", false, "Video");
  config.Set("StereoHideHUD", false, "Video");
  config.Set("StereoROMCacheStress", 0, "Video", 0, 10000000);
  config.Set("StereoROMCacheStressBypass", false, "Video");
  config.Set("HarleyFreeRide", false, "Game");
  config.Set("HarleyMusicLevel", 0, "Sound", 0, 8);
  config.Set("StereoHorizonLock", false, "Video");
  config.Set("StereoRiderAnchor", false, "Video");
  config.Set("StereoRiderComfort", false, "Video");
  config.Set("StereoVignetteScale", 1.0f, "Video", 0.0f, 1.0f);
  config.Set("StereoRiderUp", 0.0f, "Video", -10.0f, 30.0f);
  config.Set("StereoRiderBack", 0.0f, "Video", -20.0f, 30.0f);
  config.Set("StereoHorizonFov", 90.0f, "Video", 30.0f, 140.0f);
  config.Set("StereoCameraTrace", "");
  config.Set("StereoCameraTraceAllModels", false);
  config.Set("StereoDuplicateAudit", "");
  config.Set("StereoCameraTraceStart", 1u);
  config.Set("StereoCameraTraceFrames", 120u);
  config.Set("StereoCameraTraceFrameId", 0u);
  config.Set("QuadRendering", false, "Video");
  config.Set("XResolution", 496, "Video");
  config.Set("YResolution", 384, "Video");
  config.SetEmpty("WindowXPosition");
  config.SetEmpty("WindowYPosition");
  config.Set("FullScreen", false, "Video");
  config.Set("BorderlessWindow", false, "Video");
  config.Set("Supersampling", 1, "Video", 1, 8);
  config.Set("CRTcolors", int(0), "Video", 0, 0, { 0,1,2,3,4,5 });      // these might be more user friendly as strings
  config.Set("UpscaleMode", 2, "Video", 0, 0, { 0,1,2,3 });             // to do make strings
  config.Set("WideScreen", false, "Video");
  config.Set("Stretch", false, "Video");
  config.Set("WideBackground", false, "Video");
  config.Set("VSync", true, "Video");
  config.Set("Throttle", true, "Video");
  config.Set("RefreshRate", 60.0f, "Video", 0.0f, 0.0f, { 57.5f,60.f });
  config.Set("ShowFrameRate", false, "Video");
  config.Set("Crosshairs", int(0), "Video", 0, 0, { 0,1,2,3 });
  config.Set<std::string>("CrosshairStyle", "vector", "Video", "", "", { "bmp","vector" });
  config.Set("NoWhiteFlash", false, "Video");
  config.Set("FlipStereo", false, "Sound");
#ifdef SUPERMODEL_WIN32
  config.Set<std::string>("InputSystem", "dinput", "Core", "", "", { "sdl","sdlgamepad","dinput","xinput","rawinput" });
  // DirectInput ForceFeedback
  config.Set("DirectInputConstForceLeftMax", 100, "ForceFeedback", 0, 100);
  config.Set("DirectInputConstForceRightMax", 100, "ForceFeedback", 0, 100);
  config.Set("DirectInputSelfCenterMax", 100, "ForceFeedback", 0, 100);
  config.Set("DirectInputFrictionMax", 100, "ForceFeedback", 0, 100);
  config.Set("DirectInputVibrateMax", 100, "ForceFeedback", 0, 100);
  // XInput ForceFeedback
  config.Set("XInputConstForceThreshold", 30, "ForceFeedback", 0, 100);
  config.Set("XInputConstForceMax", 100, "ForceFeedback", 0, 100);
  config.Set("XInputVibrateMax", 100, "ForceFeedback", 0, 100);
  config.Set("XInputStereoVibration", true, "ForceFeedback");
  // SDL ForceFeedback
  config.Set("SDLConstForceMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLSelfCenterMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLFrictionMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLVibrateMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLConstForceThreshold", 30, "ForceFeedback", 0, 100);
#else
  config.Set<std::string>("InputSystem", "sdl", "Core", "", "", { "sdl","sdlgamepad" });
  // SDL ForceFeedback
  config.Set("SDLConstForceMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLSelfCenterMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLFrictionMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLVibrateMax", 100, "ForceFeedback", 0, 100);
  config.Set("SDLConstForceThreshold", 30, "ForceFeedback", 0, 100);
#endif
  // NetBoard
  config.Set("Network", false, "Network");
  config.Set("SimulateNet", true, "Network");
  config.Set("PortIn", unsigned(1970), "Network");
  config.Set("PortOut", unsigned(1971), "Network");
  config.Set<std::string>("AddressOut", "127.0.0.1", "Network", "", "");

#ifdef SUPERMODEL_WIN32
  config.Set<std::string>("Outputs", "none", "Misc", "", "", { "none","win","net" });
#else
  config.Set<std::string>("Outputs", "none", "Misc", "", "", { "none","net" });
#endif
  config.Set<bool>("OutputsWithLF", false, "Misc");
  config.Set<unsigned int>("OutputsTCPPort", 8000, "Misc", 1024, 65535);
  config.Set<unsigned int>("OutputsUDPBroadcastPort", 8001, "Misc", 1024, 65535);

  config.Set("DumpMemory", false, "Misc");
  config.Set("DumpTextures", false, "Misc");

  //
  // Input sensitivity
  //
  config.Set<unsigned>("InputDigitalSensitivity", DEFAULT_DIGITAL_SENSITIVITY, "Sensitivity", 0, 100);
  config.Set<unsigned>("InputDigitalDecaySpeed", DEFAULT_DIGITAL_DECAYSPEED, "Sensitivity", 0, 100);
  config.Set<unsigned>("InputKeySensitivity", DEFAULT_DIGITAL_SENSITIVITY, "Sensitivity", 0, 100);
  config.Set<unsigned>("InputKeyDecaySpeed", DEFAULT_DIGITAL_DECAYSPEED, "Sensitivity", 0, 100);

  config.Set<unsigned>("InputMouseXDeadZone", DEFAULT_MSE_DEADZONE, "Sensitivity", 0, 100);
  config.Set<unsigned>("InputMouseYDeadZone", DEFAULT_MSE_DEADZONE, "Sensitivity", 0, 100);
  config.Set<unsigned>("InputMouseZDeadZone", DEFAULT_MSE_DEADZONE, "Sensitivity", 0, 100);

  //
  // controls
  //

  // Common
  config.Set<std::string>("InputStart1", "KEY_1,JOY1_BUTTON9", "Input", "", "");
  config.Set<std::string>("InputStart2", "KEY_2,JOY2_BUTTON9", "Input", "", "");
  config.Set<std::string>("InputCoin1", "KEY_3,JOY1_BUTTON10", "Input", "", "");
  config.Set<std::string>("InputCoin2", "KEY_4,JOY2_BUTTON10", "Input", "", "");
  config.Set<std::string>("InputServiceA", "KEY_5", "Input", "", "");
  config.Set<std::string>("InputServiceB", "KEY_7", "Input", "", "");
  config.Set<std::string>("InputTestA", "KEY_6", "Input", "", "");
  config.Set<std::string>("InputTestB", "KEY_8", "Input", "", "");

  // 4-way digital joysticks
  config.Set<std::string>("InputJoyUp", "KEY_UP,JOY1_UP", "Input", "", "");
  config.Set<std::string>("InputJoyDown", "KEY_DOWN,JOY1_DOWN", "Input", "", "");
  config.Set<std::string>("InputJoyLeft", "KEY_LEFT,JOY1_LEFT", "Input", "", "");
  config.Set<std::string>("InputJoyRight", "KEY_RIGHT,JOY1_RIGHT", "Input", "", "");
  config.Set<std::string>("InputJoyUp2", "JOY2_UP", "Input", "", "");
  config.Set<std::string>("InputJoyDown2", "JOY2_DOWN", "Input", "", "");
  config.Set<std::string>("InputJoyLeft2", "JOY2_LEFT", "Input", "", "");
  config.Set<std::string>("InputJoyRight2", "JOY2_RIGHT", "Input", "", "");

  // Fighting game buttons
  config.Set<std::string>("InputPunch", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputKick", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputGuard", "KEY_D,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputEscape", "KEY_F,JOY1_BUTTON4", "Input", "", "");
  config.Set<std::string>("InputPunch2", "JOY2_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputKick2", "JOY2_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputGuard2", "JOY2_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputEscape2", "JOY2_BUTTON4", "Input", "", "");

  // Spikeout buttons
  config.Set<std::string>("InputShift", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputBeat", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputCharge", "KEY_D,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputJump", "KEY_F,JOY1_BUTTON4", "Input", "", "");

  // Virtua Striker buttons
  config.Set<std::string>("InputShortPass", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputLongPass", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputShoot", "KEY_D,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputShortPass2", "JOY2_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputLongPass2", "JOY2_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputShoot2", "JOY2_BUTTON3", "Input", "", "");

  // Steering wheel
  config.Set<std::string>("InputSteeringLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputSteeringRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputSteering", "JOY1_XAXIS", "Input", "", "");

  // Pedals
  config.Set<std::string>("InputAccelerator", "KEY_UP,JOY1_UP", "Input", "", "");
  config.Set<std::string>("InputBrake", "KEY_DOWN,JOY1_DOWN", "Input", "", "");

  // Up/down shifter manual transmission (all racers)
  config.Set<std::string>("InputGearShiftUp", "KEY_Y", "Input", "", "");
  config.Set<std::string>("InputGearShiftDown", "KEY_H", "Input", "", "");

  // 4-Speed manual transmission (Daytona 2, Sega Rally 2, Scud Race)
  config.Set<std::string>("InputGearShift1", "KEY_Q,JOY1_BUTTON5", "Input", "", "");
  config.Set<std::string>("InputGearShift2", "KEY_W,JOY1_BUTTON6", "Input", "", "");
  config.Set<std::string>("InputGearShift3", "KEY_E,JOY1_BUTTON7", "Input", "", "");
  config.Set<std::string>("InputGearShift4", "KEY_R,JOY1_BUTTON8", "Input", "", "");
  config.Set<std::string>("InputGearShiftN", "KEY_T", "Input", "", "");

  // VR4 view change buttons (Daytona 2, Le Mans 24, Scud Race)
  config.Set<std::string>("InputVR1", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputVR2", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputVR3", "KEY_D,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputVR4", "KEY_F,JOY1_BUTTON4", "Input", "", "");

  // Single view change button (Dirt Devils, ECA, Harley-Davidson, Sega Rally 2)
  config.Set<std::string>("InputViewChange", "KEY_A,JOY1_BUTTON1", "Input", "", "");

  // Handbrake (Sega Rally 2)
  config.Set<std::string>("InputHandBrake", "KEY_S,JOY1_BUTTON2", "Input", "", "");

  // Harley-Davidson controls
  config.Set<std::string>("InputRearBrake", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputMusicSelect", "KEY_D,JOY1_BUTTON3", "Input", "", "");

  // Virtual On macros
  config.Set<std::string>("InputTwinJoyTurnLeft", "KEY_Q,JOY1_RXAXIS_NEG", "Input", "", "");
  config.Set<std::string>("InputTwinJoyTurnRight", "KEY_W,JOY1_RXAXIS_POS", "Input", "", "");
  config.Set<std::string>("InputTwinJoyForward", "KEY_UP,JOY1_YAXIS_NEG", "Input", "", "");
  config.Set<std::string>("InputTwinJoyReverse", "KEY_DOWN,JOY1_YAXIS_POS", "Input", "", "");
  config.Set<std::string>("InputTwinJoyStrafeLeft", "KEY_LEFT,JOY1_XAXIS_NEG", "Input", "", "");
  config.Set<std::string>("InputTwinJoyStrafeRight", "KEY_RIGHT,JOY1_XAXIS_POS", "Input", "", "");
  config.Set<std::string>("InputTwinJoyJump", "KEY_E,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputTwinJoyCrouch", "KEY_R,JOY1_BUTTON2", "Input", "", "");

  // Virtual On individual joystick mapping
  config.Set<std::string>("InputTwinJoyLeft1", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyLeft2", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyRight1", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyRight2", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyUp1", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyUp2", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyDown1", "NONE", "Input", "", "");
  config.Set<std::string>("InputTwinJoyDown2", "NONE", "Input", "", "");

  // Virtual On buttons
  config.Set<std::string>("InputTwinJoyShot1", "KEY_A,JOY1_BUTTON5", "Input", "", "");
  config.Set<std::string>("InputTwinJoyShot2", "KEY_S,JOY1_BUTTON6", "Input", "", "");
  config.Set<std::string>("InputTwinJoyTurbo1", "KEY_Z,JOY1_BUTTON7", "Input", "", "");
  config.Set<std::string>("InputTwinJoyTurbo2", "KEY_X,JOY1_BUTTON8", "Input", "", "");

  // Analog joystick (Star Wars Trilogy)
  config.Set<std::string>("InputAnalogJoyLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyX", "JOY_XAXIS,MOUSE_XAXIS", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyY", "JOY_YAXIS,MOUSE_YAXIS", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyTrigger", "KEY_A,JOY_BUTTON1,MOUSE_LEFT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyEvent", "KEY_S,JOY_BUTTON2,MOUSE_RIGHT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyTrigger2", "KEY_D,JOY_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputAnalogJoyEvent2", "NONE", "Input", "", "");

  // Light guns (Lost World)
  config.Set<std::string>("InputGunLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputGunRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputGunUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputGunDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputGunX", "MOUSE_XAXIS,JOY1_XAXIS", "Input", "", "");
  config.Set<std::string>("InputGunY", "MOUSE_YAXIS,JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputTrigger", "KEY_A,JOY1_BUTTON1,MOUSE_LEFT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputOffscreen", "KEY_S,JOY1_BUTTON2,MOUSE_RIGHT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAutoTrigger", "0", "Input", "", "");
  config.Set<std::string>("InputGunLeft2", "NONE", "Input", "", "");
  config.Set<std::string>("InputGunRight2", "NONE", "Input", "", "");
  config.Set<std::string>("InputGunUp2", "NONE", "Input", "", "");
  config.Set<std::string>("InputGunDown2", "NONE", "Input", "", "");
  config.Set<std::string>("InputGunX2", "JOY2_XAXIS", "Input", "", "");
  config.Set<std::string>("InputGunY2", "JOY2_YAXIS", "Input", "", "");
  config.Set<std::string>("InputTrigger2", "JOY2_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputOffscreen2", "JOY2_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputAutoTrigger2", "0", "Input", "", "");

  // Analog guns (Ocean Hunter, LA Machineguns)
  config.Set<std::string>("InputAnalogGunLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputAnalogGunRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputAnalogGunUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputAnalogGunDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputAnalogGunX", "MOUSE_XAXIS,JOY1_XAXIS", "Input", "", "");
  config.Set<std::string>("InputAnalogGunY", "MOUSE_YAXIS,JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputAnalogTriggerLeft", "KEY_A,JOY1_BUTTON1,MOUSE_LEFT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAnalogTriggerRight", "KEY_S,JOY1_BUTTON2,MOUSE_RIGHT_BUTTON", "Input", "", "");
  config.Set<std::string>("InputAnalogGunLeft2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunRight2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunUp2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunDown2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunX2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogGunY2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogTriggerLeft2", "NONE", "Input", "", "");
  config.Set<std::string>("InputAnalogTriggerRight2", "NONE", "Input", "", "");

  // Ski Champ controls
  config.Set<std::string>("InputSkiLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputSkiRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputSkiUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputSkiDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputSkiX", "JOY1_XAXIS", "Input", "", "");
  config.Set<std::string>("InputSkiY", "JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputSkiPollLeft", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputSkiPollRight", "KEY_S,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputSkiSelect1", "KEY_Q,JOY1_BUTTON3", "Input", "", "");
  config.Set<std::string>("InputSkiSelect2", "KEY_W,JOY1_BUTTON4", "Input", "", "");
  config.Set<std::string>("InputSkiSelect3", "KEY_E,JOY1_BUTTON5", "Input", "", "");

  // Magical Truck Adventure controls
  config.Set<std::string>("InputMagicalLeverUp1", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputMagicalLeverDown1", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputMagicalLeverUp2", "NONE", "Input", "", "");
  config.Set<std::string>("InputMagicalLeverDown2", "NONE", "Input", "", "");
  config.Set<std::string>("InputMagicalLever1", "JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputMagicalLever2", "JOY2_YAXIS", "Input", "", "");
  config.Set<std::string>("InputMagicalPedal1", "KEY_A,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputMagicalPedal2", "KEY_S,JOY2_BUTTON1", "Input", "", "");

  // Sega Bass Fishing / Get Bass controls
  config.Set<std::string>("InputFishingRodLeft", "KEY_LEFT", "Input", "", "");
  config.Set<std::string>("InputFishingRodRight", "KEY_RIGHT", "Input", "", "");
  config.Set<std::string>("InputFishingRodUp", "KEY_UP", "Input", "", "");
  config.Set<std::string>("InputFishingRodDown", "KEY_DOWN", "Input", "", "");
  config.Set<std::string>("InputFishingStickLeft", "KEY_A", "Input", "", "");
  config.Set<std::string>("InputFishingStickRight", "KEY_D", "Input", "", "");
  config.Set<std::string>("InputFishingStickUp", "KEY_W", "Input", "", "");
  config.Set<std::string>("InputFishingStickDown", "KEY_S", "Input", "", "");
  config.Set<std::string>("InputFishingRodX", "JOY1_XAXIS", "Input", "", "");
  config.Set<std::string>("InputFishingRodY", "JOY1_YAXIS", "Input", "", "");
  config.Set<std::string>("InputFishingStickX", "JOY1_RXAXIS", "Input", "", "");
  config.Set<std::string>("InputFishingStickY", "JOY1_RYAXIS", "Input", "", "");
  config.Set<std::string>("InputFishingReel", "KEY_SPACE,JOY1_ZAXIS_POS", "Input", "", "");
  config.Set<std::string>("InputFishingCast", "KEY_Z,JOY1_BUTTON1", "Input", "", "");
  config.Set<std::string>("InputFishingSelect", "KEY_X,JOY1_BUTTON2", "Input", "", "");
  config.Set<std::string>("InputFishingTension", "KEY_T,JOY1_ZAXIS_NEG", "Input", "", "");



  return config;
}

static void Title(void)
{
  puts("Supermodel: A Sega Model 3 Arcade Emulator (Version " SUPERMODEL_VERSION ")");
  puts("Copyright 2003-2026 by The Supermodel Team");
}

static void Help(void)
{
  Util::Config::Node defaultConfig = DefaultConfig();
  puts("Usage: Supermodel <romset> [options]");
  puts("ROM set must be a valid ZIP file containing a single game.");
  puts("");
  puts("General Options:");
  puts("  -?, -h, -help, --help   Print this help text");
  puts("  -print-games            List supported games and quit");
  printf("  -game-xml-file=<file>   ROM set definition file [Default: %s]\n", s_gameXMLFilePath.c_str());
  printf("  -log-output=<outputs>   Log output destination(s) [Default: %s]\n", s_logFilePath.c_str());
  puts("  -log-level=<level>      Logging threshold [Default: info]");
  puts("");
  puts("Core Options:");
  puts("  -ppc-frequency=<mhz>    PowerPC frequency (default varies by stepping)");
  puts("  -no-threads             Disable multi-threading entirely");
  puts("  -gpu-multi-threaded     Run graphics rendering in separate thread [Default]");
  puts("  -no-gpu-thread          Run graphics rendering in main thread");
  puts("  -idle-skip              Enable experimental PowerPC idle-loop skipping");
  puts("  -no-idle-skip           Execute PowerPC idle loops in full [Default]");
  puts("  -load-state=<file>      Load save state after starting");
  puts("  -run-frames=<n>         Exit after n emulated frames (1-10000000)");
  puts("  -diagnostic-hidden     Hidden GL surface for bounded, muted diagnostics only");
  puts("  -presentation-test=<prefix> Verify independent poses on one frozen scene");
  puts("  -presentation-test-frame=<n> Native frame to hold (default 1); bounded stereo only");
  puts("  -mouse-look             Hold right mouse to look; Home recenters; SDL input only");
  puts("  -mouse-look-sensitivity=<n> Degrees per mouse count (0.01-1, default 0.12)");
  puts("  -stereo-mono-preview    Display one eye fitted to the whole desktop window");
  puts("  -mouse-look-test        Bounded synthetic mouse-look regression sequence");
  puts("  -capture-frame=<n>      Capture frame n before display swap (1-10000000)");
  puts("  -frame-profile=<path>   Exclusive CPU and renderer timing JSONL prefix");
  puts("  -profile-start=<n>      First sampled RunFrame (default121)");
  puts("  -profile-frames=<n>     Samples, 1..10000 (default600); requires run-frames");
  puts("  -profile-threaded      Keep requested threads in bounded timing-only runs");
  puts("                          No scripted inputs, captures, or camera traces");
  puts("  -present-hz=<n>         Independent desktop redraw target (60..360, 0 disables)");
  puts("                          Forces native game speed, threaded New3D and no VSync");
  puts("  -presentation-profile=<path> Bounded actual-swap JSONL; profile-start/frames apply");
  puts("                          Requires present-hz and run-frames; bounded Harley input-script allowed");
  puts("  -capture-prefix=<path>  Write path.bmp and path.txt [Default: Diagnostic]");
  puts("  -capture-state=<file>   Save state immediately BEFORE captured frame;");
  puts("                          replay with -load-state and -capture-frame=1");
  puts("                          Bounded/capture runs force single-threading.");
  puts("                          Capture outputs must not already exist.");
  puts("  -input-script=<file>    Harley test inputs: frame control value per line");
  puts("                          # comments; values hold until another event");
  puts("                          Coin1, Start1, ViewChange, MusicSelect, GearShiftUp/Down, TestA, ServiceA: 0 or 1");
  puts("                          Accelerator, Steering, Brake, RearBrake: 0..255");
  puts("                          Unset controls neutral; steering center 128");
  puts("                          Requires -run-frames; UI controls stay live");
  puts("");
  puts("Video Options:");
  puts("  -stereo-diagnostic      Experimental side-by-side stereo, 3D layers only");
  puts("                          Forces New3D; no headset connection");
  puts("  -stereo-separation=<n>  Full eye separation in game units (-1000..1000)");
  puts("                          Default 1; 0 checks identical eyes, negative swaps");
  puts("  -stereo-yaw=<degrees>   Camera yaw (-180..180), default 0");
  puts("  -stereo-pitch=<degrees> Camera pitch (-89..89), default 0");
  puts("  -stereo-cull-bypass     Bypass renderer view culling for scenery tests");
  puts("  -stereo-ui              Include native menu/HUD tile and 3D overlay layers per eye");
  puts("  -stereo-menu-comfort    Automatic Harley intro/rider/race-start masks; instant attack");
  puts("                          Selection input protects before movement; fades after settling");
  puts("  -stereo-flat-intro      Show Harley opening/race fly-ins as native flat images");
  puts("                          Enables menu comfort; immersive view returns after the zoom");
  puts("  -no-stereo-flat-intro   Disable flat intros; also disable theater to restore 3D");
  puts("  -stereo-theater         Present Harley menus/cinematics on a screen in black space");
  puts("  -openxr                 Experimental Windows headset session using the selected OpenXR runtime");
  puts("  -xr-scale=<0.5..1.5>    Per-eye resolution relative to runtime recommendation; default0.75");
  puts("  -xr-visibility-mask=<0|1> Optional runtime lens-hidden world masking; default0");
  puts("  -xr-world-scale=<1..100> Game units per metre; default16, pending physical calibration");
  puts("  -xr-mirror              Optional desktop mirror, allowed only when monitor swaps are unsynchronized");
  puts("  -xr-render-test=<prefix> Bounded off-headset stereo rendering diagnostic");
  puts("  -xr-render-test-frame=<n> Prepared native frame to test; default1");
  puts("  -xr-test-theater        Require pitch-black look-away in that diagnostic");
  puts("  -xr-test-performance    Benchmark frozen headset eye targets without a runtime");
  puts("                          Rider selection, driving, and ending stay immersive");
  puts("  -stereo-theater-intro   Compatibility alias for -stereo-theater");
  puts("  -no-stereo-theater      Disable theater (also -no-stereo-theater-intro)");
  puts("  -stereo-theater-size=<degrees> Screen angular width 40..100, default 70");
  puts("  -stereo-theater-depth=0     Flat cinema only; legacy depth values are ignored");
  puts("                          PageUp enlarges / PageDown shrinks by 5; Home recenters look");
  puts("  -stereo-vr-hud          Place the driving HUD on a lower spatial panel");
  puts("  -stereo-bike-hud        Preview separate instruments mounted near the bike controls");
  puts("  -stereo-hide-hud        Hide driving HUD; retain intro and selection screens");
  puts("  -stereo-rom-cache-stress=<frame> Bounded cache-pressure test; requires -run-frames");
  puts("  -stereo-rom-cache-stress-bypass  Apply that test to the expanded eye decode");
  puts("  -harley-free-ride       Experimental Revision B no-time-limit driving mode");
  puts("  -harley-music-level=<n> Harley BGM setting 1..8; 0 keeps the game setting");
  puts("  -stereo-cinematic-vignette  Force strong mask; keeps native UI readable");
  puts("                          C toggles the forced mask in interactive mouse-look UI preview");
  puts("  -no-stereo-cinematic-vignette  Start that preview without the cinematic mask");
  puts("  -stereo-lod-distance=<n> Extend supplied world LOD distance (1..4, default1)");
  puts("                          Values above 1 enable stereo and eye culling bypass");
  puts("  -stereo-stable-chase    Experimental stabilized Fat Boy chase camera; rider visible");
  puts("  -stereo-chase-height=<n>  World-up chase height (0..60, default 20)");
  puts("  -stereo-chase-back=<n>  Upright body-relative pullback (10..100, default 40)");
  puts("  -stereo-full-bike       Experimental Harley rider cameras from chase geometry");
  puts("                          Enables rider anchor and eye culling bypass");
  puts("  -stereo-horizon-lock    Experimental world camera horizon stabilization");
  puts("                          Enables stereo diagnostic; native LOS retained");
  puts("  -stereo-rider-anchor    Experimental Harley cockpit position anchors");
  puts("                          Enables horizon lock; measured native rider anchor");
  puts("                          Other views/models fall back to horizon lock");
  puts("  -stereo-horizon-fov=<n> Symmetric world FOV when locked (30..140, default90)");
  puts("  -stereo-rider-comfort   Vertical bump filter and soft motion vignette; enables anchor");
  puts("                          Experimental: not full crash isolation; cockpit only");
  puts("  -stereo-rider-up=<n>    Rider-anchor rise in game units (-10..30, default0)");
  puts("  -stereo-vignette-scale=<n>  Peripheral mask strength (0..1, default1)");
  puts("  -stereo-rider-back=<n>  Upright rearward offset (-20..30, default0)");
  puts("  -stereo-camera-trace=<file> Record camera transforms (requires run-frames)");
  puts("  -stereo-camera-trace-start=<n> First emulated frame to trace [Default: 1]");
  puts("  -stereo-camera-trace-frames=<n> Maximum samples (1..600) [Default: 120]");
  puts("  -stereo-duplicate-audit=<file> Off-headset exact opaque/depth/stencil audit (xr-render-test only)");
  puts("  -res=<x>,<y>            Resolution [Default: 496,384]");
  puts("  -ss=<n>                 Supersampling (range 1-8)");
  puts("  -window-pos=<x>,<y>     Window position [Default: centered]");
  puts("  -window                 Windowed mode [Default]");
  puts("  -borderless             Windowed mode with no border");
  puts("  -fullscreen             Full screen mode");
  puts("  -wide-screen            Expand 3D field of view to screen width");
  puts("  -wide-bg                When wide-screen mode is enabled, also expand the 2D");
  puts("                          background layer to screen width");
  puts("  -stretch                Fit viewport to resolution, ignoring aspect ratio");
  puts("  -upscalemode=<n>        2D layer upscaling filter mode (range 0-3)");
  puts("  -crtcolors=<n>          CRT color emulation (range 0-5)");
  puts("  -no-throttle            Disable frame rate lock");
  puts("  -vsync                  Lock to vertical refresh rate [Default]");
  puts("  -no-vsync               Do not lock to vertical refresh rate");
  puts("  -true-hz                Use true Model 3 refresh rate of 57.524 Hz");
  puts("  -show-fps               Display frame rate in window title bar");
  puts("  -crosshairs=<n>         Crosshairs configuration for gun games:");
  puts("                          0=none [Default], 1=P1 only, 2=P2 only, 3=P1 & P2");
  puts("  -crosshair-style=<s>    Crosshair style: vector or bmp. [Default: vector]");
#ifndef SUPERMODEL_OSX
  puts("  -new3d                  New 3D engine by Ian Curtis [Default]");
#endif
  puts("  -quad-rendering         Enable proper quad rendering");
#ifndef SUPERMODEL_OSX
  puts("  -legacy3d               Legacy 3D engine (faster but less accurate)");
  puts("  -multi-texture          Use 8 texture maps for decoding (legacy engine)");
  puts("  -no-multi-texture       Decode to single texture (legacy engine) [Default]");
#endif
  puts("  -no-white-flash         Disables white flash when games disable 3D rendering");
#ifndef SUPERMODEL_OSX
  puts("  -vert-shader=<file>     Load Real3D vertex shader for 3D rendering (legacy");
  puts("                          engine)");
  puts("  -frag-shader=<file>     Load Real3D fragment shader for 3D rendering (legacy");
  puts("                          engine)");
#endif
  puts("  -print-gl-info          Print OpenGL driver information and quit");
  puts("");
  puts("Audio Options:");
  puts("  -sound-volume=<vol>     Volume of SCSP-generated sound in %, applies only");
  puts("                          when Digital Sound Board is present [Default: 100]");
  puts("  -music-volume=<vol>     Digital Sound Board volume in % [Default: 100]");
  puts("  -balance=<bal>          Relative front/rear balance in % [Default: 0]");
  puts("  -channels=<c>           Number of sound channels to use on host [Default: 4]");
  puts("  -flip-stereo            Swap left and right audio channels");
  puts("  -no-sound               Disable sound board emulation (sound effects)");
  puts("  -no-dsb                 Disable Digital Sound Board (MPEG music)");
  puts("  -new-scsp               New SCSP engine based on MAME [Default]");
  puts("  -legacy-scsp            Legacy SCSP engine by ElSemi");
  puts("");
  puts("Net Options:");
  puts("  -no-net                 Disable net board [Default]");
  puts("  -net                    Enable net board");
  puts("  -simulate-netboard      Simulate the net board [Default]");
  puts("  -emulate-netboard       Emulate the net board (requires -no-threads)");
  puts("");
  puts("Input Options:");
  puts("  -force-feedback         Enable force feedback (DirectInput, XInput)");
  puts("  -config-inputs          Configure keyboards, mice, and game controllers");
  puts("  -user-menu=<0|1>        HarleyVR in-game settings (Tab / Pause / Options)");
  puts("  -user-menu-start=<0|1>  Open settings after the first valid scene in interactive play");
  puts("  -user-menu-background=<BMP> Optional locally supplied menu artwork");
  puts("  -user-comfort-strength=<n> Global comfort-mask strength, 0..1 (default 1)");
#ifdef SUPERMODEL_WIN32
  printf("  -input-system=<s>       Input system [Default: %s]\n", defaultConfig["InputSystem"].ValueAs<std::string>().c_str());
#endif
  puts("  -print-inputs           Prints current input configuration");
  puts("");
  puts("Output Options:");
  printf("  -outputs=<s>            Outputs [Default: %s]\n", defaultConfig["Outputs"].ValueAs<std::string>().c_str());
  puts("");
  puts("Debug Options:");
  puts("  -dump-memory            Write memory regions to files on exit");
  puts("  -dump-textures          Write textures to bitmap image files on exit");
#ifdef SUPERMODEL_DEBUGGER
  puts("  -disable-debugger       Completely disable debugger functionality");
  puts("  -enter-debugger         Enter debugger at start of emulation");
#endif // SUPERMODEL_DEBUGGER
#ifdef DEBUG
  puts("  -gfx-state=<file>       Produce graphics analysis for save state (works only");
  puts("                          with the legacy 3D engine and requires a");
  puts("                          GraphicsAnalysis directory to exist)");
#endif
  puts("");
}

struct ParsedCommandLine
{
  Util::Config::Node config = Util::Config::Node("CommandLine");
  std::vector<std::string> rom_files;
  bool error = false;
  bool print_help = false;
  bool print_games = false;
  bool print_gl_info = false;
  bool config_inputs = false;
  bool print_inputs = false;
  bool disable_debugger = false;
  bool enter_debugger = false;
#ifdef DEBUG
  std::string gfx_state;
#endif

  ParsedCommandLine()
  {
    // Logging is special: it is only parsed from the command line and
    // therefore, defaults are needed early
    config.Set("LogOutput", s_logFilePath.c_str());
    config.Set("LogLevel", "info");
  }
};

static bool ValidateDiagnosticNumbers(Util::Config::Node &config, bool commandLine)
{
  if (!config["StereoTheaterDepth"].Empty()) {
    const auto depth=config["StereoTheaterDepth"].ValueAs<std::string>();
    if (depth!="0" && depth!="1" && depth!="2") {
      ErrorLog("StereoTheaterDepth contains an invalid value; use 0 (Flat)."); return false;
    }
    if (depth!="0") InfoLog("Stereo cinema depth is disabled; ignoring legacy depth %s.",depth.c_str());
    config.Set("StereoTheaterDepth",0);
  }
  for (const char* key : {"UserMenu","UserMenuStart","UserMenuRestart","UserMenuCalibrate"}) {
    if (config[key].Empty()) continue;
    const auto value=config[key].ValueAs<std::string>();
    if (value!="0" && value!="1" && value!="true" && value!="false") {
      ErrorLog("%s requires 0 or 1.",key); return false;
    }
    config.Set(key,value=="1" || value=="true");
  }
  for (const char *key : { "RunFrames", "CaptureFrame", "PresentationTestFrame", "HeadsetRenderTestFrame", "StereoCameraTraceStart", "StereoCameraTraceFrames", "FrameProfileStart", "FrameProfileFrames" })
  {
    if (config[key].Empty())
      continue;
    const std::string value = config[key].ValueAs<std::string>();
    const bool digits = !value.empty() && std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
    char *end = nullptr;
    errno = 0;
    const unsigned long long count = std::strtoull(value.c_str(), &end, 10);
    const bool traceSetting = std::string(key).find("StereoCameraTrace") == 0;
    const bool profileSetting = std::string(key).find("FrameProfile") == 0;
    const unsigned minimum = (commandLine || traceSetting || profileSetting || std::string(key) == "PresentationTestFrame" || std::string(key) == "HeadsetRenderTestFrame") ? 1 : 0;
    const unsigned maximum = std::string(key) == "StereoCameraTraceFrames" ? 600 : std::string(key) == "FrameProfileFrames" ? 10000 : 10000000;
    if (!digits || errno == ERANGE || *end != '\0' || count > maximum || count < minimum)
    {
      ErrorLog("%s requires a whole number from %u to %u (received '%s').", key, minimum, maximum, value.c_str());
      return false;
    }
    config.Set(key, unsigned(count));
  }
  struct FloatLimit { const char *key; float minimum; float maximum; };
  for (const auto &limit : std::vector<FloatLimit> {
         { "StereoEyeSeparation", -1000.0f, 1000.0f }, { "StereoYaw", -180.0f, 180.0f },
         { "StereoPitch", -89.0f, 89.0f }, { "StereoHorizonFov", 30.0f, 140.0f }, { "StereoLodDistance", 1.0f, 4.0f },
         { "UserComfortStrength", 0.0f, 1.0f },
         { "StereoChaseHeight", 0.0f, 60.0f }, { "StereoChaseBack", 10.0f, 100.0f },
         { "DesktopPresentHz", 0.0f, 360.0f },
         { "OpenXRResolutionScale", 0.5f, 1.5f }, { "OpenXRUnitsPerMeter", 1.0f, 100.0f },
         { "StereoTheaterScreenAngle", TheaterScreenControls::MinimumAngle, TheaterScreenControls::MaximumAngle },
         { "StereoRiderUp", -10.0f, 30.0f }, { "StereoRiderBack", -20.0f, 30.0f }, { "StereoVignetteScale", 0.0f, 1.0f }, { "MouseLookSensitivity", 0.01f, 1.0f } })
  {
    if (config[limit.key].Empty())
      continue;
    const std::string value = config[limit.key].ValueAs<std::string>();
    char *end = nullptr;
    errno = 0;
    const float number = std::strtof(value.c_str(), &end);
    if (value.empty() || end == value.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(number) || number < limit.minimum || number > limit.maximum)
    {
      ErrorLog("%s requires a finite number in [%g, %g] (received '%s').", limit.key, limit.minimum, limit.maximum, value.c_str());
      return false;
    }
    config.Set(limit.key, number);
  }
  if (!config["DesktopPresentHz"].Empty()) {
    const float rate = config["DesktopPresentHz"].ValueAs<float>();
    if (rate > 0 && rate < 60) {
      ErrorLog("DesktopPresentHz must be 0 (disabled) or 60..360 Hz.");
      return false;
    }
  }
  return true;
}

static bool ReadDiagnosticInputScript(const Util::Config::Node &config, const Game &game)
{
  s_inputScriptEvents.clear();
  s_inputScriptHeld.clear();
  s_inputScriptNextEvent = 0;
  const std::string path = config["InputScript"].ValueAs<std::string>();
  if (path.empty())
    return true;
  const unsigned runFrames = config["RunFrames"].ValueAs<unsigned>();
  if (runFrames == 0 || !(game.inputs & Game::INPUT_HARLEY))
  {
    ErrorLog("InputScript requires a bounded RunFrames and a Harley ROM set.");
    return false;
  }
  std::error_code fileError;
  const auto fileSize = std::filesystem::file_size(path, fileError);
  if (fileError || fileSize > 8 * 1024 * 1024)
  {
    ErrorLog("Input script '%s' is unreadable or exceeds 8 MiB.", path.c_str());
    return false;
  }
  std::ifstream file(path);
  if (!file)
  {
    ErrorLog("Unable to open input script '%s'.", path.c_str());
    return false;
  }
  auto parseUnsigned = [](const std::string &text, unsigned maximum, unsigned &result) {
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
      return false;
    errno = 0;
    char *end = nullptr;
    unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed > maximum)
      return false;
    result = unsigned(parsed);
    return true;
  };
  unsigned lineNumber = 0;
  std::string line;
  while (std::getline(file, line))
  {
    lineNumber++;
    if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
      line.erase(0, 3); // UTF-8 BOM from Windows text editors
    const auto comment = line.find('#');
    if (comment != std::string::npos)
      line.erase(comment);
    std::istringstream fields(line);
    std::string frameText, control, valueText, extra;
    if (!(fields >> frameText))
      continue;
    if (!(fields >> control >> valueText) || (fields >> extra))
    {
      ErrorLog("Input script '%s' line %u: expected frame control value.", path.c_str(), lineNumber);
      return false;
    }
    const auto maximum = s_inputScriptMaximum.find(control);
    unsigned frame = 0, value = 0;
    if (maximum == s_inputScriptMaximum.end() || !parseUnsigned(frameText, runFrames, frame) || frame == 0 ||
        !parseUnsigned(valueText, maximum->second, value))
    {
      ErrorLog("Input script '%s' line %u: unknown control, invalid integer/range, or frame outside 1..RunFrames.", path.c_str(), lineNumber);
      return false;
    }
    if (s_inputScriptEvents.size() >= 100000)
    {
      ErrorLog("Input script '%s' exceeds 100000 events.", path.c_str());
      return false;
    }
    s_inputScriptEvents.push_back({ frame, control, uint16_t(value) });
  }
  if (file.bad())
  {
    ErrorLog("Failed while reading input script '%s'.", path.c_str());
    return false;
  }
  std::sort(s_inputScriptEvents.begin(), s_inputScriptEvents.end(), [](const DiagnosticInputEvent &a, const DiagnosticInputEvent &b) {
    return a.frame < b.frame || (a.frame == b.frame && a.control < b.control);
  });
  for (size_t i = 1; i < s_inputScriptEvents.size(); i++)
  {
    const auto &previous = s_inputScriptEvents[i - 1];
    const auto &current = s_inputScriptEvents[i];
    if (previous.frame == current.frame && previous.control == current.control)
    {
      ErrorLog("Input script '%s' has duplicate control '%s' at frame %u.", path.c_str(), current.control.c_str(), current.frame);
      return false;
    }
  }
  InfoLog("Loaded %llu diagnostic input events from '%s'; unspecified Harley controls neutral; events hold until changed.",
          (unsigned long long)s_inputScriptEvents.size(), path.c_str());
  return true;
}

static bool PrepareDiagnosticRun(Util::Config::Node &config, const Game &game)
{
  if (config["DiagnosticHidden"].ValueAs<bool>()) {
    const auto frames=config["RunFrames"].ValueAs<unsigned>();
    if (!frames || frames>18000 || config["SoundVolume"].ValueAs<int>()!=0 ||
        config["MusicVolume"].ValueAs<int>()!=0 || !config["EmulateSound"].ValueAs<bool>() ||
        !config["EmulateDSB"].ValueAs<bool>() || config["FullScreen"].ValueAs<bool>()) {
      ErrorLog("DiagnosticHidden requires 1..18000 frames, a windowed run, both volumes zero and both audio emulators enabled.");
      return false;
    }
  }
  if (!ValidateDiagnosticNumbers(config, false))
    return false;
  if (!ReadDiagnosticInputScript(config, game))
    return false;
  const bool headset = config["OpenXR"].ValueAs<bool>();
  const std::string headsetTest = config["HeadsetRenderTest"].ValueAs<std::string>();
  if (config["BodyInteraction"].ValueAs<bool>() &&
      ((game.name!="harley" && game.name!="harleyb") || !config["New3DEngine"].ValueAs<bool>())) {
    ErrorLog("Body interaction requires Harley and the new renderer."); return false;
  }
  if (config["BodyRenderTest"].ValueAs<bool>() &&
      (!config["BodyInteraction"].ValueAs<bool>() || headset || headsetTest.empty() || config["HeadsetTestPerformance"].ValueAs<bool>())) {
    ErrorLog("BodyRenderTest requires the bounded off-headset renderer test with body interaction enabled."); return false;
  }
  if (!config["StereoDuplicateAudit"].ValueAs<std::string>().empty() &&
      (headset || headsetTest.empty() || config["HeadsetTestPerformance"].ValueAs<bool>() ||
       !config["RunFrames"].ValueAs<unsigned>() || config["RunFrames"].ValueAs<unsigned>() > 600)) {
    ErrorLog("StereoDuplicateAudit requires a bounded off-headset renderer correctness test, never a live/performance run.");
    return false;
  }
  const std::string headsetWorker = config["HeadsetWorkerTest"].ValueAs<std::string>();
  if (!headsetWorker.empty()) {
    if (headset || !headsetTest.empty() || !config["RunFrames"].ValueAs<unsigned>() ||
        config["RunFrames"].ValueAs<unsigned>() > 600 || config["CaptureFrame"].ValueAs<unsigned>() ||
        !config["PresentationTest"].ValueAs<std::string>().empty() ||
        !config["FrameProfile"].ValueAs<std::string>().empty() ||
        !config["StereoCameraTrace"].ValueAs<std::string>().empty() ||
        !config["PresentationProfile"].ValueAs<std::string>().empty() ||
        config["ProfileThreaded"].ValueAs<bool>() || config["MouseLookTest"].ValueAs<bool>() ||
        !config["CaptureState"].ValueAs<std::string>().empty() ||
        (game.name != "harley" && game.name != "harleyb")) {
      ErrorLog("Headset worker diagnostic requires an isolated off-headset Harley run of 1..600 frames."); return false;
    }
    config.Set("DesktopPresentHz", 0.0f); config.Set("MouseLook", false);
    config.Set("StereoDiagnostic", true); config.Set("StereoUI", true);
    config.Set("StereoTheater", true); config.Set("StereoMonoPreview", true);
    config.Set("New3DEngine", true); config.Set("Supersampling", 1);
    config.Set("VSync", false); config.Set("FullScreen", false); config.Set("Throttle", false);
    config.Set("MultiThreaded", true); config.Set("GPUMultiThreaded", true);
    InfoLog("Off-headset worker diagnostic: real native workers, synchronous audio, simulated runtime timing.");
  }
  if (config["HeadsetTestPerformance"].ValueAs<bool>() && (headset || headsetTest.empty())) {
    ErrorLog("HeadsetTestPerformance requires an off-headset HeadsetRenderTest."); return false;
  }
  if (config["HeadsetTestCompact"].ValueAs<bool>() &&
      (headset || headsetTest.empty() || config["HeadsetTestPerformance"].ValueAs<bool>() || config["BodyRenderTest"].ValueAs<bool>())) {
    ErrorLog("HeadsetTestCompact requires the isolated off-headset renderer diagnostic."); return false;
  }
  if (headset || !headsetTest.empty()) {
#ifndef SUPERMODEL_WIN32
    if (headset) { ErrorLog("The OpenXR session backend currently supports Windows only."); return false; }
#endif
    if ((game.name != "harley" && game.name != "harleyb") ||
        config["CaptureFrame"].ValueAs<unsigned>() || !config["CaptureState"].ValueAs<std::string>().empty() ||
        !config["PresentationTest"].ValueAs<std::string>().empty() || config["MouseLookTest"].ValueAs<bool>() ||
        !config["PresentationProfile"].ValueAs<std::string>().empty() || !config["FrameProfile"].ValueAs<std::string>().empty() ||
        config["ProfileThreaded"].ValueAs<bool>() || (headset && !headsetTest.empty())) {
      ErrorLog("OpenXR/headset rendering tests require Harley and cannot combine with desktop capture/profiling tests or each other.");
      return false;
    }
    config.Set("DesktopPresentHz", 0.0f);
    config.Set("MouseLook", false);
    config.Set("StereoDiagnostic", true);
    config.Set("StereoUI", true);
    config.Set("StereoTheater", true);
    config.Set("StereoTheaterIntro", true);
    config.Set("StereoMonoPreview", true);
    config.Set("New3DEngine", true);
    config.Set("Supersampling", 1);
    config.Set("VSync", false);
    config.Set("FullScreen", false);
    if (headset) {
      config.Set("MultiThreaded", true);
      config.Set("GPUMultiThreaded", true);
      config.Set("RefreshRate", float(DesktopPresentationClock::NativeHz));
      config.Set("Throttle", false);
      InfoLog("OpenXR owns headset frame pacing. Native simulation remains capped; monitor mirror is optional.");
    } else if (!config["RunFrames"].ValueAs<unsigned>() ||
               config["HeadsetRenderTestFrame"].ValueAs<unsigned>() > config["RunFrames"].ValueAs<unsigned>()) {
      ErrorLog("HeadsetRenderTest requires a bounded RunFrames containing HeadsetRenderTestFrame."); return false;
    }
  }
  const bool independentPresentation = config["DesktopPresentHz"].ValueAs<double>() > 0;
  const std::string presentationProfile = config["PresentationProfile"].ValueAs<std::string>();
  if (independentPresentation) {
    if (config["CaptureFrame"].ValueAs<unsigned>() || !config["CaptureState"].ValueAs<std::string>().empty() ||
        !config["StereoCameraTrace"].ValueAs<std::string>().empty() ||
        !config["PresentationTest"].ValueAs<std::string>().empty() || config["MouseLookTest"].ValueAs<bool>() ||
        !config["FrameProfile"].ValueAs<std::string>().empty() || config["ProfileThreaded"].ValueAs<bool>()) {
      ErrorLog("Independent desktop presentation cannot combine with capture, trace, presentation-test, mouse-look-test or frame-profile diagnostics.");
      return false;
    }
    config.Set("StereoDiagnostic", true);
    config.Set("New3DEngine", true);
    config.Set("MultiThreaded", true);
    config.Set("GPUMultiThreaded", true);
    config.Set("Throttle", true);
    config.Set("VSync", false);
    config.Set("RefreshRate", float(DesktopPresentationClock::NativeHz));
    InfoLog("Independent desktop presentation target %.3f Hz; native game clock remains %.6f Hz. No headset session.",
      config["DesktopPresentHz"].ValueAs<double>(), DesktopPresentationClock::NativeHz);
  }
  if (!presentationProfile.empty()) {
    const uint64_t start = config["FrameProfileStart"].ValueAs<unsigned>();
    const uint64_t count = config["FrameProfileFrames"].ValueAs<unsigned>();
    const uint64_t frames = config["RunFrames"].ValueAs<unsigned>();
    if (!independentPresentation || !frames || start + count - 1 > frames) {
      ErrorLog("PresentationProfile requires DesktopPresentHz and a bounded RunFrames containing profile-start/profile-frames.");
      return false;
    }
  }
  if (config["MouseLookTest"].ValueAs<bool>()) {
    if (!config["RunFrames"].ValueAs<unsigned>() || config["ProfileThreaded"].ValueAs<bool>()) {
      ErrorLog("MouseLookTest requires a bounded single-threaded run.");
      return false;
    }
    config.Set("MouseLook", true);
  }
  if (config["MouseLook"].ValueAs<bool>()) {
    const std::string input = config["InputSystem"].ValueAs<std::string>();
    if (input != "sdl" && input != "sdlgamepad") {
      ErrorLog("Mouse look requires -input-system=sdl or sdlgamepad.");
      return false;
    }
    config.Set("StereoDiagnostic", true);
  }
  if (config["StereoMonoPreview"].ValueAs<bool>()) config.Set("StereoDiagnostic", true);
  if (config["StereoTheater"].ValueAs<bool>() || config["StereoTheaterIntro"].ValueAs<bool>()) {
    config.Set("StereoTheater", true);
    config.Set("StereoTheaterIntro", true); // Legacy settings select the same complete theater policy.
    config.Set("StereoFlatIntro", true);
    config.Set("StereoUI", true);
  }
  if (config["StereoBikeHUD"].ValueAs<bool>()) config.Set("StereoVRHUD", true);
  if (config["StereoVRHUD"].ValueAs<bool>() || config["StereoHideHUD"].ValueAs<bool>()) config.Set("StereoUI", true);
  if (config["StereoFlatIntro"].ValueAs<bool>()) config.Set("StereoMenuComfort", true);
  if (config["StereoMenuComfort"].ValueAs<bool>()) config.Set("StereoUI", true);
  if (config["StereoUI"].ValueAs<bool>() || config["StereoCinematicVignette"].ValueAs<bool>())
    config.Set("StereoDiagnostic", true);
  if (config["StereoStableChase"].ValueAs<bool>()) {
    config.Set("StereoRiderAnchor", true);
    config.Set("StereoRiderComfort", true);
    config.Set("StereoCullBypass", true);
  }
  if (config["StereoFullBike"].ValueAs<bool>()) {
    config.Set("StereoRiderAnchor", true);
    config.Set("StereoCullBypass", true);
  }
  if (config["StereoLodDistance"].ValueAs<float>() > 1.0f) {
    config.Set("StereoDiagnostic", true);
    config.Set("StereoCullBypass", true);
  }
  if (config["StereoRiderComfort"].ValueAs<bool>())
    config.Set("StereoRiderAnchor", true);
  if (config["StereoRiderAnchor"].ValueAs<bool>())
    config.Set("StereoHorizonLock", true);
  if (config["StereoHorizonLock"].ValueAs<bool>())
    config.Set("StereoDiagnostic", true);
  if (config["StereoDiagnostic"].ValueAs<bool>())
  {
    config.Set("New3DEngine", true);
    InfoLog("Stereo rendering enabled: native UI layers=%d; OpenXR session requested=%d.", int(config["StereoUI"].ValueAs<bool>()), int(headset));
  }
  const unsigned runFrames = config["RunFrames"].ValueAs<unsigned>();
  const unsigned captureFrame = config["CaptureFrame"].ValueAs<unsigned>();
  const unsigned cacheStress = config["StereoROMCacheStress"].ValueAs<unsigned>();
  const bool cacheStressBypass = config["StereoROMCacheStressBypass"].ValueAs<bool>();
  if (cacheStress && (!runFrames || cacheStress > runFrames)) {
    ErrorLog("ROM cache stress requires a positive -run-frames containing the requested stress frame.");
    return false;
  }
  if (cacheStressBypass && (!cacheStress || !config["StereoDiagnostic"].ValueAs<bool>() || !config["StereoCullBypass"].ValueAs<bool>())) {
    ErrorLog("ROM cache bypass stress requires a bounded stress frame and stereo culling bypass.");
    return false;
  }
  if (cacheStress) config.Set("New3DEngine", true);
  const std::string captureState = config["CaptureState"].ValueAs<std::string>();
  const std::string tracePath = config["StereoCameraTrace"].ValueAs<std::string>();
  if (config["StereoCameraTraceAllModels"].ValueAs<bool>() &&
      (tracePath.empty() || config["StereoCameraTraceFrames"].ValueAs<unsigned>() > 8)) {
    ErrorLog("Complete model motion traces require a named trace limited to eight native frames."); return false;
  }
  if (!tracePath.empty())
  {
    if (runFrames == 0 || config["StereoCameraTraceStart"].ValueAs<unsigned>() > runFrames)
    {
      ErrorLog("StereoCameraTrace requires RunFrames and a trace start within that run.");
      return false;
    }
    config.Set("New3DEngine", true);
  }
  const std::string profilePath = config["FrameProfile"].ValueAs<std::string>();
  const bool profileThreaded = config["ProfileThreaded"].ValueAs<bool>();
  const std::string presentationTest = config["PresentationTest"].ValueAs<std::string>();
  if (!presentationTest.empty() && (!runFrames || config["PresentationTestFrame"].ValueAs<unsigned>() > runFrames ||
      !config["StereoDiagnostic"].ValueAs<bool>() || profileThreaded ||
      config["StereoYaw"].ValueAs<float>() != 0 || config["StereoPitch"].ValueAs<float>() != 0)) {
    ErrorLog("PresentationTest requires bounded single-threaded stereo, a frame within the run, and zero configured yaw/pitch.");
    return false;
  }
  if (!profilePath.empty())
  {
    const uint64_t start = config["FrameProfileStart"].ValueAs<unsigned>();
    const uint64_t count = config["FrameProfileFrames"].ValueAs<unsigned>();
    if (!runFrames || start + count - 1 > runFrames) {
      ErrorLog("FrameProfile requires a bounded RunFrames containing the full sample range.");
      return false;
    }
    config.Set("New3DEngine", true);
  }
  if (profileThreaded && (profilePath.empty() || !runFrames || captureFrame ||
      !captureState.empty() || !tracePath.empty() || !config["InputScript"].ValueAs<std::string>().empty())) {
    ErrorLog("ProfileThreaded requires a bounded FrameProfile with no captures, input scripts, or camera trace.");
    return false;
  }
  if ((runFrames != 0 || captureFrame != 0) && !profileThreaded && !independentPresentation && !headset && headsetWorker.empty())
  {
    config.Set("MultiThreaded", false);
    config.Set("GPUMultiThreaded", false);
    InfoLog("Bounded/capture mode: forced single-threading; frame 1 is the first RunFrame after reset/load.");
  }
  if (runFrames != 0 && captureFrame > runFrames)
  {
    ErrorLog("CaptureFrame must not exceed RunFrames.");
    return false;
  }
  if (captureFrame == 0)
  {
    if (!captureState.empty())
    {
      ErrorLog("CaptureState requires a positive CaptureFrame.");
      return false;
    }
  }
  const std::string prefix = config["CapturePrefix"].ValueAs<std::string>();
  if (captureFrame != 0 && prefix.empty())
  {
    ErrorLog("CapturePrefix cannot be empty.");
    return false;
  }
  std::vector<std::string> outputs;
  if (!headsetWorker.empty()) {
    outputs.push_back(headsetWorker + ".json");
    outputs.push_back(headsetWorker + ".final.st0");
  }
  if (!headsetTest.empty()) {
    outputs.push_back(headsetTest + ".json");
    outputs.push_back(headsetTest + ".before.st0");
    outputs.push_back(headsetTest + ".after.st0");
    if (!config["HeadsetTestPerformance"].ValueAs<bool>())
      for (const char* pose : {"zero_ipd", "stereo", "yaw_pitch", "roll", "lean", "behind", "returned", "asymmetric"})
        for (const char* eye : {"left", "right"}) outputs.push_back(headsetTest + "." + pose + "." + eye + ".bmp");
  }
  if (!presentationProfile.empty()) outputs.push_back(presentationProfile);
  if (!presentationTest.empty()) {
    outputs.push_back(presentationTest + ".jsonl");
    outputs.push_back(presentationTest + "-before.st0");
    outputs.push_back(presentationTest + "-after.st0");
    for (const char* name : s_presentationNames) {
      outputs.push_back(presentationTest + "-" + name + ".bmp");
      outputs.push_back(presentationTest + "-" + name + ".txt");
    }
    if (config["StereoUI"].ValueAs<bool>()) for (const char* name : { "cinema-off", "cinema-on", "cinema-restored" }) {
      outputs.push_back(presentationTest + "-" + name + ".bmp");
      outputs.push_back(presentationTest + "-" + name + ".txt");
    }
  }
  if (!profilePath.empty()) {
    outputs.push_back(profilePath + ".cpu.jsonl");
    outputs.push_back(profilePath + ".render.jsonl");
  }
  if (captureFrame != 0)
  {
    outputs.push_back(prefix + ".bmp");
    outputs.push_back(prefix + ".txt");
  }
  if (!captureState.empty())
    outputs.push_back(captureState);
  if (!tracePath.empty())
    outputs.push_back(tracePath);
  std::vector<std::string> normalizedOutputs;
  for (const std::string &output : outputs)
  {
    std::error_code error;
    const std::filesystem::path path(output);
    const bool exists = std::filesystem::exists(path, error);
    if (exists || error)
    {
      ErrorLog("Capture output '%s' exists or cannot be inspected; choose a new output path.", output.c_str());
      return false;
    }
    auto absolute = std::filesystem::absolute(path, error);
    if (error)
    {
      ErrorLog("Invalid capture output path '%s': %s", output.c_str(), error.message().c_str());
      return false;
    }
    std::string normalized = absolute.lexically_normal().generic_string();
#ifdef SUPERMODEL_WIN32
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return char(std::tolower(c)); });
#endif
    if (std::find(normalizedOutputs.begin(), normalizedOutputs.end(), normalized) != normalizedOutputs.end())
    {
      ErrorLog("Capture bitmap, metadata, state, and camera trace must use different paths.");
      return false;
    }
    normalizedOutputs.push_back(normalized);
    if (!path.parent_path().empty())
      std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
      ErrorLog("Unable to create capture directory for '%s': %s", output.c_str(), error.message().c_str());
      return false;
    }
  }
  return true;
}

static ParsedCommandLine ParseCommandLine(int argc, char **argv)
{
  ParsedCommandLine cmd_line;
  static const std::map<std::string, std::string> valued_options
  { // -option=value
    { "-user-menu",             "UserMenu"                },
    { "-user-menu-start",       "UserMenuStart"           },
    { "-user-menu-restart",     "UserMenuRestart"         },
    { "-user-menu-calibrate",   "UserMenuCalibrate"       },
    { "-user-menu-background",  "UserMenuBackground"      },
    { "-user-comfort-strength", "UserComfortStrength"     },
    { "-game-xml-file",         "GameXMLFile"             },
    { "-load-state",            "InitStateFile"           },
    { "-run-frames",            "RunFrames"               },
    { "-presentation-test",     "PresentationTest"        },
    { "-presentation-test-frame", "PresentationTestFrame" },
    { "-mouse-look-sensitivity", "MouseLookSensitivity" },
    { "-frame-profile",         "FrameProfile"            },
    { "-present-hz",            "DesktopPresentHz"        },
    { "-xr-scale",              "OpenXRResolutionScale"   },
    { "-xr-array-swapchain",    "OpenXRArraySwapchain"    },
    { "-xr-late-worker-present", "OpenXRLateWorkerPresent" },
    { "-xr-pre-prepare-present", "OpenXRPrePreparePresent" },
    { "-xr-ready-wake", "OpenXRReadyWake" },
    { "-xr-visibility-mask", "OpenXRVisibilityMask" },
    { "-xr-scene-interpolation", "HeadsetSceneInterpolation" },
    { "-xr-owned-hud-state",    "HeadsetOwnedHUDState"     },
    { "-xr-hud-flush",          "HeadsetHUDFlush"          },
    { "-body-interaction",      "BodyInteraction"          },
    { "-body-test",             "BodyRenderTest"           },
    { "-xr-world-scale",        "OpenXRUnitsPerMeter"     },
    { "-xr-render-test",        "HeadsetRenderTest"       },
    { "-xr-render-test-frame",  "HeadsetRenderTestFrame"  },
    { "-xr-worker-test",        "HeadsetWorkerTest"       },
    { "-presentation-profile",  "PresentationProfile"     },
    { "-profile-start",         "FrameProfileStart"       },
    { "-profile-frames",        "FrameProfileFrames"      },
    { "-capture-frame",         "CaptureFrame"            },
    { "-capture-prefix",        "CapturePrefix"           },
    { "-capture-state",         "CaptureState"            },
    { "-input-script",          "InputScript"             },
    { "-stereo-horizon-fov",     "StereoHorizonFov"        },
    { "-stereo-theater-size",    "StereoTheaterScreenAngle" },
    { "-stereo-theater-depth",   "StereoTheaterDepth"       },
    { "-stereo-lod-distance",    "StereoLodDistance"       },
    { "-stereo-chase-height",    "StereoChaseHeight"       },
    { "-stereo-chase-back",      "StereoChaseBack"         },
    { "-stereo-rider-up",        "StereoRiderUp"            },
    { "-stereo-rider-back",      "StereoRiderBack"          },
    { "-stereo-vignette-scale",  "StereoVignetteScale"      },
    { "-stereo-camera-trace",    "StereoCameraTrace"       },
    { "-stereo-duplicate-audit", "StereoDuplicateAudit"    },
    { "-stereo-camera-trace-start", "StereoCameraTraceStart" },
    { "-stereo-camera-trace-frames", "StereoCameraTraceFrames" },
    { "-stereo-separation",     "StereoEyeSeparation"     },
    { "-stereo-yaw",            "StereoYaw"               },
    { "-stereo-pitch",          "StereoPitch"             },
    { "-ppc-frequency",         "PowerPCFrequency"        },
    { "-crosshairs",            "Crosshairs"              },
    { "-crosshair-style",       "CrosshairStyle"          },
    { "-vert-shader",           "VertexShader"            },
    { "-frag-shader",           "FragmentShader"          },
    { "-sound-volume",          "SoundVolume"             },
    { "-music-volume",          "MusicVolume"             },
    { "-harley-music-level",    "HarleyMusicLevel"         },
    { "-stereo-rom-cache-stress", "StereoROMCacheStress"   },
    { "-balance",               "Balance"                 },
    { "-channels", 	            "NbSoundChannels"         },
    { "-soundfreq",             "SoundFreq"               },
    { "-input-system",          "InputSystem"             },
    { "-outputs",               "Outputs"                 },
    { "-log-output",            "LogOutput"               },
    { "-log-level",             "LogLevel"                }
  };
  static const std::map<std::string, std::pair<std::string, bool>> bool_options
  { // -option
    { "-threads",             { "MultiThreaded",    true } },
    { "-no-threads",          { "MultiThreaded",    false } },
    { "-gpu-multi-threaded",  { "GPUMultiThreaded", true } },
    { "-no-gpu-thread",       { "GPUMultiThreaded", false } },
    { "-idle-skip",           { "IdleSkip",         true } },
    { "-no-idle-skip",        { "IdleSkip",         false } },
    { "-window",              { "FullScreen",       false } },
    { "-fullscreen",          { "FullScreen",       true } },
    { "-borderless",          { "BorderlessWindow", true } },
    { "-no-wide-screen",      { "WideScreen",       false } },
    { "-wide-screen",         { "WideScreen",       true } },
    { "-stretch",             { "Stretch",          true } },
    { "-no-stretch",          { "Stretch",          false } },
    { "-wide-bg",             { "WideBackground",   true } },
    { "-no-wide-bg",          { "WideBackground",   false } },
#ifndef SUPERMODEL_OSX
    { "-no-multi-texture",    { "MultiTexture",     false } },
    { "-multi-texture",       { "MultiTexture",     true } },
#endif
    { "-throttle",            { "Throttle",         true } },
    { "-no-throttle",         { "Throttle",         false } },
    { "-vsync",               { "VSync",            true } },
    { "-no-vsync",            { "VSync",            false } },
    { "-show-fps",            { "ShowFrameRate",    true } },
    { "-no-fps",              { "ShowFrameRate",    false } },
    { "-new3d",               { "New3DEngine",      true } },
    { "-openxr",              { "OpenXR",          true } },
    { "-no-openxr",           { "OpenXR",          false } },
    { "-xr-mirror",           { "OpenXRMirror",    true } },
    { "-no-xr-mirror",        { "OpenXRMirror",    false } },
    { "-xr-test-theater",     { "HeadsetTestTheater", true } },
    { "-xr-test-performance", { "HeadsetTestPerformance", true } },
    { "-xr-test-compact",     { "HeadsetTestCompact", true } },
    { "-xr-worker-enabled",   { "HeadsetWorkerEnabled", true } },
    { "-stereo-diagnostic",   { "StereoDiagnostic", true } },
    { "-stereo-camera-trace-all-models", { "StereoCameraTraceAllModels", true } },
    { "-mouse-look",          { "MouseLook", true } },
    { "-mouse-look-test",     { "MouseLookTest", true } },
    { "-stereo-mono-preview", { "StereoMonoPreview", true } },
    { "-profile-threaded",    { "ProfileThreaded", true } },
    { "-stereo-cull-bypass",  { "StereoCullBypass", true } },
    { "-stereo-stable-chase", { "StereoStableChase", true } },
    { "-no-stereo-stable-chase", { "StereoStableChase", false } },
    { "-stereo-full-bike",    { "StereoFullBike", true } },
    { "-no-stereo-full-bike", { "StereoFullBike", false } },
    { "-stereo-ui",           { "StereoUI", true } },
    { "-stereo-menu-comfort", { "StereoMenuComfort", true } },
    { "-stereo-flat-intro", { "StereoFlatIntro", true } },
    { "-no-stereo-flat-intro", { "StereoFlatIntro", false } },
    { "-stereo-theater-intro", { "StereoTheaterIntro", true } },
    { "-no-stereo-theater-intro", { "StereoTheaterIntro", false } },
    { "-stereo-theater", { "StereoTheater", true } },
    { "-diagnostic-hidden", { "DiagnosticHidden", true } },
    { "-no-stereo-theater", { "StereoTheater", false } },
    { "-stereo-vr-hud", { "StereoVRHUD", true } },
    { "-stereo-bike-hud", { "StereoBikeHUD", true } },
    { "-no-stereo-bike-hud", { "StereoBikeHUD", false } },
    { "-no-stereo-vr-hud", { "StereoVRHUD", false } },
    { "-stereo-hide-hud", { "StereoHideHUD", true } },
    { "-stereo-rom-cache-stress-bypass", { "StereoROMCacheStressBypass", true } },
    { "-no-stereo-hide-hud", { "StereoHideHUD", false } },
    { "-harley-free-ride", { "HarleyFreeRide", true } },
    { "-no-harley-free-ride", { "HarleyFreeRide", false } },
    { "-no-stereo-menu-comfort", { "StereoMenuComfort", false } },
    { "-stereo-cinematic-vignette", { "StereoCinematicVignette", true } },
    { "-no-stereo-cinematic-vignette", { "StereoCinematicVignette", false } },
    { "-stereo-horizon-lock", { "StereoHorizonLock", true } },
    { "-stereo-rider-anchor", { "StereoRiderAnchor", true } },
    { "-stereo-rider-comfort", { "StereoRiderComfort", true } },
    { "-quad-rendering",      { "QuadRendering",    true } },
#ifndef SUPERMODEL_OSX
    { "-legacy3d",            { "New3DEngine",      false } },
#endif
    { "-no-flip-stereo",      { "FlipStereo",       false } },
    { "-flip-stereo",         { "FlipStereo",       true } },
    { "-sound",               { "EmulateSound",     true } },
    { "-no-sound",            { "EmulateSound",     false } },
    { "-dsb",                 { "EmulateDSB",       true } },
    { "-no-dsb",              { "EmulateDSB",       false } },
    { "-legacy-scsp",         { "LegacySoundDSP",   true } },
    { "-new-scsp",            { "LegacySoundDSP",   false } },
    { "-no-white-flash",      { "NoWhiteFlash",     true } },
    { "-white-flash",         { "NoWhiteFlash",     false } },
    { "-net",                 { "Network",          true } },
    { "-no-net",              { "Network",          false } },
    { "-simulate-netboard",   { "SimulateNet",      true } },
    { "-emulate-netboard",    { "SimulateNet",      false } },
    { "-no-force-feedback",   { "ForceFeedback",    false } },
    { "-force-feedback",      { "ForceFeedback",    true } },
    { "-dump-memory",         { "DumpMemory",       true } },
    { "-dump-textures",       { "DumpTextures",     true } },
  };
  for (int i = 1; i < argc; i++)
  {
    std::string arg(argv[i]);
    if (arg[0] == '-')
    {
      // First, check maps
      size_t idx_equals = arg.find_first_of('=');
      if (idx_equals != std::string::npos)
      {
        std::string option(arg.begin(), arg.begin() + idx_equals);
        std::string value(arg.begin() + idx_equals + 1, arg.end());
        if (value.empty())
        {
          ErrorLog("Argument to '%s' cannot be blank.", option.c_str());
          cmd_line.error = true;
          continue;
        }
        auto it = valued_options.find(option);
        if (it != valued_options.end())
        {
          const std::string &config_key = it->second;
          cmd_line.config.Set(config_key, value);
          continue;
        }
      }
      else
      {
        auto it = bool_options.find(arg);
        if (it != bool_options.end())
        {
          const std::string &config_key = it->second.first;
          bool value = it->second.second;
          cmd_line.config.Set(config_key, value);
          // Both spellings are aliases. An explicit disable must also override
          // the other spelling in an INI/launcher, with the last CLI flag winning.
          if (config_key == "StereoTheater" || config_key == "StereoTheaterIntro") {
            cmd_line.config.Set("StereoTheater", value);
            cmd_line.config.Set("StereoTheaterIntro", value);
          }
          continue;
        }
        else if (valued_options.find(arg) != valued_options.end())
        {
          ErrorLog("'%s' requires an argument.", argv[i]);
          cmd_line.error = true;
          continue;
        }
      }
      // Fell through -- handle special cases
      if (arg == "-?" || arg == "-h" || arg == "-help" || arg == "--help")
        cmd_line.print_help = true;
      else if (arg == "-print-games")
        cmd_line.print_games = true;
      else if (arg == "-res" || arg.find("-res=") == 0)
      {
        std::vector<std::string> parts = Util::Format(arg).Split('=');
        if (parts.size() != 2)
        {ErrorLog("'-res' requires both a width and height (e.g., '-res=496,384').");
          cmd_line.error = true;
        }
        else
        {
          unsigned  x, y;
          if (2 == sscanf(&argv[i][4],"=%u,%u", &x, &y))
          {
            std::string xres = Util::Format() << x;
            std::string yres = Util::Format() << y;
            cmd_line.config.Set("XResolution", xres);
            cmd_line.config.Set("YResolution", yres);
          }
          else
          {
            ErrorLog("'-res' requires both a width and height (e.g., '-res=496,384').");
            cmd_line.error = true;
          }
        }
      }
      else if (arg == "-window-pos" || arg.find("-window-pos=") == 0)
      {
          std::vector<std::string> parts = Util::Format(arg).Split('=');
          if (parts.size() != 2)
          {
              ErrorLog("'-window-pos' requires both an X and Y position (e.g., '-window-pos=10,0').");
              cmd_line.error = true;
          }
          else
          {
              int xpos, ypos;
              if (2 == sscanf(&argv[i][11], "=%d,%d", &xpos, &ypos))
              {
                  cmd_line.config.Set("WindowXPosition", xpos);
                  cmd_line.config.Set("WindowYPosition", ypos);
              }
              else
              {
                  ErrorLog("'-window-pos' requires both an X and Y position (e.g., '-window-pos=10,0').");
                  cmd_line.error = true;
              }
          }
      }
      else if (arg == "-ss" || arg.find("-ss=") == 0) {

          std::vector<std::string> parts = Util::Format(arg).Split('=');

          if (parts.size() != 2)
          {
              ErrorLog("'-ss' requires an integer argument (e.g., '-ss=2').");
              cmd_line.error = true;
          }
          else {

              try {
                  int val = std::stoi(parts[1]);
                  val = std::clamp(val, 1, 8);

                  cmd_line.config.Set("Supersampling", val);
              }
              catch (...) {
                  ErrorLog("'-ss' requires an integer argument (e.g., '-ss=2').");
                  cmd_line.error = true;
              }
          }
      }
      else if (arg == "-crtcolors" || arg.find("-crtcolors=") == 0) {

          std::vector<std::string> parts = Util::Format(arg).Split('=');

          if (parts.size() != 2)
          {
              ErrorLog("'-crtcolors' requires an integer argument (e.g., '-crtcolors=1').");
              cmd_line.error = true;
          }
          else {

              try {
                  int val = std::stoi(parts[1]);
                  val = std::clamp(val, 0, 5);

                  cmd_line.config.Set("CRTcolors", val);
              }
              catch (...) {
                  ErrorLog("'-crtcolors' requires an integer argument (e.g., '-crtcolors=1').");
                  cmd_line.error = true;
              }
          }
      }
      else if (arg == "-upscalemode" || arg.find("-upscalemode=") == 0) {

          std::vector<std::string> parts = Util::Format(arg).Split('=');

          if (parts.size() != 2)
          {
              ErrorLog("'-upscalemode' requires an integer argument (e.g., '-upscalemode=1').");
              cmd_line.error = true;
          }
          else {

              try {
                  int val = std::stoi(parts[1]);
                  val = std::clamp(val, 0, 3);

                  cmd_line.config.Set("UpscaleMode", val);
              }
              catch (...) {
                  ErrorLog("'-upscalemode' requires an integer argument (e.g., '-upscalemode=1').");
                  cmd_line.error = true;
              }
          }
      }
      else if (arg == "-true-hz")
        cmd_line.config.Set("RefreshRate", 57.524f);
      else if (arg == "-print-gl-info")
        cmd_line.print_gl_info = true;
      else if (arg == "-config-inputs")
        cmd_line.config_inputs = true;
      else if (arg == "-print-inputs")
        cmd_line.print_inputs = true;
#ifdef SUPERMODEL_DEBUGGER
      else if (arg == "-disable-debugger")
        cmd_line.disable_debugger = true;
      else if (arg == "-enter-debugger")
        cmd_line.enter_debugger = true;
#endif
#ifdef DEBUG
      else if (arg == "-gfx-state" || arg.find("-gfx-state=") == 0)
      {
        std::vector<std::string> parts = Util::Format(arg).Split('=');
        if (parts.size() != 2)
        {
          ErrorLog("'-gfx-state' requires a file name.");
          cmd_line.error = true;
        }
        else
          cmd_line.gfx_state = parts[1];
      }
#endif
      else
      {
        ErrorLog("Ignoring unrecognized option: %s", argv[i]);
        cmd_line.error = true;
      }
    }
    else
      cmd_line.rom_files.emplace_back(arg);
  }
  if (!ValidateDiagnosticNumbers(cmd_line.config, true))
    cmd_line.error = true;
  return cmd_line;
}

/*
 * main(argc, argv):
 *
 * Program entry point.
 */
int main(int argc, char **argv)
{
  Title();
  WriteDefaultConfigurationFileIfNotPresent();

  bool loadGUI = false;
  if (argc <= 1)
  {
    //Help();
    loadGUI = true;
  }

  // Before command line is parsed, console logging only
  SetLogger(std::make_shared<CConsoleErrorLogger>());

  // Load config and parse command line
  auto cmd_line = ParseCommandLine(argc, argv);
  if (cmd_line.error)
  {
    return 1;
  }

  if (loadGUI) {
      // Load up what settings we have so far. The complete file is passed to the GUI,
      // which edits only the global section and preserves any game-specific sections.
      // Editing the game-specific sections themselves is not supported atm but we can
      // look at this later.
      Util::Config::Node fileConfig("Global");
      Util::Config::FromINIFile(&fileConfig, s_configFilePath);

      cmd_line.rom_files = RunGUI(s_configFilePath, fileConfig);

      if (cmd_line.rom_files.empty()) {
          return 0;
      }
  }

  // Create logger as specified by command line
  auto logger = CreateLogger(cmd_line.config);
  if (!logger)
  {
    ErrorLog("Unable to initialize logging system.");
    return 1;
  }
  SetLogger(logger);
  InfoLog("Supermodel Version " SUPERMODEL_VERSION);
  InfoLog("Started as:");
  for (int i = 0; i < argc; i++)
    InfoLog("  argv[%d] = %s", i, argv[i]);

  // Finish processing command line
  if (cmd_line.print_help)
  {
    Help();
    return 0;
  }
  if (cmd_line.print_gl_info)
  {
    // Use command line options so we can select the 3D engine for which to print GL info 
    Util::Config::MergeINISections(&s_runtime_config, DefaultConfig(), cmd_line.config);

    // We must exit after this because CreateGLScreen() is used
    PrintGLInfo(true, false, false);
    return 0;
  }

#ifdef DEBUG
  s_gfxStatePath.assign(cmd_line.gfx_state);
#endif
  bool print_games = cmd_line.print_games;
  bool rom_specified = !cmd_line.rom_files.empty();
  if (!rom_specified && !print_games && !cmd_line.config_inputs && !cmd_line.print_inputs)
  {
    ErrorLog("No ROM file specified.");
    return 0;
  }

  // Load game and resolve run-time config
  Game game;
  ROMSet rom_set;
  
  Util::Config::Node fileConfig("Global");
  {
    Util::Config::Node fileConfigWithDefaults("Global");
    Util::Config::Node config3("Global");
    Util::Config::Node config4("Global");
    Util::Config::FromINIFile(&fileConfig, s_configFilePath);
    Util::Config::MergeINISections(&fileConfigWithDefaults, DefaultConfig(), fileConfig); // apply .ini file's global section over defaults
    Util::Config::MergeINISections(&config3, fileConfigWithDefaults, cmd_line.config);    // apply command line overrides
    if (rom_specified || print_games)
    {
      std::string xml_file = config3["GameXMLFile"].ValueAs<std::string>();
      GameLoader loader(xml_file);
      if (print_games)
      {
        PrintGameList(xml_file, loader.GetGames());
        return 0;
      }
      if (loader.Load(&game, &rom_set, *cmd_line.rom_files.begin()))
        return 1;
      Util::Config::MergeINISections(&config4, config3, fileConfig[game.name]);   // apply game-specific config
    }
    else
      config4 = config3;
    Util::Config::MergeINISections(&s_runtime_config, config4, cmd_line.config);  // apply command line overrides once more
  }
  if (!PrepareDiagnosticRun(s_runtime_config, game))
    return 1;
  LogConfig(s_runtime_config);

  // Initialize SDL (individual subsystems get initialized later)
  if (SDL_Init(0) != 0)
  {
    ErrorLog("Unable to initialize SDL: %s\n", SDL_GetError());
    return 1;
  }

  // Flag as DPI-aware, otherwise the window content might be scaled by some graphics drivers
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "system");

  // Begin initializing various subsystems...
  int exitCode = 0;
  IEmulator *Model3 = nullptr;
  std::shared_ptr<CInputSystem> InputSystem;
  CInputs *Inputs = nullptr;
  COutputs *Outputs = nullptr;
#ifdef SUPERMODEL_DEBUGGER
  std::shared_ptr<Debugger::CSupermodelDebugger> Debugger;
#endif // SUPERMODEL_DEBUGGER
  std::string selectedInputSystem = s_runtime_config["InputSystem"].ValueAs<std::string>();

  aaValue = s_runtime_config["Supersampling"].ValueAs<int>();
  CRTcolors = (CRTcolor)s_runtime_config["CRTcolors"].ValueAs<int>();

  // Create a window
  xRes = 496;
  yRes = 384;
  if (Result::OKAY != CreateGLScreen(s_runtime_config["New3DEngine"].ValueAs<bool>(), s_runtime_config["QuadRendering"].ValueAs<bool>(),"Supermodel", false, &xOffset, &yOffset, &xRes, &yRes, &totalXRes, &totalYRes, false, false))
  {
    exitCode = 1;
    goto Exit;
  }

  // Create Crosshair
  s_crosshair = new CCrosshair(s_runtime_config);
  if (s_crosshair->Init() != Result::OKAY)
  {
      ErrorLog("Unable to load bitmap crosshair texture\n");
      exitCode = 1;
      goto Exit;
  }

  // Create Model 3 emulator
#ifdef DEBUG
  Model3 = s_gfxStatePath.empty() ? static_cast<IEmulator *>(new CModel3(s_runtime_config)) : static_cast<IEmulator *>(new CModel3GraphicsState(s_runtime_config, s_gfxStatePath));
#else
  Model3 = new CModel3(s_runtime_config);
#endif

  // Create input system
  if (selectedInputSystem == "sdl")
    InputSystem = std::shared_ptr<CInputSystem>(new CSDLInputSystem(s_runtime_config, false));
  else if (selectedInputSystem == "sdlgamepad")
    InputSystem = std::shared_ptr<CInputSystem>(new CSDLInputSystem(s_runtime_config, true));
#ifdef SUPERMODEL_WIN32
  else if (selectedInputSystem == "dinput")
    InputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(s_runtime_config, s_window, false, false));
  else if (selectedInputSystem == "xinput")
    InputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(s_runtime_config, s_window, false, true));
  else if (selectedInputSystem == "rawinput")
    InputSystem = std::shared_ptr<CInputSystem>(new CDirectInputSystem(s_runtime_config, s_window, true, false));
#endif // SUPERMODEL_WIN32
  else
  {
    ErrorLog("Unknown input system: %s\n", selectedInputSystem.c_str());
    exitCode = 1;
    goto Exit;
  }

  // Create inputs from input system (configuring them if required)
  Inputs = new CInputs(InputSystem);
  if (!Inputs->Initialize())
  {
    ErrorLog("Unable to initalize inputs.\n");
    exitCode = 1;
    goto Exit;
  }

  // NOTE: fileConfig is passed so that the global section is used for input settings
  // and because this function may write out a new config file, which must preserve
  // all sections. We don't want to pollute the output with built-in defaults.
  if (ConfigureInputs(Inputs, &fileConfig, &s_runtime_config, game, cmd_line.config_inputs) != Result::OKAY)
  {
    exitCode = 1;
    goto Exit;
  }

  if (cmd_line.print_inputs)
  {
    Inputs->PrintInputs(NULL);
    InputSystem->PrintSettings();
  }

  if (!rom_specified)
    goto Exit;

  // Create outputs
  {
    std::string outputs = s_runtime_config["Outputs"].ValueAs<std::string>();
    if (outputs == "none")
      Outputs = NULL;
    else if (outputs == "net") 
    {
        CNetOutputs* netOutputs = new CNetOutputs();
        if (s_runtime_config["OutputsWithLF"].ValueAs<bool>())
            netOutputs->SetFrameEnding(std::string("\r\n"));
        netOutputs->SetTcpPort(s_runtime_config["OutputsTCPPort"].ValueAs<unsigned int>());
        netOutputs->SetUdpBroadcastPort(s_runtime_config["OutputsUDPBroadcastPort"].ValueAs<unsigned int>());
        Outputs = (COutputs*)netOutputs;
#ifdef SUPERMODEL_WIN32
    } else if (outputs == "win") {
      Outputs = new CWinOutputs();
#endif // SUPERMODEL_WIN32
    }
    else
    {
      ErrorLog("Unknown outputs: %s\n", outputs.c_str());
      exitCode = 1;
      goto Exit;
    }
  }

  // Initialize outputs
  if (Outputs != NULL && !Outputs->Initialize())
  {
    ErrorLog("Unable to initialize outputs.\n");
    exitCode = 1;
    goto Exit;
  }

#ifdef SUPERMODEL_DEBUGGER
  // Create Supermodel debugger unless debugging is disabled
  if (!cmd_line.disable_debugger)
  {
    Debugger = std::make_shared<Debugger::CSupermodelDebugger>(dynamic_cast<CModel3 *>(Model3), Inputs, logger);
    // If -enter-debugger option was set force debugger to break straightaway
    if (cmd_line.enter_debugger)
      Debugger->ForceBreak(true);
  }
  // Fire up Supermodel with debugger
  exitCode = Supermodel(game, &rom_set, Model3, Inputs, Outputs, Debugger);
#else
  // Fire up Supermodel
  exitCode = Supermodel(game, &rom_set, Model3, Inputs, Outputs);
#endif // SUPERMODEL_DEBUGGER
  delete Model3;

Exit:
  delete Inputs;
  delete Outputs;
  delete s_crosshair;
  DestroyGLScreen();
  SDL_Quit();

  if (exitCode >= 75 && exitCode <= 80)
    InfoLog("Program closed normally for the requested startup-settings restart.");
  else if (exitCode)
    InfoLog("Program terminated due to an error.");
  else
    InfoLog("Program terminated normally.");

  return exitCode;
}

