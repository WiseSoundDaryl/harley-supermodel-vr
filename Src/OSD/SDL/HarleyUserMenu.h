#ifndef HARLEY_USER_MENU_H
#define HARLEY_USER_MENU_H

// Owner-thread-only menu policy and cached CPU artwork. This component neither
// polls devices nor touches the game, audio, GL, configuration files or clocks.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "HarleyMenuTypography.h"

namespace HarleyMenu {
struct Navigation {
  bool up=false, down=false, left=false, right=false, accept=false, back=false;
};
struct Settings {
  int steeringTravel=100, sound=100, music=0;
  bool steeringAvailable=false, arcadeControls=false, musicAvailable=true;
  bool comfortEnabled=true, desktopFovAvailable=false;
  bool renderScaleAvailable=false, startupRestartAvailable=false;
  // Startup-only choices are hidden once riding begins. A live camera preview
  // is offered only when the renderer has a real, paused riding scene.
  bool startup=false, liveCameraAvailable=false;
  bool dreamRide=false, dreamRideAvailable=false;
  int comfortStrength=100, theaterAngle=70, theaterDepth=0, desktopFov=90;
  double riderHeight=3.0, riderBack=.75;
  double renderScale=.5, initialRenderScale=.5;
  // Allocated common eye target: max(widthL,widthR), max(heightL,heightR).
  // Never infer this readout from a pending quality choice or panel resolution.
  unsigned currentEyeWidth=0, currentEyeHeight=0;
  std::string steeringAxis;
};
enum class Action {
  None, Resume, StartGame, InsertCoin, Recenter, SteeringTravel, Sound, Music,
  EnterArcade, LeaveArcade, Exit, ComfortEnabled, ComfortStrength,
  RiderHeight, RiderBack, TheaterAngle, TheaterDepth, DesktopFov, RenderScale, CalibrateRide, DreamRide, ReturnStartup
};
struct Event { Action action=Action::None; double value=0; };
enum class Page { Main, Comfort, Camera, Controls, Audio, Arcade, CameraPreview, Bonus, Graphics };

class UserMenu {
public:
  static constexpr int Width() { return 1024; }
  static constexpr int Height() { return 768; }
  static constexpr int RowCount=11; // Includes the dedicated Graphics page.

  void SetSettings(Settings value) {
    value.steeringTravel=std::max(10,std::min(100,value.steeringTravel));
    value.sound=std::max(0,std::min(100,value.sound));
    value.music=std::max(0,std::min(8,value.music));
    value.comfortStrength=std::max(0,std::min(100,value.comfortStrength));
    value.theaterAngle=std::max(40,std::min(100,value.theaterAngle));
    value.theaterDepth=0; // Stereo cinema has been removed.
    value.desktopFov=std::max(60,std::min(120,value.desktopFov));
    value.riderHeight=std::isfinite(value.riderHeight)?std::max(0.0,std::min(8.0,value.riderHeight)):3.0;
    value.riderBack=std::isfinite(value.riderBack)?std::max(0.0,std::min(5.0,value.riderBack)):.75;
    if(!std::isfinite(value.renderScale) || value.renderScale<=0 || value.renderScale>4) value.renderScale=.5;
    if(!std::isfinite(value.initialRenderScale) || value.initialRenderScale<=0 || value.initialRenderScale>4) value.initialRenderScale=.5;
    if(!value.currentEyeWidth || !value.currentEyeHeight || value.currentEyeWidth>65536 || value.currentEyeHeight>65536)
      value.currentEyeWidth=value.currentEyeHeight=0;
    if (value.steeringAxis.size()>48) value.steeringAxis.resize(48);
    if (value.steeringTravel==m_settings.steeringTravel && value.sound==m_settings.sound &&
        value.music==m_settings.music && value.steeringAvailable==m_settings.steeringAvailable &&
        value.arcadeControls==m_settings.arcadeControls && value.musicAvailable==m_settings.musicAvailable && value.steeringAxis==m_settings.steeringAxis &&
        value.comfortEnabled==m_settings.comfortEnabled && value.comfortStrength==m_settings.comfortStrength &&
        value.riderHeight==m_settings.riderHeight && value.riderBack==m_settings.riderBack &&
        value.theaterAngle==m_settings.theaterAngle && value.theaterDepth==m_settings.theaterDepth && value.desktopFov==m_settings.desktopFov &&
        value.desktopFovAvailable==m_settings.desktopFovAvailable &&
        value.renderScale==m_settings.renderScale && value.renderScaleAvailable==m_settings.renderScaleAvailable &&
        value.initialRenderScale==m_settings.initialRenderScale &&
        value.currentEyeWidth==m_settings.currentEyeWidth && value.currentEyeHeight==m_settings.currentEyeHeight &&
        value.startupRestartAvailable==m_settings.startupRestartAvailable &&
        value.startup==m_settings.startup && value.liveCameraAvailable==m_settings.liveCameraAvailable &&
        value.dreamRide==m_settings.dreamRide && value.dreamRideAvailable==m_settings.dreamRideAvailable) return;
    m_settings=value;
    if (!value.startup && m_page==Page::Bonus) { m_page=Page::Main; m_selected=0; }
    m_selected=std::min(m_selected,Rows()-1); Dirty();
  }
  const Settings& GetSettings() const { return m_settings; }
  bool PendingQuality() const {
    return m_settings.renderScaleAvailable && std::abs(m_settings.renderScale-m_settings.initialRenderScale)>0.00001;
  }
  void Open() {
    if (m_open) return;
    m_open=true; m_page=m_settings.arcadeControls?Page::Arcade:Page::Main; m_selected=0;
    m_confirmExit=m_confirmArcade=m_confirmArcadeReturn=false; ResetInput(); Dirty();
  }
  void Close() {
    if (!m_open) return;
    m_open=false; m_confirmExit=m_confirmArcade=false; ResetInput(); Dirty();
  }
  bool IsOpen() const { return m_open; }
  int SelectedRow() const { return m_selected; }
  Page CurrentPage() const { return m_page; }
  bool ConfirmingExit() const { return m_confirmExit; }
  bool ConfirmingArcade() const { return m_confirmArcade; }
  bool ArcadeContinueSelected() const { return m_arcadeContinue; }
  bool IsCameraPreview() const { return m_open && m_page==Page::CameraPreview; }
  void ShowCameraPreview(bool pullback=false) {
    m_previewPullback=pullback; ChangePage(Page::CameraPreview);
    m_selected=m_settings.liveCameraAvailable && pullback?1:0;
  }
  int VisibleRows() const { return Rows(); }
  uint64_t Revision() const { return m_revision; }

