#ifndef HARLEY_MENU_INTEGRATION_H
#define HARLEY_MENU_INTEGRATION_H

#include "HarleyUserMenu.h"
#include "HarleyMenuArtwork.h"
#include "HarleyMenuPreferences.h"
#include "Graphics/New3D/SpatialPanel.h"
#include "Inputs/Inputs.h"
#include "Inputs/InputSystem.h"
#include "Util/ConfigBuilders.h"
#include "SDLIncludes.h"
#include "OSD/Audio.h"
#include "../Windows/OpenXRSession.h"
#include <filesystem>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <map>

// All methods run on the SDL/GL owner thread outside native worker execution.
// The original emulator configuration and player NVRAM are never written here.
class HarleyMenuIntegration {
public:
  HarleyMenuIntegration(Util::Config::Node& config, CInputs* inputs,
      std::string preferences, bool interactive, bool headset)
    : m_config(config), m_inputs(inputs), m_path(std::move(preferences)),
      m_interactive(interactive), m_headset(headset), m_preferences("Global")
  {
    // A muted test invocation must stay muted even if user preferences exist.
    m_soundLocked = config["SoundVolume"].ValueAs<int>() == 0;
    // Capture launch intent BEFORE loading a saved user volume of zero. A
    // player can raise that saved mute later; automated/CLI mute stays locked.
    config.Set("UserMenuAudioPreviewAllowed",interactive && !m_soundLocked);
    m_dreamRide.Initialize(config["HarleyFreeRide"].ValueAsDefault<bool>(false),
                          config["StereoHideHUD"].ValueAsDefault<bool>(false));
    DetectSteering();
    if (interactive) LoadPreferences();
    m_nextScale = config["OpenXRResolutionScale"].ValueAs<float>();
    m_initialScale = m_nextScale;
    RefreshSettings();
    LoadBackground(config["UserMenuBackground"].ValueAsDefault<std::string>(""));
  }
  HarleyMenu::UserMenu& Menu() { return m_menu; }
  bool IsOpen() const { return m_menu.IsOpen(); }
  bool ArcadeControls() const { return m_arcade; }
  bool Startup() const { return m_startup; }
  void SetContext(bool startup, bool liveCamera) {
    if (m_startup==startup && m_liveCamera==liveCamera) return;
    m_startup=startup; m_liveCamera=liveCamera; RefreshSettings();
  }
  // GetRenderTargetSize supplies the common target (maximum across both eyes),
  // not the physical panel dimensions. Pending quality never changes this.
  void SetCurrentEyeSize(unsigned width, unsigned height) {
    if(!width || !height || width>65536 || height>65536) width=height=0;
    if(m_currentEyeWidth==width && m_currentEyeHeight==height) return;
    m_currentEyeWidth=width; m_currentEyeHeight=height; RefreshSettings();
  }
  bool PendingRestart() const {
    return HarleyMenuPreferences::StartupRestartRequired(m_startup,m_nextScale,m_initialScale,m_dreamRide.Changed());
  }
  int StartupRestartCode(bool calibrate) const {
    return HarleyMenuPreferences::StartupRestartCode(calibrate,m_dreamRide.Changed(),m_dreamRide.Enabled());
  }
  void SetDreamRideSupported(bool supported) {
    m_dreamRideSupported=supported; RefreshSettings();
  }
  void SetRenderer(IRender3D* renderer) { m_renderer = renderer; }
  void LoadOriginalArtwork(const uint8_t* bankedRom, size_t size) {
    if (m_customBackground) return;
    std::vector<uint8_t> pixels;
    int width=0,height=0;
    if (HarleyMenuArtwork::DecodeOriginalRideCard(bankedRom,size,pixels,width,height) &&
        m_menu.SetBackgroundRGBA(pixels,width,height))
      InfoLog("HarleyVR menu: original three-bike artwork decoded from the loaded ROM (%dx%d).",width,height);
    else InfoLog("HarleyVR menu: original artwork unavailable for this ROM; using the built-in design.");
  }

