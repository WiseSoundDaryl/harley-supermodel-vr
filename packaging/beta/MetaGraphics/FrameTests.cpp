// SPDX-License-Identifier: GPL-3.0-or-later
// Fake frame callbacks drive the production renderer. A stock D3D11 software
// device backs the image. No OpenXR loader, headset, window or emulator is used.
#define HARLEY_GRAPHICS_TEST
#include "MetaGraphics.cpp"
#include <iostream>

namespace {
struct FrameFixture {
    static inline int checks,frame,releases,submits,eventStep,begins,ends,requested;
    static inline bool takeCancel;
    static inline XrSession dummy=reinterpret_cast<XrSession>(1);
    static inline XrSpace room=reinterpret_cast<XrSpace>(2);
    static inline XrAction yes=reinterpret_cast<XrAction>(3);
    static inline Picker* current;
    static inline ComPtr<ID3D11Texture2D> texture,readback;

    static void Expect(bool value,const char* msg) {
        if (!value) throw std::logic_error(msg);
        ++checks;
    }
    static XrResult XRAPI_PTR Poll(XrInstance,XrEventDataBuffer* out) {
        XrSessionState state=XR_SESSION_STATE_UNKNOWN;
        if (eventStep==0) { ++eventStep; state=XR_SESSION_STATE_READY; }
        else if (eventStep==1) { ++eventStep; state=XR_SESSION_STATE_FOCUSED; }
        else if (eventStep==2 && requested) { ++eventStep; state=XR_SESSION_STATE_STOPPING; }
        if (state==XR_SESSION_STATE_UNKNOWN) return XR_EVENT_UNAVAILABLE;
        XrEventDataSessionStateChanged change{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
        change.session=dummy; change.state=state;
        std::memcpy(out,&change,sizeof(change)); return XR_SUCCESS;
    }
    static void Wire(Picker& p) {
        current=&p;
        p.session=dummy; p.local=room; p.accept=yes;
        p.xrPollEvent=Poll;
        p.xrBeginSession=[](XrSession,const XrSessionBeginInfo* s) { ++begins; Expect(s->primaryViewConfigurationType==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,"stereo session"); return XR_SUCCESS; };
        p.xrEndSession=[](XrSession) { ++ends; return XR_SUCCESS; };
        p.xrRequestExitSession=[](XrSession) { ++requested;return XR_SUCCESS; };
        p.xrWaitFrame=[](XrSession,const XrFrameWaitInfo*,XrFrameState* f) {
            ++frame; Expect(frame<12,"unbounded frame loop");
            f->predictedDisplayTime=frame*1000;f->shouldRender=frame!=1;return XR_SUCCESS;
        };
        p.xrBeginFrame=[](XrSession,const XrFrameBeginInfo*) {return XR_SUCCESS;};
        p.xrLocateSpace=[](XrSpace,XrSpace,XrTime t,XrSpaceLocation* l) {
            Expect(t==frame*1000,"predicted display time");
            l->locationFlags=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
            if (frame!=2) l->locationFlags|=XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
            l->pose.position={1,2,3};l->pose.orientation={0,.70710678f,0,.70710678f};return XR_SUCCESS;
        };
        p.xrSyncActions=[](XrSession,const XrActionsSyncInfo* s) {Expect(s->countActiveActionSets==1,"attached action set");return XR_SUCCESS;};
        p.xrGetActionStateVector2f=[](XrSession,const XrActionStateGetInfo*,XrActionStateVector2f* a) {
            a->isActive=true;a->currentState={frame==6 || frame==7?1.f:0.f,0};return XR_SUCCESS;
        };
        p.xrGetActionStateBoolean=[](XrSession,const XrActionStateGetInfo* s,XrActionStateBoolean* a) {
            a->isActive=true;a->currentState=(frame==4 && s->action==yes) || (frame==9 && (takeCancel!=(s->action==yes)));return XR_SUCCESS;
        };
        p.xrAcquireSwapchainImage=[](XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* i){*i=0;return XR_SUCCESS;};
        p.xrWaitSwapchainImage=[](XrSwapchain,const XrSwapchainImageWaitInfo*){return XR_SUCCESS;};
        p.xrReleaseSwapchainImage=[](XrSwapchain,const XrSwapchainImageReleaseInfo*){
            // Validate an actual readback after the update-subresource path.
            const auto& ctx=current->context;
            ctx->CopyResource(readback.Get(),texture.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            CheckHR(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"Readback test panel");
            const auto* row=reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(mapped.pData)+254*mapped.RowPitch);
            const int selected=releases==0?1:2;
            const auto pixel=row[51+314*selected];
            ctx->Unmap(readback.Get(),0);
            Expect(pixel==0xffffcf5b,"wrong selected preset on headset texture");
            ++releases;return XR_SUCCESS;
        };
        p.xrEndFrame=[](XrSession,const XrFrameEndInfo* e) {
            Expect(e->displayTime==frame*1000,"wrong composition time");
            if (frame<3) Expect(e->layerCount==0,"submitted when hidden or invalid origin");
            else {
                Expect(e->layerCount==1 && e->layers && e->layers[0],"missing panel");
                const auto& q=*reinterpret_cast<const XrCompositionLayerQuad*>(e->layers[0]);
                Expect(q.type==XR_TYPE_COMPOSITION_LAYER_QUAD && q.eyeVisibility==XR_EYE_VISIBILITY_BOTH && q.space==room,"quad space or eyes");
                Expect(q.subImage.imageRect.extent.width==width && q.subImage.imageRect.extent.height==height,"quad resolution");
                Expect(std::fabs(q.pose.position.x+.25f)<1e-4f && std::fabs(q.pose.position.y-1.965f)<1e-4f && std::fabs(q.pose.position.z-3)<1e-4f,"not anchored in front of seated viewer");
                ++submits;
            }
            return XR_SUCCESS;
        };
        CheckHR(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
            p.device.GetAddressOf(),nullptr,p.context.GetAddressOf()),"Create headless test device");
        D3D11_TEXTURE2D_DESC d{};
        d.Width=width;d.Height=height;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.Usage=D3D11_USAGE_DEFAULT;
        d.BindFlags=D3D11_BIND_RENDER_TARGET;
        CheckHR(p.device->CreateTexture2D(&d,nullptr,texture.ReleaseAndGetAddressOf()),"Create test image");
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        CheckHR(p.device->CreateTexture2D(&d,nullptr,readback.ReleaseAndGetAddressOf()),"Create test readback");
        p.images={{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR,nullptr,texture.Get()}};
    }
    static void Run(bool cancel) {
        frame=releases=submits=eventStep=begins=ends=requested=0;takeCancel=cancel;
        Picker p(1);Wire(p);
        Expect(p.Run()==(cancel?0:100),"unexpected process decision");
        Expect(frame==9 && begins==1 && requested==1 && ends==1,"unbalanced session lifecycle");
        Expect(submits==7 && releases==2,"unnecessary uploads or incorrect visibility");
        texture.Reset();readback.Reset();current=nullptr;
    }
    static void StartupCases() {
        Picker p(1);
        p.session=dummy; p.local=room; p.state=XR_SESSION_STATE_VISIBLE;
        Expect(!p.AnchorAtHead(10),"anchored before focus");
        p.anchored=true; p.selection.armed=true; p.reanchorAt=25;
        Expect(p.AnchorAtHead(24) && p.selection.armed,"reference space changed before effective time");
        Expect(!p.AnchorAtHead(25) && !p.selection.armed,"did not wait for focused recenter");
        p.xrPollEvent=[](XrInstance, XrEventDataBuffer* out) {
            XrEventDataSessionStateChanged change{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
            change.session=dummy;change.state=XR_SESSION_STATE_LOSS_PENDING;
            std::memcpy(out,&change,sizeof(change));return XR_SUCCESS;
        };
        bool failed=false;try { p.Events(); } catch(const std::runtime_error&) { failed=true; }
        Expect(failed,"unexpected successful exit on session loss");
        p.xrPollEvent=[](XrInstance, XrEventDataBuffer* out) {
            out->type=XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING;return XR_SUCCESS;
        };
        failed=false;try { p.Events(); } catch(const std::runtime_error&) { failed=true; }
        Expect(failed,"unexpected successful exit on instance loss");
    }
};
}

int main() {
    FrameFixture::StartupCases();
    FrameFixture::Run(false);FrameFixture::Run(true);
    std::cout<<FrameFixture::checks<<" synthetic OpenXR frame and D3D11 readback checks passed.\n";
}