  // Optional artwork loaded by the application, never by the menu. Input rows
  // are TOP-DOWN straight RGBA8. Resample once; dirty UI redraws reuse the cache.
  bool SetBackgroundRGBA(const std::vector<uint8_t>& rgba,int width,int height) {
    if(width<1 || height<1 || width>2048 || height>2048 || rgba.size()!=size_t(width)*height*4) return false;
    m_background.resize(size_t(Width())*Height()*4);
    m_backgroundAspect=float(width)/height;
    for(int y=0;y<Height();++y) for(int x=0;x<Width();++x) {
      const float sx=float(x)*(width-1)/(Width()-1),sy=float(y)*(height-1)/(Height()-1);
      const int xx=int(sx),yy=int(sy),right=std::min(xx+1,width-1),bottom=std::min(yy+1,height-1);
      const float tx=sx-xx,ty=sy-yy;
      const size_t dest=(size_t(Height()-1-y)*Width()+x)*4;
      float channels[4]{};
      for(int row=0;row<2;++row)for(int col=0;col<2;++col) {
        const size_t at=(size_t(row?bottom:yy)*width+(col?right:xx))*4;
        const float weight=(row?ty:1-ty)*(col?tx:1-tx);
        for(int c=0;c<3;++c) channels[c]+=weight*rgba[at+c]*rgba[at+3]/255.0f;
        channels[3]+=weight*rgba[at+3];
      }
      for(int c=0;c<4;++c)m_background[dest+c]=uint8_t(std::round(channels[c]));
    }
    Dirty(); return true;
  }

  // Bottom-up GL rows, premultiplied RGBA8, tightly packed. Stable storage and
  // pixels until a visible state changes. Revision is independent of upload.
  const std::vector<uint8_t>& Pixels() {
    if (m_dirty) { Rasterize(); m_dirty=false; }
    return m_pixels;
  }