  bool TabPressed(bool focused, bool down) {
    if (!focused) { m_tabReady=false; m_tabHeld=down; return false; }
    if (!m_tabReady) { m_tabReady=true; m_tabHeld=down; return false; }
    const bool edge=down && !m_tabHeld; m_tabHeld=down; return edge;
  }
  void Open() { RefreshSettings(); m_menu.Open(); m_releaseGate=true; Publish(); }
  void Close() {
    StopMenuSoundPreview();
    m_menu.Close(); m_releaseGate=true;
    if (m_renderer) m_renderer->SetUserMenuPanel(0,false,m_menu.Revision());
    FlushPreferences();
  }
  void Reanchor() {
    if (m_renderer && IsOpen()) {
      m_renderer->SetUserMenuPanel(0,false,m_menu.Revision());
      m_renderer->SetUserMenuPanel(m_texture,true,m_menu.Revision());
    }
  }
  void SetArcade(bool active) {
    m_arcade=active;
    m_releaseGate=true;
    m_pulse=active?Pulse::Test:Pulse::None;
    RefreshSettings();
    InfoLog(active ? "Harley arcade setup: A/Cross=TEST/select, X/Square=SERVICE/next. Choose native EXIT to save and return automatically to HarleyVR startup. L3 or left grip + R3 opens HarleyVR options."
      : "Harley arcade setup finished: normal riding controls restored; held transition buttons remain blocked until release.");
  }
  void QueueStart() { m_pulse=Pulse::Start; }
  void QueueCoin() { m_pulse=Pulse::Coin; }

