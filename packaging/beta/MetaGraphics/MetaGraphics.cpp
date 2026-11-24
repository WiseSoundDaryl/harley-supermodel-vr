// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_NO_PROTOTYPES
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "Panel.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

using Microsoft::WRL::ComPtr;
using namespace HarleyGraphics;
using Clock = std::chrono::steady_clock;

namespace {
void Check(XrResult value, const char* operation) {
    if (value==XR_SESSION_LOSS_PENDING) throw std::runtime_error("The Meta headset session was lost. Reconnect Meta Link and try again.");
    if (XR_FAILED(value)) throw std::runtime_error(std::string(operation)+" failed (OpenXR "+std::to_string(value)+").");
}
void CheckHR(HRESULT value, const char* operation) {
    if (FAILED(value)) throw std::runtime_error(std::string(operation)+" failed (HRESULT "+std::to_string(value)+").");
}

#define XR_FUNCTIONS(X) \
    X(xrDestroyInstance) X(xrGetSystem) X(xrGetD3D11GraphicsRequirementsKHR) \
    X(xrCreateSession) X(xrDestroySession) X(xrCreateReferenceSpace) X(xrDestroySpace) X(xrLocateSpace) \
    X(xrCreateActionSet) X(xrDestroyActionSet) X(xrCreateAction) X(xrStringToPath) X(xrSuggestInteractionProfileBindings) \
    X(xrAttachSessionActionSets) X(xrSyncActions) X(xrGetActionStateBoolean) X(xrGetActionStateVector2f) \
    X(xrEnumerateSwapchainFormats) X(xrCreateSwapchain) X(xrDestroySwapchain) X(xrEnumerateSwapchainImages) \
    X(xrAcquireSwapchainImage) X(xrWaitSwapchainImage) X(xrReleaseSwapchainImage) \
    X(xrPollEvent) X(xrBeginSession) X(xrEndSession) X(xrWaitFrame) X(xrBeginFrame) X(xrEndFrame) X(xrRequestExitSession)

class Picker {
#ifdef HARLEY_GRAPHICS_TEST
    friend struct FrameFixture;
#endif
    HMODULE loader{};
    PFN_xrGetInstanceProcAddr getProc{};
#define MEMBER(name) PFN_##name name{};
    XR_FUNCTIONS(MEMBER)
#undef MEMBER
    XrInstance instance{};
    XrSession session{};
    XrSpace local{}, head{};
    XrSwapchain chain{};
    XrActionSet actionSet{};
    XrAction stick{}, accept{}, cancel{};
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool running = false, anchored = false, bgra = true, finished = false, sawActiveController = false;
    XrTime reanchorAt = 0;
    XrPosef anchor{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    std::vector<XrSwapchainImageD3D11KHR> images;
    Selection selection;
    int uploaded = -1;

    template<class Function> void Load(const char* name, Function& out) {
        PFN_xrVoidFunction fn{};
        Check(getProc(instance,name,&fn),name);
        out=reinterpret_cast<Function>(fn);
        if (!out) throw std::runtime_error(std::string("Missing OpenXR function ")+name+".");
    }
    XrPath Path(const char* name) {
        XrPath path{};
        Check(xrStringToPath(instance,name,&path),name);
        return path;
    }
    void CreateAction(XrAction& action, XrActionType type, const char* name, const char* label) {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        info.actionType=type;
        std::snprintf(info.actionName,sizeof(info.actionName),"%s",name);
        std::snprintf(info.localizedActionName,sizeof(info.localizedActionName),"%s",label);
        Check(xrCreateAction(actionSet,&info,&action),"Create controller action");
    }
    void SetupControls() {
        XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
        std::snprintf(set.actionSetName,sizeof(set.actionSetName),"graphics");
        std::snprintf(set.localizedActionSetName,sizeof(set.localizedActionSetName),"Graphics setup");
        Check(xrCreateActionSet(instance,&set,&actionSet),"Create controller action set");
        CreateAction(stick,XR_ACTION_TYPE_VECTOR2F_INPUT,"quality","Select graphics quality");
        CreateAction(accept,XR_ACTION_TYPE_BOOLEAN_INPUT,"start","Start ride");
        CreateAction(cancel,XR_ACTION_TYPE_BOOLEAN_INPUT,"cancel","Cancel");
        const XrActionSuggestedBinding bindings[]={
            {stick,Path("/user/hand/right/input/thumbstick")},
            {accept,Path("/user/hand/right/input/a/click")},
            {cancel,Path("/user/hand/right/input/b/click")}
        };
        XrInteractionProfileSuggestedBinding profile{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        profile.interactionProfile=Path("/interaction_profiles/oculus/touch_controller");
        profile.countSuggestedBindings=3; profile.suggestedBindings=bindings;
        Check(xrSuggestInteractionProfileBindings(instance,&profile),"Bind Meta Touch controllers");
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach.countActionSets=1; attach.actionSets=&actionSet;
        Check(xrAttachSessionActionSets(session,&attach),"Attach controller action set");
    }
    void GraphicsSession(XrSystemId system) {
        XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        Check(xrGetD3D11GraphicsRequirementsKHR(instance,system,&requirements),"Read Meta graphics requirements");
        ComPtr<IDXGIFactory1> factory;
        CheckHR(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())),"Create DXGI factory");
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i=0;;++i) {
            const auto result=factory->EnumAdapters1(i,adapter.ReleaseAndGetAddressOf());
            if (result==DXGI_ERROR_NOT_FOUND) throw std::runtime_error("Could not find the graphics adapter selected by Meta Link.");
            CheckHR(result,"Find headset graphics adapter");
            DXGI_ADAPTER_DESC1 desc{};
            CheckHR(adapter->GetDesc1(&desc),"Read graphics adapter");
            if (desc.AdapterLuid.HighPart==requirements.adapterLuid.HighPart &&
                desc.AdapterLuid.LowPart==requirements.adapterLuid.LowPart) break;
        }
        D3D_FEATURE_LEVEL actual;
        CheckHR(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,device.GetAddressOf(),&actual,context.GetAddressOf()),"Create headset D3D11 device");
        if (actual<requirements.minFeatureLevel) throw std::runtime_error("Headset graphics adapter lacks the required D3D11 feature level.");
        XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
        binding.device=device.Get();
        XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
        info.next=&binding; info.systemId=system;
        Check(xrCreateSession(instance,&info,&session),"Open headset graphics session");