  // Feed monotonic host microseconds. A backwards/large time discontinuity
  // or focus return re-arms held buttons instead of inventing an input edge.
  // Repeats never catch up in a burst after a stall. Accept/back never repeat.
  Event Update(const Navigation& input,uint64_t now,bool focused=true) {
    if (!m_open) { ResetInput(); return {}; }
    if (!focused) {
      ResetInput();
      if(m_confirmArcade && m_arcadeContinue) { m_arcadeContinue=false; Dirty(); }
      return {};
    }
    const unsigned down=Mask(input);
    if (!m_primed || now<m_lastTime || now-m_lastTime>2000000) {
      m_primed=true; m_previous=down; m_blocked=down; m_lastTime=now;
      if(m_confirmArcade && m_arcadeContinue) { m_arcadeContinue=false; Dirty(); }
      m_repeat=0; m_nextRepeat=0; return {};
    }
    m_lastTime=now;
    m_blocked &= down;
    unsigned edges=down & ~m_previous & ~m_blocked;
    m_previous=down;
    unsigned directions=down & ~m_blocked & 15u;
    // Contradictory directions cancel and must not schedule repeats.
    if ((directions&3u)==3u) directions&=~3u;
    if ((directions&12u)==12u) directions&=~12u;
    // One navigation operation per sample, vertical before horizontal.
    const unsigned direction=(directions&1u)?1u:(directions&2u)?2u:
                             (directions&4u)?4u:(directions&8u)?8u:0u;
    unsigned navigation=0;
    if (direction!=m_repeat) {
      m_repeat=direction;
      m_nextRepeat=now+350000;
      if (edges&direction) navigation=direction;
    } else if (direction && now>=m_nextRepeat) {
      navigation=direction; m_nextRepeat=now+120000;
    }
    if (edges&32u) {
      if (m_confirmExit) { m_confirmExit=false; Dirty(); return {}; }
      if (m_confirmArcade) { m_confirmArcade=false; m_arcadeContinue=false; Dirty(); return {}; }
      if (m_page==Page::CameraPreview) { ChangePage(Page::Camera); m_selected=1; return {}; }
      if (m_page!=Page::Main) { ChangePage(Page::Main); return {}; }
      return {Action::Resume,0};
    }
    if (m_confirmArcade) {
      if(navigation) {
        m_arcadeContinue=navigation==2u || navigation==8u; Dirty();
        return {}; // Selection and destructive confirmation require separate presses.
      }
      if(edges&16u) {
        const bool enter=m_arcadeContinue;
        m_confirmArcade=false; m_arcadeContinue=false; Dirty();
        return enter?Event{m_confirmArcadeReturn?Action::ReturnStartup:Action::EnterArcade,0}:Event{};
      }
      return {};
    }
    if (m_confirmExit) return (edges&16u)?Event{Action::Exit,0}:Event{};
    if (navigation==1u || navigation==2u) {
      m_selected=(m_selected+(navigation==1u?Rows()-1:1))%Rows(); Dirty();
    } else if (navigation==4u || navigation==8u) {
      return Adjust(navigation==4u?-1:1);
    }
    if (!(edges&16u)) return {};
    switch (ItemAt(m_selected)) {
      case Item::Resume:return {Action::Resume,0};
      case Item::Start:return {Action::StartGame,0};
      case Item::Coin:return {Action::InsertCoin,0};
      case Item::Recenter:return {Action::Recenter,0};
      case Item::OpenComfort:ChangePage(Page::Comfort); break;
      case Item::OpenCamera:ChangePage(Page::Camera); break;
      case Item::OpenGraphics:ChangePage(Page::Graphics); break;
      case Item::OpenControls:ChangePage(Page::Controls); break;
      case Item::OpenAudio:ChangePage(Page::Audio); break;
      case Item::OpenArcade:ChangePage(Page::Arcade); break;
      case Item::OpenBonus:if(m_settings.startup) ChangePage(Page::Bonus); break;
      case Item::RiderHeight:case Item::RiderBack:
        if(m_page!=Page::CameraPreview) OpenCameraPreview(ItemAt(m_selected));
        break;
      case Item::Calibrate:return {Action::CalibrateRide,m_previewPullback?1.0:0.0};
      case Item::Back:ChangePage(m_page==Page::CameraPreview?Page::Camera:Page::Main); break;
      case Item::Arcade:
        if(m_settings.arcadeControls) return {Action::Resume,0};
        m_confirmArcade=true; m_confirmArcadeReturn=false; m_arcadeContinue=false; Dirty(); break;
      case Item::RestartStartup:
        m_confirmArcade=true; m_confirmArcadeReturn=true; m_arcadeContinue=false; Dirty(); break;
      case Item::ComfortEnabled:m_settings.comfortEnabled=!m_settings.comfortEnabled; Dirty();
        return {Action::ComfortEnabled,m_settings.comfortEnabled?1.0:0.0};
      case Item::DreamRide:return Adjust(m_settings.dreamRide?-1:1);
      case Item::Exit:m_confirmExit=true; Dirty(); break;
      default:break; // Left/right adjusts settings; accept never changes them.
    }
    return {};
  }

private:
  enum class Item { Resume,Start,Coin,Recenter,OpenComfort,OpenCamera,OpenControls,
    OpenAudio,OpenArcade,Exit,Back,ComfortEnabled,ComfortStrength,RiderHeight,
    RiderBack,TheaterAngle,DesktopFov,RenderScale,Travel,Sound,Music,Arcade,FreePlay,Region,Calibrate,OpenBonus,DreamRide,OpenGraphics,RestartStartup };
  int Rows() const {
    switch(m_page) {
      case Page::Main:return m_settings.startup?10:11; case Page::Comfort:return 3;
      case Page::Camera:return 5; case Page::Graphics:return 2;
      case Page::Controls:return 2; case Page::Audio:return 2;
      case Page::Arcade:return m_settings.arcadeControls?5:4; case Page::CameraPreview:return 2; case Page::Bonus:return 2;
    } return 1;
  }
  Item ItemAt(int row) const {
    static const Item main[]={Item::Resume,Item::Start,Item::Coin,Item::Recenter,
      Item::OpenComfort,Item::OpenCamera,Item::OpenGraphics,Item::OpenControls,Item::OpenAudio,Item::OpenArcade,Item::Exit};
    static const Item startupMain[]={Item::Resume,Item::Recenter,
      Item::OpenComfort,Item::OpenCamera,Item::OpenGraphics,Item::OpenControls,Item::OpenAudio,Item::OpenArcade,Item::OpenBonus,Item::Exit};
    static const Item comfort[]={Item::Back,Item::ComfortEnabled,Item::ComfortStrength};
    static const Item camera[]={Item::Back,Item::RiderHeight,Item::RiderBack,Item::TheaterAngle,Item::DesktopFov};
    static const Item graphics[]={Item::Back,Item::RenderScale};
    static const Item controls[]={Item::Back,Item::Travel};
    static const Item audio[]={Item::Back,Item::Sound};
    static const Item arcade[]={Item::Back,Item::Arcade,Item::FreePlay,Item::Region};
    static const Item activeArcade[]={Item::Back,Item::Arcade,Item::RestartStartup,Item::FreePlay,Item::Region};
    static const Item preview[]={Item::RiderHeight,Item::RiderBack};
    static const Item waiting[]={Item::Calibrate,Item::Back};
    static const Item bonus[]={Item::Back,Item::DreamRide};
    switch(m_page) {
      case Page::Main:return m_settings.startup?startupMain[row]:main[row]; case Page::Comfort:return comfort[row];
      case Page::Camera:return camera[row]; case Page::Graphics:return graphics[row]; case Page::Controls:return controls[row];
      case Page::Audio:return audio[row]; case Page::Arcade:return m_settings.arcadeControls?activeArcade[row]:arcade[row];
      case Page::CameraPreview:return m_settings.liveCameraAvailable?preview[row]:waiting[row];
      case Page::Bonus:return bonus[row];
    } return Item::Back;
  }
  void ChangePage(Page page) { m_page=page; m_selected=0; Dirty(); }
  void OpenCameraPreview(Item item) {
    ShowCameraPreview(item==Item::RiderBack);
  }
  static unsigned Mask(const Navigation& n) {
    return (n.up?1u:0u)|(n.down?2u:0u)|(n.left?4u:0u)|(n.right?8u:0u)|
           (n.accept?16u:0u)|(n.back?32u:0u);
  }
  void ResetInput() { m_primed=false; m_previous=m_blocked=m_repeat=0; m_nextRepeat=m_lastTime=0; }
  void Dirty() { m_dirty=true; ++m_revision; }
  Event Adjust(int direction) {
    int* value=nullptr; Action action=Action::None; int minimum=0;
    int maximum=100;
    const auto item=ItemAt(m_selected);
    if(item==Item::DreamRide) {
      if(!m_settings.startup || !m_settings.dreamRideAvailable) return {};
      const bool enabled=direction>0;
      if(enabled==m_settings.dreamRide) return {};
      m_settings.dreamRide=enabled; Dirty(); return {Action::DreamRide,enabled?1.0:0.0};
    }
    if(item==Item::Music && (!m_settings.startup || !m_settings.musicAvailable)) return {};
    const int step=direction*(item==Item::Music?1:5);
    if(item==Item::RenderScale) {
      if(!m_settings.startup || !m_settings.renderScaleAvailable || !m_settings.startupRestartAvailable) return {};
      double next=m_settings.renderScale;
      if(direction>0) {
        for(double preset:{.5,.75,1.0}) if(preset>next) { next=preset; break; }
      } else {
        for(double preset:{1.0,.75,.5}) if(preset<next) { next=preset; break; }
      }
      if(next==m_settings.renderScale) return {};
      m_settings.renderScale=next; Dirty(); return {Action::RenderScale,next};
    }
    if(item==Item::ComfortEnabled) {
      const bool enabled=direction>0;
      if(enabled==m_settings.comfortEnabled) return {};
      m_settings.comfortEnabled=enabled; Dirty(); return {Action::ComfortEnabled,enabled?1.0:0.0};
    }
    if(item==Item::RiderHeight || item==Item::RiderBack) {
      if(m_page!=Page::CameraPreview) OpenCameraPreview(item);
      if(!m_settings.liveCameraAvailable) return {};
      double& offset=item==Item::RiderHeight?m_settings.riderHeight:m_settings.riderBack;
      const double next=std::max(0.0,std::min(item==Item::RiderHeight?8.0:5.0,offset+direction*.25));
      if(next==offset) return {};
      offset=next; Dirty(); return {item==Item::RiderHeight?Action::RiderHeight:Action::RiderBack,next};
    }
    if (item==Item::Travel && m_settings.steeringAvailable) {
      value=&m_settings.steeringTravel; action=Action::SteeringTravel; minimum=10;
    } else if (item==Item::Sound) { value=&m_settings.sound; action=Action::Sound; }
    else if (item==Item::Music) { value=&m_settings.music; action=Action::Music; maximum=8; }
    else if (item==Item::ComfortStrength) { value=&m_settings.comfortStrength; action=Action::ComfortStrength; }
    else if (item==Item::TheaterAngle) { value=&m_settings.theaterAngle; action=Action::TheaterAngle; minimum=40; }
    else if (item==Item::DesktopFov && m_settings.desktopFovAvailable) {
      value=&m_settings.desktopFov; action=Action::DesktopFov; minimum=60; maximum=120;
    }
    if (!value) return {};
    const int next=std::max(minimum,std::min(maximum,*value+step));
    if (next==*value) return {};
    *value=next; Dirty(); return {action,double(next)};
  }