  // Called once after all ordinary native game inputs are sampled, before
  // RunFrame. Held navigation/analog inputs must return neutral on menu exit.
  void FilterAndPulse(const OpenXRSession::Controls& raw, bool keyboardFocused) {
    if (m_releaseGate) {
      const auto* keys=SDL_GetKeyboardState(nullptr);
      const bool keysHeld=keyboardFocused && (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_DOWN] ||
        keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_RETURN] ||
        keys[SDL_SCANCODE_BACKSPACE] || keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_TAB]);
      const bool xrHeld=raw.select || raw.coin || raw.view || raw.music || raw.gearShiftUp ||
        raw.gearShiftDown || std::abs(raw.steering)>.2f || raw.throttle>.1f || raw.brake>.1f;
      const bool gameHeld=m_inputs->start[0]->value || m_inputs->coin[0]->value ||
        m_inputs->test[0]->value || m_inputs->service[0]->value ||
        m_inputs->gearShiftUp->value || m_inputs->gearShiftDown->value || m_inputs->viewChange->value ||
        m_inputs->musicSelect->value || m_inputs->accelerator->value || m_inputs->brake->value ||
        std::abs(int(m_inputs->steering->value)-128)>8;
      if (!keysHeld && !xrHeld && !gameHeld) m_releaseGate=false;
      // Clear this final neutral frame too; only explicit queued actions below
      // are allowed through the transition. Cabinet edges are gated too.
      m_inputs->steering->value=128; m_inputs->accelerator->value=0; m_inputs->brake->value=0;
      m_inputs->start[0]->value=m_inputs->coin[0]->value=0;
      m_inputs->test[0]->value=m_inputs->service[0]->value=0;
      m_inputs->gearShiftUp->value=m_inputs->gearShiftDown->value=0;
      m_inputs->viewChange->value=m_inputs->musicSelect->value=0;
    }
    if (m_pulse==Pulse::Start) { m_inputs->start[0]->value=1; m_inputs->accelerator->value=255; }
    else if (m_pulse==Pulse::Coin) m_inputs->coin[0]->value=1;
    else if (m_pulse==Pulse::Test) m_inputs->test[0]->value=1;
    m_pulse=Pulse::None;
  }

  void ApplySetting(HarleyMenu::Event event) {
    using A=HarleyMenu::Action;
    const auto& shown=m_menu.GetSettings();
    switch(event.action) {
      case A::ComfortEnabled:
      case A::ComfortStrength:
        m_config.Set("UserComfortStrength",shown.comfortEnabled?float(shown.comfortStrength)/100.0f:0.0f);
        Remember("UserComfortEnabled",shown.comfortEnabled?1:0);
        Remember("UserComfortPercent",shown.comfortStrength); m_rebuild=true; break;
      case A::RiderHeight:
        Remember("StereoRiderUp",float(event.value)); m_pendingCamera["StereoRiderUp"]=float(event.value); m_cameraPending=true; break;
      case A::RiderBack:
        Remember("StereoRiderBack",float(event.value)); m_pendingCamera["StereoRiderBack"]=float(event.value); m_cameraPending=true; break;
      case A::DesktopFov:
        if (!m_headset) { Remember("StereoHorizonFov",float(event.value)); m_pendingCamera["StereoHorizonFov"]=float(event.value); m_cameraPending=true; } break;
      case A::TheaterAngle:
        m_config.Set("StereoTheaterScreenAngle",float(event.value)); Remember("StereoTheaterScreenAngle",float(event.value));
        break;
      case A::TheaterDepth:
        // Reserved action: stale events cannot restore the removed depth mode.
        m_config.Set("StereoTheaterDepth",0);
        break;
      case A::Sound:
        if (!m_soundLocked) {
          m_config.Set("SoundVolume",int(event.value)); Remember("SoundVolume",int(event.value));
          PreviewMenuSound(int(event.value));
        }
        else RefreshSettings();
        break;
      case A::RenderScale:
        if (m_startup && m_headset && m_config["UserMenuRestart"].ValueAsDefault<bool>(false)) { m_nextScale=float(event.value); Remember("OpenXRResolutionScale",m_nextScale);
          InfoLog("Headset render scale %.2f saved for next launch; current eye targets unchanged.",m_nextScale); } break;
      case A::DreamRide:
        if (m_dreamRide.Request(m_startup,m_config["UserMenuRestart"].ValueAsDefault<bool>(false),
                               m_dreamRideSupported,event.value))
          InfoLog("Dream Ride %s selected for this launch; HUD and timer change together after the startup restart.",
                  m_dreamRide.Enabled()?"enabled":"disabled");
        RefreshSettings(); // Reject stale events without changing the active game or saved settings.
        break;
      case A::SteeringTravel:
        DetectSteering();
        if (!m_steeringKey.empty()) {
          const unsigned value=unsigned(std::max(double(m_minimumTravel),std::min(100.0,event.value)));
          m_config.Set(m_steeringKey,value); m_inputs->LoadFromConfig(m_config);
          Remember("SteeringAxis",m_steeringKey); Remember("SteeringTravel",value);
          RefreshSettings();
        }
        break;
      default: break;
    }
  }
  bool CommitPending() {
    if (m_cameraPending) {
      for (const auto& setting:m_pendingCamera) m_config.Set(setting.first,setting.second);
      m_pendingCamera.clear();
      m_cameraPending=false; m_rebuild=true;
    }
    const bool rebuild=m_rebuild; m_rebuild=false; return rebuild;
  }

  bool Publish() {
    if (!IsOpen()) return true;
    if (m_uploaded!=m_menu.Revision() || !m_texture) {
      GLint active=0,texture=0,unpackBuffer=0,alignment=0,rowLength=0,skipRows=0,skipPixels=0;
      glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glActiveTexture(GL_TEXTURE0);
      glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
      glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpackBuffer);
      glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment); glGetIntegerv(GL_UNPACK_ROW_LENGTH,&rowLength);
      glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skipRows); glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skipPixels);
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
      glPixelStorei(GL_UNPACK_ALIGNMENT,1); glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
      glPixelStorei(GL_UNPACK_SKIP_ROWS,0); glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
      const bool allocate=!m_texture;
      if (allocate) glGenTextures(1,&m_texture);
      glBindTexture(GL_TEXTURE_2D,m_texture);
      if (allocate) {
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,HarleyMenu::UserMenu::Width(),HarleyMenu::UserMenu::Height(),0,GL_RGBA,GL_UNSIGNED_BYTE,m_menu.Pixels().data());
      } else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,HarleyMenu::UserMenu::Width(),HarleyMenu::UserMenu::Height(),GL_RGBA,GL_UNSIGNED_BYTE,m_menu.Pixels().data());
      GLint textureWidth=HarleyMenu::UserMenu::Width(),textureHeight=HarleyMenu::UserMenu::Height();
      if (allocate) {
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&textureWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&textureHeight);
      }
      glPixelStorei(GL_UNPACK_ALIGNMENT,alignment); glPixelStorei(GL_UNPACK_ROW_LENGTH,rowLength);
      glPixelStorei(GL_UNPACK_SKIP_ROWS,skipRows); glPixelStorei(GL_UNPACK_SKIP_PIXELS,skipPixels);
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpackBuffer); glBindTexture(GL_TEXTURE_2D,texture); glActiveTexture(active);
      if (!m_texture || textureWidth!=HarleyMenu::UserMenu::Width() || textureHeight!=HarleyMenu::UserMenu::Height()) {
        ErrorLog("Unable to create HarleyVR menu texture."); return false;
      }
      m_uploaded=m_menu.Revision();
    }
    if (m_renderer) m_renderer->SetUserMenuPanel(m_texture,true,m_menu.Revision());
    return true;
  }
  void DrawDesktop(SDL_Window* window) {
    if (!IsOpen() || !m_texture) return;
    int width=0,height=0; SDL_GL_GetDrawableSize(window,&width,&height);
    if (width<=0 || height<=0) return;
    GLint target=0,buffer=0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&target); glGetIntegerv(GL_DRAW_BUFFER,&buffer);
    const GLboolean srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB); glDisable(GL_FRAMEBUFFER_SRGB);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0); glDrawBuffer(GL_BACK);
    const float aspect=float(width)/height;
    // This renderer's FrustumRZ accepts slopes, not near-plane coordinates;
    // near only controls reverse-Z depth. The plane fits 90% of the viewport.
    New3D::Mat4 view,projection; projection.FrustumRZ(-aspect,aspect,-1,1,.001f);
    const float halfHeight=std::min(.9f,aspect*.675f);
    m_desktopPanel.Draw(m_texture,0,0,width,height,view,projection,halfHeight*4/3,halfHeight,1,0,0,0,1,1);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,target); glDrawBuffer(buffer);
    if (srgb) glEnable(GL_FRAMEBUFFER_SRGB);
  }
  void ReleaseGraphics() {
    if (m_renderer) m_renderer->SetUserMenuPanel(0,false,m_menu.Revision());
    if (m_texture) glDeleteTextures(1,&m_texture);
    m_texture=0; m_uploaded=0; m_desktopPanel.Destroy();
  }
  bool FlushPreferences() {
    if (!m_preferencesDirty) return true;
    if (!m_interactive || !m_preferencesWritable) return false;
    if (HarleyMenuPreferences::WriteAtomic(m_path,m_preferences)) { m_preferencesDirty=false; return true; }
    ErrorLog("HarleyVR menu preferences could not be saved; existing settings were preserved. Changes remain active for this session.");
    return false;
  }