        XrReferenceSpaceCreateInfo sp{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        sp.poseInReferenceSpace.orientation.w=1; sp.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
        Check(xrCreateReferenceSpace(session,&sp,&local),"Create room reference space");
        sp.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
        Check(xrCreateReferenceSpace(session,&sp,&head),"Create headset reference space");
    }
    void SetupPanel() {
        uint32_t count=0;
        Check(xrEnumerateSwapchainFormats(session,0,&count,nullptr),"Count panel image formats");
        std::vector<int64_t> formats(count);
        Check(xrEnumerateSwapchainFormats(session,count,&count,formats.data()),"Read panel image formats");
        int64_t format=0;
        for (auto candidate : {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM}) {
            if (std::find(formats.begin(),formats.end(),candidate)!=formats.end()) { format=candidate; break; }
        }
        if (!format) throw std::runtime_error("Meta supplied no supported 32-bit panel image format.");
        bgra=format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format==DXGI_FORMAT_B8G8R8A8_UNORM;
        XrSwapchainCreateInfo swap{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        swap.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        swap.format=format; swap.sampleCount=1; swap.width=width; swap.height=height;
        swap.faceCount=1; swap.arraySize=1; swap.mipCount=1;
        Check(xrCreateSwapchain(session,&swap,&chain),"Create graphics panel texture");
        Check(xrEnumerateSwapchainImages(chain,0,&count,nullptr),"Count panel images");
        if (!count) throw std::runtime_error("Meta supplied an empty panel swapchain.");
        images.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        Check(xrEnumerateSwapchainImages(chain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"Read panel images");
    }
    void UploadPanel() {
        // Update only when selection changes; the compositor reuses the last released image.
        if (selection.index==uploaded) return;
        const auto pixels=PanelPixels(selection.index,bgra);
        uint32_t index=0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        Check(xrAcquireSwapchainImage(chain,&acquire,&index),"Acquire panel image");
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout=XR_INFINITE_DURATION;
        Check(xrWaitSwapchainImage(chain,&wait),"Wait for panel image");
        if (index>=images.size() || !images[index].texture) throw std::runtime_error("Invalid panel texture from runtime.");
        context->UpdateSubresource(images[index].texture,0,nullptr,pixels.data(),width*4,0);
        context->Flush();
        CheckHR(device->GetDeviceRemovedReason(),"Update panel image");
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        Check(xrReleaseSwapchainImage(chain,&release),"Release panel image");
        uploaded=selection.index;
    }
    void Events() {
        for (;;) {
            XrEventDataBuffer e{XR_TYPE_EVENT_DATA_BUFFER};
            const auto result=xrPollEvent(instance,&e);
            if (result==XR_EVENT_UNAVAILABLE) break;
            Check(result,"Read OpenXR events");
            if (e.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
                throw std::runtime_error("The Meta runtime disconnected during graphics setup. Reconnect and try again.");
            if (e.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                const auto& change=reinterpret_cast<const XrEventDataReferenceSpaceChangePending&>(e);
                if (change.session==session && change.referenceSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL) reanchorAt=change.changeTime;
            }
            if (e.type!=XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) continue;
            const auto& change=reinterpret_cast<const XrEventDataSessionStateChanged&>(e);
            if (change.session!=session) continue;
            state=change.state;
            if (state!=XR_SESSION_STATE_FOCUSED) selection.armed=false;
            if (state==XR_SESSION_STATE_READY && !running) {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                Check(xrBeginSession(session,&begin),"Begin headset setup"); running=true;
            } else if (state==XR_SESSION_STATE_STOPPING && running) {
                Check(xrEndSession(session),"End headset setup"); running=false; finished=true;
            } else if (state==XR_SESSION_STATE_LOSS_PENDING) {
                throw std::runtime_error("Meta lost the headset during graphics setup. Reconnect and try again.");
            } else if (state==XR_SESSION_STATE_EXITING) {
                finished=true;
            }
        }
    }
    bool AnchorAtHead(XrTime time) {
        if (reanchorAt && time>=reanchorAt) { anchored=false; reanchorAt=0; selection.armed=false; }
        if (anchored) return true;
        if (state!=XR_SESSION_STATE_FOCUSED) return false;
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        Check(xrLocateSpace(head,local,time,&loc),"Locate graphics menu");
        constexpr auto valid=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT|
            XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
        if ((loc.locationFlags & valid)!=valid) return false;
        const auto q=loc.pose.orientation;
        const float fx=-2*(q.x*q.z+q.w*q.y), fz=-1+2*(q.x*q.x+q.y*q.y);
        if (std::hypot(fx,fz)<.2f) return false;
        const float yaw=std::atan2(-fx,-fz);
        anchor.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
        anchor.position=loc.pose.position;
        anchor.position.x-=1.25f*std::sin(yaw);
        anchor.position.y-=.035f;
        anchor.position.z-=1.25f*std::cos(yaw);
        anchored=true;
        return true;
    }
    int PollControls() {
        XrActiveActionSet active{actionSet,XR_NULL_PATH};
        XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
        sync.countActiveActionSets=1; sync.activeActionSets=&active;
        const auto result=xrSyncActions(session,&sync);
        if (result==XR_SESSION_NOT_FOCUSED) { selection.armed=false; return -1; }
        Check(result,"Read headset controls");
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        XrActionStateVector2f axis{XR_TYPE_ACTION_STATE_VECTOR2F};
        get.action=stick; Check(xrGetActionStateVector2f(session,&get,&axis),"Read quality selector");
        XrActionStateBoolean a{XR_TYPE_ACTION_STATE_BOOLEAN}, b{XR_TYPE_ACTION_STATE_BOOLEAN};
        get.action=accept; Check(xrGetActionStateBoolean(session,&get,&a),"Read Start ride");
        get.action=cancel; Check(xrGetActionStateBoolean(session,&get,&b),"Read Cancel");
        const bool controllerActive=axis.isActive && a.isActive && b.isActive;
        sawActiveController |= controllerActive;
        return selection.Update(controllerActive,axis.currentState.x,axis.currentState.y,a.currentState,b.currentState);
    }
public:
    explicit Picker(int preset):selection{preset} {}
    ~Picker() {
        if (chain && xrDestroySwapchain) xrDestroySwapchain(chain);
        if (head && xrDestroySpace) xrDestroySpace(head);
        if (local && xrDestroySpace) xrDestroySpace(local);
        if (session && xrDestroySession) xrDestroySession(session);
        if (actionSet && xrDestroyActionSet) xrDestroyActionSet(actionSet);
        if (instance && xrDestroyInstance) xrDestroyInstance(instance);
        if (loader) FreeLibrary(loader);
    }
    void Initialize() {
        std::wstring path(32768,L'\0');
        const auto used=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        if (!used || used>=path.size()) throw std::runtime_error("Could not locate the headset menu directory.");
        path.resize(used); path.resize(path.find_last_of(L"\\/")+1); path+=L"libopenxr_loader.dll";
        loader=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!loader) throw std::runtime_error("The OpenXR loader beside the Meta graphics menu could not be loaded.");
        getProc=reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader,"xrGetInstanceProcAddr"));
        if (!getProc) throw std::runtime_error("The OpenXR loader is missing xrGetInstanceProcAddr.");
        PFN_xrCreateInstance create{}; Load("xrCreateInstance",create);
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
        std::snprintf(info.applicationInfo.applicationName,sizeof(info.applicationInfo.applicationName),"HarleyVR Meta graphics");
        info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
        const char* extension=XR_KHR_D3D11_ENABLE_EXTENSION_NAME;
        info.enabledExtensionCount=1; info.enabledExtensionNames=&extension;
        Check(create(&info,&instance),"Initialize Meta OpenXR D3D11 (enter Quest Link or Air Link, or connect your Rift)");
#define LOAD(name) Load(#name,name);
        XR_FUNCTIONS(LOAD)
#undef LOAD
        XrSystemGetInfo sys{XR_TYPE_SYSTEM_GET_INFO}; sys.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrSystemId system{};
        Check(xrGetSystem(instance,&sys,&system),"Find connected Meta headset");
        GraphicsSession(system);
        SetupControls();
        SetupPanel();
    }
    int Run() {
        auto start=Clock::now(), requestedAt=start;
        bool sawFocusedPanel=false;
        int choice=-1;
        for (;;) {
            Events();
            if (finished || (choice>=0 && Clock::now()-requestedAt>std::chrono::seconds(3))) break;
            if (!sawFocusedPanel && Clock::now()-start>std::chrono::seconds(45))
                throw std::runtime_error("The headset menu did not receive focused tracking. Enter Meta Link and check headset tracking, or use the desktop graphics choice.");
            if (sawFocusedPanel && !sawActiveController && Clock::now()-start>std::chrono::seconds(60))
                throw std::runtime_error("The graphics menu could not read the right Touch controller. Wake it and retry, or choose the desktop graphics option.");
            if (!running) { Sleep(10); continue; }
            XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
            XrFrameState frame{XR_TYPE_FRAME_STATE};
            Check(xrWaitFrame(session,&wait,&frame),"Wait for headset frame");
            XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
            Check(xrBeginFrame(session,&begin),"Begin headset frame");
            XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
            quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH; quad.space=local;
            quad.subImage.swapchain=chain; quad.subImage.imageRect.extent={width,height};
            quad.size={1.16f,1.16f*height/width};
            XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
            end.displayTime=frame.predictedDisplayTime; end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            auto layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
            int input=-1;
            if (frame.shouldRender && choice<0 && AnchorAtHead(frame.predictedDisplayTime)) {
                if (state==XR_SESSION_STATE_FOCUSED && sawFocusedPanel) input=PollControls();
                if (state==XR_SESSION_STATE_FOCUSED) sawFocusedPanel=true;
                UploadPanel();
                quad.pose=anchor; end.layerCount=1; end.layers=&layer;
            } else { selection.armed=false; }
            Check(xrEndFrame(session,&end),"Submit graphics menu");
            if (input>=0) {
                choice=input; requestedAt=Clock::now();
                Check(xrRequestExitSession(session),"Close headset menu before starting the ride");
            }
        }
        return choice<0 ? 0 : choice;
    }
};
}

#ifndef HARLEY_GRAPHICS_TEST
int main(int argc,char** argv) {
    if (argc!=3 || std::strcmp(argv[1],"--preset") || PresetIndex(argv[2])<0) {
        std::fputs("Expected --preset Performance, Balanced, or Quality.\n",stderr); return 1;
    }
    try {
        Picker picker(PresetIndex(argv[2])); picker.Initialize(); return picker.Run();
    } catch (const std::exception& e) {
        std::fprintf(stderr,"%s\n",e.what()); return 1;
    }
}
#endif