  struct Colour { uint8_t r,g,b,a; };
  static Colour MakeColour(int r,int g,int b,int a=255) {
    return {uint8_t(r*a/255),uint8_t(g*a/255),uint8_t(b*a/255),uint8_t(a)};
  }
  void Pixel(int x,int y,Colour c) {
    if (x<0 || y<0 || x>=Width() || y>=Height()) return;
    const size_t i=(size_t(Height()-1-y)*Width()+x)*4;
    m_pixels[i]=c.r; m_pixels[i+1]=c.g; m_pixels[i+2]=c.b; m_pixels[i+3]=c.a;
  }
  void Rect(int x,int y,int w,int h,Colour c) {
    const int endX=std::min(Width(),x+w),endY=std::min(Height(),y+h);
    for(int yy=std::max(0,y);yy<endY;++yy)
      for(int xx=std::max(0,x);xx<endX;++xx) Pixel(xx,yy,c);
  }
  void Line(int x,int y,int xx,int yy,Colour colour,int thickness=1) {
    const int dx=std::abs(xx-x),dy=-std::abs(yy-y),sx=x<xx?1:-1,sy=y<yy?1:-1;
    int error=dx+dy;
    for(;;) {
      Rect(x,y,thickness,thickness,colour);
      if(x==xx && y==yy) break;
      const int twice=2*error;
      if(twice>=dy) { error+=dy; x+=sx; }
      if(twice<=dx) { error+=dx; y+=sy; }
    }
  }
  void BlendPixel(int x,int y,Colour c) {
    if(x<0 || y<0 || x>=Width() || y>=Height()) return;
    const size_t at=(size_t(Height()-1-y)*Width()+x)*4;
    const int inverse=255-c.a;
    const uint8_t src[]={c.r,c.g,c.b,c.a};
    for(int channel=0;channel<4;++channel)
      m_pixels[at+channel]=uint8_t(std::min(255,int(src[channel])+(m_pixels[at+channel]*inverse+127)/255));
  }
  void Rounded(int x,int y,int width,int height,int radius,Colour colour) {
    for(int yy=y;yy<y+height;++yy)for(int xx=x;xx<x+width;++xx) {
      const float dx=std::max(float(x+radius)-xx-.5f,std::max(0.0f,float(xx)+.5f-(x+width-radius)));
      const float dy=std::max(float(y+radius)-yy-.5f,std::max(0.0f,float(yy)+.5f-(y+height-radius)));
      const float coverage=std::clamp(float(radius)+.5f-std::hypot(dx,dy),0.0f,1.0f);
      if(coverage>0)BlendPixel(xx,yy,{uint8_t(colour.r*coverage),uint8_t(colour.g*coverage),uint8_t(colour.b*coverage),uint8_t(colour.a*coverage)});
    }
  }
  float TextWidth(const std::string& text,float size,bool title=false) const {
    using namespace HarleyMenuTypography;
    const auto face=title?Face::Title:Face::Body;
    float width=0;for(unsigned char c:text)width+=Get(face,c).advance;
    return width*size/Size(face);
  }
  void Type(float x,float y,const std::string& text,float size,Colour colour,bool title=false) {
    using namespace HarleyMenuTypography;
    const auto face=title?Face::Title:Face::Body;
    const float scale=size/Size(face),baseline=y+Ascent(face)*scale;
    const auto& coverage=Coverage();
    for(unsigned char ch:text) {
      const auto& glyph=Get(face,ch);
      const float left=x+glyph.left*scale,top=baseline+glyph.top*scale;
      const int firstX=int(std::floor(left)),firstY=int(std::floor(top));
      const int endX=int(std::ceil(left+glyph.width*scale)),endY=int(std::ceil(top+glyph.height*scale));
      for(int yy=firstY;yy<endY;++yy)for(int xx=firstX;xx<endX;++xx) {
        const float gx=(xx+.5f-left)/scale-.5f,gy=(yy+.5f-top)/scale-.5f;
        const int ix=int(std::floor(gx)),iy=int(std::floor(gy));
        const float tx=gx-ix,ty=gy-iy;
        float a=0;
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
          if(ix+dx<0 || iy+dy<0 || ix+dx>=glyph.width || iy+dy>=glyph.height)continue;
          a+=coverage[glyph.offset+size_t(iy+dy)*glyph.width+ix+dx]*(dx?tx:1-tx)*(dy?ty:1-ty);
        }
        if(a>0) {
          const float amount=a/255;
          BlendPixel(xx,yy,{uint8_t(colour.r*amount),uint8_t(colour.g*amount),uint8_t(colour.b*amount),uint8_t(colour.a*amount)});
        }
      }
      x+=glyph.advance*scale;
    }
  }
  void Artwork(int x,int y,int width,int height) {
    if(m_background.empty()) return;
    const int fittedWidth=std::min(width,int(height*m_backgroundAspect));
    const int fittedHeight=std::min(height,int(width/m_backgroundAspect));
    const int left=x+(width-fittedWidth)/2,top=y+(height-fittedHeight)/2;
    for(int yy=0;yy<fittedHeight;++yy)for(int xx=0;xx<fittedWidth;++xx) {
      const float sx=float(xx)*(Width()-1)/std::max(1,fittedWidth-1),sy=float(yy)*(Height()-1)/std::max(1,fittedHeight-1);
      const int ix=int(sx),iy=int(sy);const float tx=sx-ix,ty=sy-iy;
      float colour[4]{};
      for(int row=0;row<2;++row)for(int col=0;col<2;++col) {
        const size_t at=(size_t(Height()-1-std::min(iy+row,Height()-1))*Width()+std::min(ix+col,Width()-1))*4;
        const float weight=(row?ty:1-ty)*(col?tx:1-tx);
        for(int c=0;c<4;++c)colour[c]+=m_background[at+c]*weight;
      }
      BlendPixel(left+xx,top+yy,{uint8_t(std::round(colour[0])),uint8_t(std::round(colour[1])),uint8_t(std::round(colour[2])),uint8_t(std::round(colour[3]))});
    }
  }
  void ChromeFrame(int x,int y,int width,int height,int radius) {
    Rounded(x,y,width,height,radius,MakeColour(12,20,30));
    Rounded(x+1,y+1,width-2,height-2,radius-1,MakeColour(235,245,251));
    Rounded(x+3,y+3,width-6,height-6,radius-3,MakeColour(89,111,131));
    Rounded(x+5,y+5,width-10,height-10,radius-5,MakeColour(214,229,238));
    Rounded(x+7,y+7,width-14,height-14,radius-7,MakeColour(8,24,42));
  }
  int Paragraph(int x,int y,const std::string& text,int width,float size,Colour colour,int lineHeight=30) {
    std::string line;size_t from=0;
    while(from<text.size()) {
      const size_t space=text.find(' ',from);
      const size_t end=space==std::string::npos?text.size():space;
      const std::string word=text.substr(from,end-from);
      const std::string proposed=line.empty()?word:line+" "+word;
      if(!line.empty() && TextWidth(proposed,size)>width) {Type(float(x),float(y),line,size,colour);y+=lineHeight;line=word;}
      else line=proposed;
      from=end+1;
    }
    if(!line.empty()) {Type(float(x),float(y),line,size,colour);y+=lineHeight;}
    return y;
  }
  void Ring(int cx,int cy,int radius,Colour colour,int thickness=2) {
    for(int y=cy-radius-1;y<=cy+radius+1;++y)for(int x=cx-radius-1;x<=cx+radius+1;++x) {
      const float d=std::abs(std::hypot(float(x-cx),float(y-cy))-(radius-thickness*.5f));
      const float coverage=std::clamp(thickness*.5f+.5f-d,0.0f,1.0f);
      if(coverage>0)BlendPixel(x,y,{uint8_t(colour.r*coverage),uint8_t(colour.g*coverage),uint8_t(colour.b*coverage),uint8_t(colour.a*coverage)});
    }
  }
  void Icon(int x,int y,Item item,Colour colour) {
    if(item==Item::Resume || item==Item::Start) {
      for(int dx=0;dx<12;++dx)Rect(x-5+dx,y-10+dx,1,21-2*dx,colour);
    } else if(item==Item::Recenter) {
      Ring(x,y,8,colour);Line(x-12,y,x+12,y,colour);Line(x,y-12,x,y+12,colour);
    } else if(item==Item::Coin) {
      Ring(x,y,11,colour);Type(float(x-4),float(y-16),"1",26,colour);
    } else if(item==Item::OpenControls || item==Item::Travel) {
      Ring(x,y,11,colour);Line(x-9,y-2,x+9,y-2,colour,2);Line(x,y,x,y+9,colour,2);
    } else if(item==Item::Back) {
      Line(x+5,y-9,x-5,y,colour,2);Line(x-5,y,x+5,y+9,colour,2);
    } else if(item==Item::Exit) {
      Rect(x-9,y-11,11,2,colour);Rect(x-9,y-11,2,23,colour);Rect(x-9,y+10,11,2,colour);
      Line(x-1,y,x+12,y,colour,2);Line(x+6,y-6,x+12,y,colour,2);Line(x+6,y+6,x+12,y,colour,2);
    } else {
      Ring(x,y,10,colour);Ring(x,y,3,colour);
    }
  }
  std::string RowLabel(int row) const {
    switch(ItemAt(row)) {
      case Item::Resume:return m_settings.arcadeControls?"Resume arcade setup":m_settings.startup?"Enter game":"Resume ride"; case Item::Start:return "Start game";
      case Item::Coin:return "Insert coin"; case Item::Recenter:return "Recenter view";
      case Item::OpenComfort:return "Comfort"; case Item::OpenCamera:return "Camera position / FOV";
      case Item::OpenGraphics:return "Graphics";
      case Item::OpenControls:return "Controls"; case Item::OpenAudio:return "Audio";
      case Item::OpenArcade:return "Arcade settings"; case Item::Exit:return "Exit game";
      case Item::OpenBonus:return "Bonus"; case Item::DreamRide:return "Dream Ride";
      case Item::Back:return "Back to ride menu"; case Item::ComfortEnabled:return "Comfort vignette";
      case Item::ComfortStrength:return "Vignette strength"; case Item::RiderHeight:return "Rider height";
      case Item::RiderBack:return "Camera pullback"; case Item::TheaterAngle:return "Virtual screen width";
      case Item::DesktopFov:return "Desktop FOV"; case Item::Travel:return "Steering travel";
      case Item::RenderScale:return "Render quality";
      case Item::Sound:return "Master sound"; case Item::Music:return "Arcade music level";
      case Item::Arcade:return m_settings.arcadeControls?"Resume arcade setup":"Enter arcade settings";
      case Item::RestartStartup:return "Return to startup";
      case Item::FreePlay:return "Free play guide"; case Item::Region:return "Region guide";
      case Item::Calibrate:return m_settings.startup?"Choose Rider & Fit Seat":"Continue to Riding";
    } return "";
  }
  std::string Detail() const {
    switch(ItemAt(m_selected)) {
      case Item::Resume:return m_settings.arcadeControls?
        "Continue cabinet setup. Select EXIT in each cabinet menu to save and return automatically to HarleyVR startup.":m_settings.startup?
        "Enter the arcade. Insert a credit and press Start when you are ready to ride.":
        "Return to the road. Your ride stays paused while this menu is open.";
      case Item::Start:return "Send the arcade Start button. If the cabinet needs a credit, choose Insert coin first.";
      case Item::Coin:return "Add one credit using the original arcade coin input.";
      case Item::Recenter:return "Set your comfortable seated direction as the center of the VR view.";
      case Item::OpenComfort:return "Tune comfort effects for your ride. Adjust the vignette without changing your saved camera position.";
      case Item::OpenCamera:return "Adjust rider height, camera pullback, and virtual cinema width. Headset lens FOV stays runtime controlled.";
      case Item::OpenControls:return "Menu: click left stick, or hold left side grip and click right stick. PS / Home opens SteamVR. Adjust physical steering travel here.";
      case Item::OpenGraphics:return "Choose Performance, Balanced or Quality before entering the game. Current render quality remains visible during your ride.";
      case Item::OpenAudio:return "Adjust all game audio. A short level-check sound plays as you adjust.";
      case Item::OpenArcade:return "Original cabinet settings include coin setup, free play, music and engine levels. Entering setup ends the current game; confirmation is required.";
      case Item::OpenBonus:return "Explore with no gameplay HUD or time limit. Choose Dream Ride before entering the game.";
      case Item::DreamRide:return m_settings.dreamRideAvailable?
        "No HUD or time limit while riding. Menus and endings stay visible. This launch only; music unchanged.":
        "Needs the verified Export Revision B ROM and a HarleyVR startup launcher. Your current launch settings stay unchanged.";
      case Item::Back:return "Return to the main ride menu. Your current settings remain applied.";
      case Item::ComfortEnabled:return "Turn the riding comfort vignette on or off. The remembered strength stays available when you turn it back on.";
      case Item::ComfortStrength:return "Scale the riding comfort vignette. Lower is lighter; higher keeps the stronger porthole. Left/right adjusts by 5%.";
      case Item::RiderHeight:return "Select to see the seat preview. Raise or lower your view with left/right while keeping the bike visible. Head tracking remains independent.";
      case Item::RiderBack:return "Select to see the seat preview. Move your view back from the handlebars with left/right. Up/down switches between height and pullback.";
      case Item::TheaterAngle:return "Change the apparent width of the virtual cinema screen, in degrees. This does not change your headset's lens FOV.";
      case Item::DesktopFov:return m_settings.desktopFovAvailable?
        "Change the desktop preview field of view, in degrees. Headset lenses are controlled by the VR runtime.":
        "Unavailable in a headset session. OpenXR supplies the correct lens field of view for each eye.";
      case Item::RenderScale:return !m_settings.renderScaleAvailable?
        "Available in headset sessions. This changes VR eye resolution, not headset lens FOV.":
        !m_settings.startup || !m_settings.startupRestartAvailable?
        "CURRENT QUALITY. Restart HarleyVR to change it before entering the game. 50% Performance, 75% Balanced, 100% Quality.":
        "Choose with left/right. Applies when you enter the game. Higher resolution needs more GPU time: 50% Performance, 75% Balanced, 100% Quality.";
      case Item::Travel:return m_settings.steeringAvailable?
        "Lower travel reaches full steering with less wheel or stick movement. This changes the selected physical input axis, not the VR thumbstick.":
        "No supported steering wheel or joystick axis is mapped. Map a steering axis before changing its travel.";
      case Item::Sound:return "Adjust all game audio, including music. A short level-check sound previews the volume. Left/right changes by 5%.";
      case Item::Music:return m_settings.musicAvailable?
        "STARTUP SETTING. 1 is quieter, 8 is louder. Game setting keeps the cabinet choice. Master sound controls all audio.":
        "Music adjustment is unavailable for this ROM revision. Use the game's original arcade settings instead.";
      case Item::Arcade:return m_settings.arcadeControls?
        "Use X / Square for next item and A / Cross to select. Choose EXIT in each cabinet menu to save; HarleyVR startup opens automatically.":
        "Entering setup ends the ride. A / Cross selects; X / Square moves to the next item. Use the original EXIT to go back and save.";
      case Item::RestartStartup:return "Restart the arcade and return to HarleyVR startup. Save cabinet changes with the original EXIT options first; unapplied edits may be lost.";
      case Item::FreePlay:return "Enter arcade settings. Open COIN ASSIGNMENTS. Set COIN/CREDIT SETTING to #27 for FREE PLAY. Select EXIT in each arcade menu to save.";
      case Item::Region:return "At GAME ASSIGNMENTS: shift up twice, shift down twice, then View, Music, View, Music. Use the original menu to choose the region.";
      case Item::Calibrate:return "Continue to the game. Seat adjustment opens automatically when your actual riding camera and bike are ready.";
      default:return "Leave HarleyVR safely. A second confirmation is required.";
    }
  }
  static std::string Number(double value) {
    char text[32]; std::snprintf(text,sizeof(text),"%.2f",value); return text;
  }
  std::string Value(Item item) const {
    switch(item) {
      case Item::Travel:return m_settings.steeringAvailable?std::to_string(m_settings.steeringTravel)+"%":"--";
      case Item::Sound:return std::to_string(m_settings.sound)+"%";
      case Item::Music:return !m_settings.musicAvailable?"--":m_settings.music?std::to_string(m_settings.music)+"/8":"Game setting";
      case Item::ComfortEnabled:return m_settings.comfortEnabled?"On":"Off";
      case Item::DreamRide:return m_settings.dreamRideAvailable?(m_settings.dreamRide?"On":"Off"):"--";
      case Item::ComfortStrength:return std::to_string(m_settings.comfortStrength)+"%";
      case Item::RiderHeight:return Number(m_settings.riderHeight);
      case Item::RiderBack:return Number(m_settings.riderBack);
      case Item::TheaterAngle:return std::to_string(m_settings.theaterAngle)+" deg";
      case Item::DesktopFov:return m_settings.desktopFovAvailable?std::to_string(m_settings.desktopFov)+" deg":"XR";
      case Item::RenderScale:return m_settings.renderScaleAvailable?std::to_string(int(std::round(m_settings.renderScale*100)))+"%":"--";
      default:return "";
    }
  }
  void RasterizeCameraPreview() {
    m_pixels.assign(size_t(Width())*Height()*4,0);
    const auto cream=MakeColour(255,248,230),orange=MakeColour(245,112,24);
    const auto navy=MakeColour(8,28,48),silver=MakeColour(224,237,245);
    Rounded(40,30,944,98,12,MakeColour(5,25,45,232));Rect(56,44,4,68,orange);
    Type(76,40,m_settings.liveCameraAvailable?"YOUR RIDING POSITION":"FIT YOUR SEAT",34,cream,true);
    Type(76,85,m_settings.liveCameraAvailable?
      "Ride paused. Look at the bike while you adjust.":
      "Adjust your view on your actual bike.",25,silver);
    if(!m_settings.liveCameraAvailable) {
      Rounded(100,210,824,340,14,MakeColour(5,25,45,245));Rect(124,231,776,3,orange);
      Paragraph(132,258,m_settings.startup?"Choose your rider to fit your seat.":
        "Your bike is not ready to adjust yet.",752,34,cream,38);
      Paragraph(132,326,m_settings.startup?
        "Use Coin and Start as needed, then choose your rider. Your ride pauses automatically when the actual bike and riding camera are ready.":
        "Continue your current game normally. The ride pauses automatically when the actual bike and riding camera are ready.",752,27,silver,32);
      Rounded(m_selected?604:126,455,m_selected?286:462,54,8,orange);
      Type(144,463,m_settings.startup?"Choose Rider & Fit Seat":"Continue to Riding",29,m_selected?cream:navy);
      Type(624,463,"Back",29,m_selected?navy:cream);
      Type(132,523,"Up/down: choose   A / Cross: select   B / Circle: return",23,silver);
      return;
    }
    // No full-screen plate or artwork in the live mode: the actual bike and
    // surrounding road remain visible through the centre of this texture.
    Rounded(40,605,944,135,12,MakeColour(5,25,45,232));
    for(int row=0;row<2;++row) {
      const int left=58+row*464;
      if(row==m_selected) Rounded(left,619,442,46,6,orange);
      Type(float(left+14),621,row?"Camera pullback":"Rider height",29,row==m_selected?navy:cream);
      const auto value=Value(row?Item::RiderBack:Item::RiderHeight);
      Type(float(left+423)-TextWidth(value,29),621,value,29,row==m_selected?navy:silver);
    }
    Type(60,682,"Up/down: choose   Left/right: adjust   B / Circle: return",26,cream);
    Type(60,715,"Values use game camera units.",18,silver);
  }
  void Rasterize() {
    if(m_page==Page::CameraPreview) { RasterizeCameraPreview(); return; }
    m_pixels.assign(size_t(Width())*Height()*4,0);
    for(int y=0;y<Height();++y) {
      const float t=std::min(1.0f,float(y)/Height());
      Rect(0,y,Width(),1,MakeColour(int(15-8*t),int(81-55*t),int(131-84*t)));
    }
    const auto cream=MakeColour(255,248,230),navy=MakeColour(8,28,48),orange=MakeColour(245,112,24);
    const auto gold=MakeColour(255,190,73),silver=MakeColour(224,237,245),blue=MakeColour(102,188,222);
    const auto muted=MakeColour(184,205,217),inkMuted=MakeColour(83,103,117);
    // Bold chrome-lined lettering and orange instrument trim echo the period
    // flyer without reproducing its advertising, logos or cabinet photography.
    for(int dy=-2;dy<=3;++dy)for(int dx=-2;dx<=3;++dx)
      if(dx*dx+dy*dy<=13)Type(31.0f+dx,17.0f+dy,"HARLEY",59,navy,true);
    Type(31,17,"HARLEY",59,silver,true);
    const float titleVR=31+TextWidth("HARLEY",59,true)+3;
    for(int dy=-2;dy<=3;++dy)for(int dx=-2;dx<=3;++dx)
      if(dx*dx+dy*dy<=13)Type(titleVR+dx,17.0f+dy,"VR",59,navy,true);
    Type(titleVR,17,"VR",59,orange,true);
    Type(34,91,"Feel the freedom only a Harley can give.",24,cream);
    ChromeFrame(891,24,100,94,16);
    Rounded(901,34,80,27,6,orange);Type(916,32,"L.A.",22,navy,true);
    Type(914,60,"01",42,silver,true);
    Rect(31,126,961,2,MakeColour(143,171,192));Rect(31,129,961,2,MakeColour(20,40,59));
    const char* page=m_page==Page::Main?"RIDE MENU":m_page==Page::Comfort?"COMFORT":m_page==Page::Camera?"CAMERA & DISPLAY":
      m_page==Page::Graphics?"GRAPHICS":m_page==Page::Controls?"CONTROLS":m_page==Page::Audio?"Things that Go Vroom...":m_page==Page::Bonus?"BONUS":"ARCADE CABINET";
    Type(34,132,page,23,cream);
    Type(433,132,"HARLEY-DAVIDSON & L.A. RIDERS",22,silver);
    const std::string status=m_settings.arcadeControls?"ARCADE CONTROLS":m_settings.startup?"WELCOME":"PAUSED";
    Type(987-TextWidth(status,22),132,status,22,gold);

    ChromeFrame(23,163,376,521,15);
    Rounded(31,171,360,505,8,cream);
    // Keep every rider, palm and the full front wheel in the user's actual
    // clean splash. Preserve aspect and fit the whole unaltered source image.
    ChromeFrame(417,163,578,394,15);
    if(!m_background.empty())Artwork(427,173,558,374);
    else {
      for(int y=173;y<547;++y)Rect(427,y,558,1,MakeColour(20,int(115-(y-173)*.15f),int(172-(y-173)*.12f)));
      Type(501,289,"LOS ANGELES",44,cream,true);
      Type(583,357,"RIDE READY",29,silver);
    }
    Rounded(417,569,578,115,12,MakeColour(5,25,45,249));Rect(434,579,544,3,orange);
    Paragraph(436,589,Detail(),540,24,cream,26);

    const int startY=185,rowHeight=Rows()>10?40:44;
    for(int row=0;row<Rows();++row) {
      const int y=startY+row*rowHeight;const auto item=ItemAt(row);const bool selected=row==m_selected;
      if(selected) {Rounded(36,y-4,350,rowHeight-6,6,orange);Rect(38,y+1,4,28,MakeColour(255,219,149));}
      else if(row+1<Rows())Rect(83,y+rowHeight-7,289,1,MakeColour(219,225,219));
      const auto textColour=navy;
      Icon(60,y+14,item,selected?navy:inkMuted);
      Type(83,float(y-5),RowLabel(row),25,textColour);
      const std::string value=Value(item);
      if(!value.empty())Type(374-TextWidth(value,24),float(y-5),value,24,selected?navy:MakeColour(24,104,132));
      else if(item==Item::OpenComfort || item==Item::OpenCamera || item==Item::OpenGraphics || item==Item::OpenControls || item==Item::OpenAudio || item==Item::OpenArcade || item==Item::OpenBonus)
        Type(365,float(y-6),">",27,selected?navy:inkMuted);
    }
    const auto selected=ItemAt(m_selected);
    if(m_page==Page::Main) {
      Rect(46,634,329,1,MakeColour(186,202,209));
      Type(48,638,"MENU: LEFT STICK CLICK / TAB",18,inkMuted);
      Type(48,659,"OR LEFT GRIP + RIGHT STICK CLICK",16,inkMuted);
    } else {
      const int gaugeY=Rows()>=5?548:478;
      const int radius=Rows()>=5?64:82;
      const auto text=Value(selected);
      if(!text.empty() && selected!=Item::DesktopFov) {
        Rounded(210-radius,gaugeY-radius,2*radius,2*radius,radius,navy);
        Ring(210,gaugeY,radius,silver,4);Ring(210,gaugeY,radius-6,MakeColour(104,139,164),2);
        for(int tick=0;tick<=12;++tick) {
          const float angle=(float(tick)*24+126)*.01745329252f;
          const int outer=radius-11,inner=radius-(tick%3?16:22);
          Line(int(210+std::cos(angle)*inner),int(gaugeY+std::sin(angle)*inner),
               int(210+std::cos(angle)*outer),int(gaugeY+std::sin(angle)*outer),tick>9?orange:silver,2);
        }
        const bool longValue=text.size()>6;
        const float font=longValue?27:Rows()>=5?34:42;
        Type(210-TextWidth(text,font)/2,float(gaugeY)-font*.65f,text,font,cream);
        const std::string label=selected==Item::RenderScale?(PendingQuality()?"SELECTED":"CURRENT"):selected==Item::DreamRide?"STARTUP":selected==Item::Music?(m_settings.musicAvailable?"STARTUP":"UNAVAILABLE"):
          selected==Item::RiderHeight || selected==Item::RiderBack?"PREVIEW":"ADJUST";
        Type(210-TextWidth(label,18)/2,float(gaugeY+radius-34),label,18,blue);
      } else {
        Type(210-TextWidth("YOUR ROAD.",27)/2,float(gaugeY-25),"YOUR ROAD.",27,navy);
        Type(210-TextWidth("YOUR PACE.",27)/2,float(gaugeY+12),"YOUR PACE.",27,MakeColour(24,104,132));
      }
      if(selected==Item::Travel) {
        Paragraph(51,587,m_settings.steeringAvailable?m_settings.steeringAxis:"No mapped steering axis",319,21,inkMuted,23);
      } else if(selected==Item::RenderScale) {
        const auto scale=m_settings.renderScale;
        const std::string label=!m_settings.renderScaleAvailable?"Headset only":scale==.5?"Performance":scale==.75?"Balanced":scale==1?"Quality":"Custom";
        Type(210-TextWidth(label,22)/2,638,label,22,inkMuted);
      } else Type(61,644,"LEFT / RIGHT TO ADJUST",20,inkMuted);
      if(m_page==Page::Graphics) {
        const std::string label="CURRENT EYE TARGET";
        const std::string size=m_settings.currentEyeWidth?
          std::to_string(m_settings.currentEyeWidth)+" x "+std::to_string(m_settings.currentEyeHeight)+" pixels":
          "Not available";
        Type(210-TextWidth(label,18)/2,302,label,18,inkMuted);
        Type(210-TextWidth(size,24)/2,327,size,24,navy);
        const std::string timing=PendingQuality()?"APPLIES ON ENTER GAME":
          m_settings.startup && m_settings.startupRestartAvailable?"SELECT BEFORE ENTERING GAME":"RESTART TO CHANGE QUALITY";
        Type(210-TextWidth(timing,17)/2,584,timing,17,inkMuted);
      }
    }
    Rounded(23,703,972,49,10,MakeColour(4,22,39,235));
    Ring(50,727,11,orange);Type(69,710,"Sticks / arrows",23,cream);
    Rounded(315,714,25,24,4,MakeColour(36,115,83));Type(321,711,"A",23,silver);Type(353,710,"Cross / Enter",23,cream);
    Rounded(552,714,25,24,4,MakeColour(161,62,44));Type(559,711,"B",23,silver);
    Type(590,710,"Circle / Back",23,cream);Type(799,713,"< / >  ADJUST",21,gold);
    if(m_confirmExit) {
      ChromeFrame(166,238,692,282,18);Rounded(174,246,676,266,10,MakeColour(4,23,43));Rect(194,262,636,4,orange);
      Type(208,282,"END THIS RIDE?",42,cream,true);Type(208,361,"A / Cross / Enter - Exit HarleyVR",30,gold);
      Type(208,417,"B / Circle / Back - Keep riding",30,silver);Type(208,474,"Release, then press to confirm.",23,muted);
    }
    if(m_confirmArcade) {
      ChromeFrame(110,214,804,350,18);Rounded(118,222,788,334,10,MakeColour(4,23,43));Rect(138,238,748,4,orange);
      Type(150,258,m_confirmArcadeReturn?"Return to HarleyVR?":"Open Arcade Setup?",40,cream,true);
      Paragraph(150,326,m_confirmArcadeReturn?
        "Restart the arcade. Saved cabinet settings remain; unapplied edits may be lost. Your save slots are unchanged.":m_settings.startup?
        "Leave the startup menu and open the original cabinet settings.":
        "This ends your current ride and opens the original arcade settings.",700,m_confirmArcadeReturn?27:31,silver,m_confirmArcadeReturn?31:36);
      Rounded(m_arcadeContinue?515:146,425,358,56,8,orange);
      Type(167,433,m_confirmArcadeReturn?"Stay in Setup":m_settings.startup?"Back":"Keep Riding",30,m_arcadeContinue?silver:navy);
      Type(537,433,m_confirmArcadeReturn?"Restart to Startup":m_settings.startup?"Open Setup":"End Ride & Open Setup",28,m_arcadeContinue?navy:silver);
      Type(150,506,"Left/right: choose   A / Cross: select   B / Circle: cancel",24,cream);
    }
    for(int y=0;y<Height();++y)for(int x=0;x<Width();++x) {
      if(x>=12 && x<Width()-12 && y>=12 && y<Height()-12)continue;
      const int cx=x<12?12:x>=Width()-12?Width()-13:x,cy=y<12?12:y>=Height()-12?Height()-13:y;
      if((x-cx)*(x-cx)+(y-cy)*(y-cy)>144)Pixel(x,y,{0,0,0,0});
    }
  }

  Settings m_settings;
  Page m_page=Page::Main;
  std::vector<uint8_t> m_pixels, m_background;
  float m_backgroundAspect=4.0f/3.0f;
  bool m_open=false,m_dirty=true,m_confirmExit=false,m_primed=false;
  bool m_confirmArcade=false,m_arcadeContinue=false,m_confirmArcadeReturn=false;
  bool m_previewPullback=false;
  int m_selected=0;
  unsigned m_previous=0,m_blocked=0,m_repeat=0;
  uint64_t m_revision=1,m_lastTime=0,m_nextRepeat=0;
};
}
#endif