private:
  void LoadBackground(const std::string& path) {
    if (path.empty()) return;
    std::error_code error;
    const auto size=std::filesystem::file_size(path,error);
    if (error || size>16*1024*1024) { InfoLog("Optional menu background unavailable; using the built-in menu artwork."); return; }
    SDL_Surface* source=SDL_LoadBMP(path.c_str());
    if (!source) { InfoLog("Optional menu background could not be decoded: %s",SDL_GetError()); return; }
    if (source->w<=0 || source->h<=0 || source->w>2048 || source->h>2048) { SDL_FreeSurface(source); return; }
    SDL_Surface* rgba=SDL_ConvertSurfaceFormat(source,SDL_PIXELFORMAT_RGBA32,0); SDL_FreeSurface(source);
    if (!rgba) return;
    std::vector<uint8_t> pixels(size_t(rgba->w)*rgba->h*4);
    if (!SDL_MUSTLOCK(rgba) || SDL_LockSurface(rgba)==0) {
      for (int row=0;row<rgba->h;++row)
        std::memcpy(pixels.data()+size_t(row)*rgba->w*4,static_cast<uint8_t*>(rgba->pixels)+size_t(row)*rgba->pitch,size_t(rgba->w)*4);
      if (SDL_MUSTLOCK(rgba)) SDL_UnlockSurface(rgba);
      m_customBackground=m_menu.SetBackgroundRGBA(pixels,rgba->w,rgba->h);
    }
    SDL_FreeSurface(rgba);
  }
  template<class T> void Remember(const std::string& key,T value) {
    if (!m_interactive) return;
    m_preferences.Set(key,value); m_preferencesDirty=true;
  }
  bool Number(const char* key,double minimum,double maximum,double& result) const {
    return HarleyMenuPreferences::Number(m_preferences,key,minimum,maximum,result);
  }
  void LoadPreferences() {
    std::error_code error;
    if (!std::filesystem::exists(m_path,error)) { m_preferencesWritable=!error; return; }
    const auto size=std::filesystem::file_size(m_path,error);
    if (error || size>65536 || Util::Config::FromINIFile(&m_preferences,m_path)) {
      m_preferencesWritable=false; ErrorLog("HarleyVR menu preferences unreadable/oversized; retaining launch settings and preserving the file."); return;
    }
    double value=0;
    HarleyMenuPreferences::ApplyViewOverrides(m_config,m_preferences,m_headset);
    if (!m_soundLocked && Number("SoundVolume",0,100,value)) m_config.Set("SoundVolume",int(value));
    if (Number("UserComfortPercent",0,100,value)) {
      const float amount=float(value)/100;
      double enabled=1; Number("UserComfortEnabled",0,1,enabled);
      m_config.Set("UserComfortStrength",enabled?amount:0.0f);
    }
    if (!m_steeringKey.empty() && m_preferences["SteeringAxis"].ValueAsDefault<std::string>("")==m_steeringKey &&
        Number("SteeringTravel",m_minimumTravel,100,value)) {
      m_config.Set(m_steeringKey,unsigned(value)); m_inputs->LoadFromConfig(m_config);
    }
    InfoLog("HarleyVR scoped menu preferences loaded; effective SoundVolume=%d, MusicVolume=%d.",
      m_config["SoundVolume"].ValueAs<int>(),m_config["MusicVolume"].ValueAs<int>());
  }
  void DetectSteering() {
    m_steeringKey.clear(); m_axisLabel.clear(); m_minimumTravel=10; m_travel=100;
    std::string mapping=m_inputs->steering->GetMapping();
    mapping.erase(std::remove_if(mapping.begin(),mapping.end(),[](unsigned char c){return std::isspace(c)!=0;}),mapping.end());
    std::transform(mapping.begin(),mapping.end(),mapping.begin(),[](unsigned char c){return char(std::toupper(c));});
    if (mapping.compare(0,3,"JOY") || mapping.size()<6) return;
    size_t p=3; unsigned joy=0;
    while (p<mapping.size() && std::isdigit(static_cast<unsigned char>(mapping[p]))) {
      joy=joy*10+unsigned(mapping[p++]-'0'); if (joy>100) return;
    }
    if (!joy || p>=mapping.size() || mapping[p++]!='_') return;
    const auto system=m_inputs->GetInputSystem();
    if (joy>unsigned(system->GetNumJoysticks())) return;
    const std::array<const char*,8> axes={{"X","Y","Z","RX","RY","RZ","S1","S2"}};
    const std::string part=mapping.substr(p);
    for (size_t axis=0;axis<axes.size();++axis) {
      const std::string name=std::string(axes[axis])+"AXIS";
      if (part!=name && part!=name+"_INV") continue;
      const auto* details=system->GetJoyDetails(int(joy)-1);
      const auto* settings=system->GetJoySettings(int(joy)-1,true);
      if (!details || !settings || !details->hasAxis[axis] || settings->deadZones[axis]>=99) return;
      m_minimumTravel=std::max(10u,settings->deadZones[axis]+1);
      m_travel=int(settings->saturations[axis]);
      m_steeringKey="InputJoy"+std::to_string(joy)+axes[axis]+"Saturation"; m_axisLabel=mapping;
      return;
    }
  }
  void RefreshSettings() {
    DetectSteering();
    auto settings=m_menu.GetSettings();
    settings.startup=m_startup; settings.liveCameraAvailable=m_liveCamera;
    settings.dreamRide=m_dreamRide.Enabled();
    settings.dreamRideAvailable=m_dreamRideSupported && m_config["UserMenuRestart"].ValueAsDefault<bool>(false);
    settings.steeringAvailable=!m_steeringKey.empty(); settings.steeringAxis=m_axisLabel;
    settings.steeringTravel=m_travel; settings.arcadeControls=m_arcade;
    settings.sound=m_config["SoundVolume"].ValueAs<int>();
    settings.renderScale=m_nextScale; settings.renderScaleAvailable=m_headset;
    settings.initialRenderScale=m_initialScale;
    settings.currentEyeWidth=m_currentEyeWidth; settings.currentEyeHeight=m_currentEyeHeight;
    settings.startupRestartAvailable=m_config["UserMenuRestart"].ValueAsDefault<bool>(false);
    settings.desktopFovAvailable=!m_headset;
    if (!m_cameraPending) {
      settings.riderHeight=m_config["StereoRiderUp"].ValueAs<float>();
      settings.riderBack=m_config["StereoRiderBack"].ValueAs<float>();
      settings.desktopFov=int(m_config["StereoHorizonFov"].ValueAs<float>());
    }
    settings.theaterAngle=int(m_config["StereoTheaterScreenAngle"].ValueAs<float>());
    settings.theaterDepth=0;
    const float strength=m_config["UserComfortStrength"].ValueAsDefault<float>(1);
    settings.comfortEnabled=strength>0; settings.comfortStrength=int(std::lround(strength*100));
    double remembered=0;
    if (!settings.comfortEnabled && Number("UserComfortPercent",0,100,remembered)) settings.comfortStrength=int(remembered);
    m_menu.SetSettings(settings);
  }
  enum class Pulse {None,Start,Coin,Test};
  Util::Config::Node& m_config;
  CInputs* m_inputs;
  std::string m_path;
  bool m_interactive=false,m_headset=false,m_soundLocked=false,m_arcade=false;
  bool m_customBackground=false;
  bool m_startup=false,m_liveCamera=false;
  bool m_dreamRideSupported=false;
  HarleyMenuPreferences::StartupDreamRide m_dreamRide;
  float m_initialScale=0;
  unsigned m_currentEyeWidth=0,m_currentEyeHeight=0;
  bool m_preferencesWritable=true,m_preferencesDirty=false,m_releaseGate=false;
  bool m_cameraPending=false,m_rebuild=false,m_tabReady=false,m_tabHeld=false;
  Util::Config::Node m_preferences;
  HarleyMenu::UserMenu m_menu;
  IRender3D* m_renderer=nullptr;
  GLuint m_texture=0;
  uint64_t m_uploaded=0;
  New3D::SpatialPanel m_desktopPanel;
  std::string m_steeringKey,m_axisLabel;
  std::map<std::string,float> m_pendingCamera;
  unsigned m_minimumTravel=10;
  int m_travel=100;
  float m_nextScale=.5f;
  Pulse m_pulse=Pulse::None;
};
#endif
