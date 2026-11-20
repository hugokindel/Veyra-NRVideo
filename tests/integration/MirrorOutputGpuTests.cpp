// Mirror output window: the presenter draws the same final frame into a second
// borrowed window. Checks attach, pixel parity (SDR 8-bit and HDR10 10-bit),
// hidden-window skips, settled resize, the paused re-present, detach/reattach
// and that the D3D12 debug layer stays clean. Bounded; private log directory.
#include "veyra/engine/VideoPresenter.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/Log.h"
#include "veyra/sink/ImageExportSink.h"
#include <d3d12sdklayers.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
#include "HdrCopyPresentationFixture.h"

namespace {
void pump(){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
bool check(bool condition,const char* what,bool& ok){std::cout<<"MIRROR_CHECK "<<what<<" passed="<<condition<<std::endl;ok=ok&&condition;return condition;}
}

int wmain(int argc,wchar_t** argv){
    using namespace veyra;if(argc!=2)return 2;
    const std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);Logger::instance().openFile((dir/L"engine.log").wstring());
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    if(!ctx.initialize(device,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    gfx::ComPtr<ID3D12InfoQueue> messages;ctx.device()->QueryInterface(IID_PPV_ARGS(&messages));if(!messages)return 2;messages->ClearStoredMessages();
    HWND window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Veyra mirror test player",WS_POPUP|WS_VISIBLE,30,30,320,180,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND mirror=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Veyra mirror test",WS_POPUP|WS_VISIBLE,380,30,240,120,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window||!mirror)return 2;
    pump();
    engine::MirrorOutputLink link;bool ok=true;
    engine::VideoPresenter presenter;presenter.setMirror(&link);

    // --- SDR: attach, parity, hidden skip, settled resize, paused re-present, detach.
    {
        AVFrame* frame=av_frame_alloc();frame->format=AV_PIX_FMT_RGBA;frame->width=320;frame->height=180;
        if(av_frame_get_buffer(frame,32)<0)return 2;
        pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc desc;
        desc.sourceWidth=desc.workWidth=desc.nrWidth=320;desc.sourceHeight=desc.workHeight=desc.nrHeight=180;
        desc.rgbInput=true;desc.enableNr=desc.enableFg=desc.enableSr=false;desc.noFeatures=desc.noNgx=true;
        ok=graph.initialize(desc)&&graph.createViews()&&presenter.open(ctx,window,graph);
        check(ok,"sdr-open",ok);
        pipeline::EnhanceGraph::FrameOutputs out;unsigned frameNo=0;
        const auto presentFrame=[&](uint8_t red,bool retain=false){
            if(!retain){
                for(int y=0;y<frame->height;++y)for(int x=0;x<frame->width;++x){auto* p=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;p[0]=red;p[1]=90;p[2]=140;p[3]=255;}
                out={};++frameNo;
                if(!graph.process(frame,frameNo*16.666667,frameNo==1,out,frameNo))return false;
            }
            const bool presented=presenter.present(ctx,ring,graph,out.videoSlot,false,true,0,false,.5f,out.batch.identity,{},-1,retain);
            pump();return presented;
        };
        // Without a requested window the mirror stays off.
        ok=ok&&presentFrame(40);
        check(engine::MirrorState(link.state.load())==engine::MirrorState::Off&&!presenter.mirrorAttached(),"off-without-request",ok);
        link.window.store(mirror);
        for(unsigned i=0;i<12&&ok;++i){ok=presentFrame(uint8_t(60+i));std::this_thread::sleep_for(std::chrono::milliseconds(17));}
        check(presenter.mirrorAttached()&&engine::MirrorState(link.state.load())==engine::MirrorState::Active,"attached-active",ok);
        check(link.presented.load()>0&&link.width.load()==240&&link.height.load()==120,"presented-at-client-size",ok);
        std::cout<<"MIRROR_SDR presented="<<link.presented.load()<<" skipped="<<link.skipped.load()<<std::endl;
        // Parity: present one known colour, let the mirror take it, compare centres.
        for(unsigned i=0;i<4&&ok;++i){ok=presentFrame(200);std::this_thread::sleep_for(std::chrono::milliseconds(20));}
        {
            auto shown=presenter.mirrorPresentedResourceForTest();sink::RgbaImage image;
            const bool read=shown&&sink::readRgba8(ctx,ring,shown.Get(),image);
            int red=-1;if(read){const size_t at=(size_t(image.height/2)*image.width+image.width/2)*4;red=image.pixels[at];}
            std::cout<<"MIRROR_SDR_PIXEL size="<<image.width<<"x"<<image.height<<" red="<<red<<std::endl;
            check(read&&image.width==240&&image.height==120&&std::abs(red-200)<=2,"sdr-pixel-parity",ok);
            // 240x120 is wider than 16:9: the picture is pillarboxed, the edge column black.
            if(read){const size_t edge=(size_t(image.height/2)*image.width+1)*4;check(image.pixels[edge]<=2,"contain-fit-pillarbox",ok);}
        }
        // Hidden: frames continue on the player, the mirror only counts skips.
        ShowWindow(mirror,SW_HIDE);pump();
        const auto presentedBefore=link.presented.load(),skippedBefore=link.skipped.load();
        for(unsigned i=0;i<5&&ok;++i)ok=presentFrame(uint8_t(100+i));
        check(link.presented.load()==presentedBefore&&link.skipped.load()>=skippedBefore+5,"hidden-skips",ok);
        ShowWindow(mirror,SW_SHOWNOACTIVATE);pump();
        // Settled resize: applied once the size is stable for 120 ms.
        SetWindowPos(mirror,nullptr,0,0,320,180,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);pump();
        for(unsigned i=0;i<16&&ok;++i){ok=presentFrame(uint8_t(120+i));std::this_thread::sleep_for(std::chrono::milliseconds(17));}
        check(link.width.load()==320&&link.height.load()==180,"settled-resize",ok);
        // Paused: an unchanged retained frame is not re-presented, a mirror resize re-presents it once.
        ok=ok&&presentFrame(150);std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ok=ok&&presentFrame(150,true);
        const auto pausedMain=presenter.submittedCount(),pausedMirror=link.presented.load();
        ok=ok&&presentFrame(150,true);
        check(presenter.submittedCount()==pausedMain&&link.presented.load()==pausedMirror,"paused-retained",ok);
        SetWindowPos(mirror,nullptr,0,0,300,160,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);pump();
        for(unsigned i=0;i<12&&ok&&link.presented.load()==pausedMirror;++i){ok=presentFrame(150,true);std::this_thread::sleep_for(std::chrono::milliseconds(20));}
        check(link.width.load()==300&&link.presented.load()>pausedMirror,"paused-resize-represents",ok);
        // Detach and reattach.
        link.window.store(nullptr);ok=ok&&presentFrame(160);
        check(!presenter.mirrorAttached()&&engine::MirrorState(link.state.load())==engine::MirrorState::Off&&link.width.load()==0,"detached",ok);
        link.window.store(mirror);
        for(unsigned i=0;i<4&&ok;++i){ok=presentFrame(uint8_t(170+i));std::this_thread::sleep_for(std::chrono::milliseconds(17));}
        check(presenter.mirrorAttached(),"reattached",ok);
        ok=presenter.prepareGraphChange()&&ring.drainQueue()&&ok;out={};graph.shutdown();
        presenter.close();
        check(engine::MirrorState(link.state.load())==engine::MirrorState::Waiting&&link.width.load()==0,"close-waiting",ok);
        av_frame_free(&frame);
    }

    // --- HDR10: same 10-bit code values in the mirror as in the graph output.
    {
        SetWindowPos(mirror,nullptr,0,0,320,180,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);pump();
        AVFrame* frame=av_frame_alloc();frame->format=AV_PIX_FMT_P010LE;frame->width=640;frame->height=360;
        frame->color_range=AVCOL_RANGE_MPEG;frame->colorspace=AVCOL_SPC_BT2020_NCL;frame->color_primaries=AVCOL_PRI_BT2020;frame->color_trc=AVCOL_TRC_SMPTE2084;
        ok=av_frame_get_buffer(frame,32)>=0&&ok;
        pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc desc;
        desc.sourceWidth=desc.workWidth=desc.nrWidth=640;desc.sourceHeight=desc.workHeight=desc.nrHeight=360;
        desc.hdrInput=desc.hdrOutput=true;desc.captureBitDepth=10;desc.enableNr=desc.enableFg=desc.enableSr=false;desc.noNgx=true;
        ok=ok&&graph.initialize(desc)&&graph.createViews()&&presenter.open(ctx,window,graph);
        check(ok&&graph.hdr10Output(),"hdr10-open",ok);
        for(int y=0;y<360;++y)for(int x=0;x<640;++x)reinterpret_cast<uint16_t*>(frame->data[0]+size_t(y)*frame->linesize[0])[x]=uint16_t(520<<6);
        for(int y=0;y<180;++y)for(int x=0;x<640;++x)reinterpret_cast<uint16_t*>(frame->data[1]+size_t(y)*frame->linesize[1])[x]=uint16_t(512<<6);
        pipeline::EnhanceGraph::FrameOutputs out;
        for(unsigned i=0;i<8&&ok;++i){
            out={};ok=graph.process(frame,i*16.666667,i==0,out,i+1)&&presenter.present(ctx,ring,graph,out.videoSlot,false,false,0,false,.5f,out.batch.identity);
            pump();std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        auto shown=presenter.mirrorPresentedResourceForTest();std::vector<uint32_t> actual,expected;unsigned w=0,h=0,ew=0,eh=0;
        const bool read=shown&&readHdrWords(ctx,ring,shown.Get(),actual,w,h)&&readHdrWords(ctx,ring,graph.videoFrameResource(out.videoSlot),expected,ew,eh);
        int maxError=-1;
        if(read){maxError=0;const auto a=actual[size_t(h/2)*w+w/2],b=expected[size_t(eh/2)*ew+ew/2];
            for(unsigned c=0;c<3;++c)maxError=std::max(maxError,std::abs(int((a>>(10*c))&1023)-int((b>>(10*c))&1023)));}
        std::cout<<"MIRROR_HDR10 presented="<<link.presented.load()<<" size="<<w<<"x"<<h<<" max10BitError="<<maxError<<std::endl;
        check(read&&maxError<=1,"hdr10-pixel-parity",ok);
        shown.Reset();
        ok=presenter.prepareGraphChange()&&ring.drainQueue()&&ok;out={};graph.shutdown();
        presenter.close();av_frame_free(&frame);
    }
    link.window.store(nullptr);
    unsigned errors=0;for(UINT64 i=0;i<messages->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T size=0;messages->GetMessage(i,nullptr,&size);std::vector<char> buffer(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(buffer.data());messages->GetMessage(i,m,&size);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}
    ok=ok&&errors==0&&SUCCEEDED(ctx.device()->GetDeviceRemovedReason());messages.Reset();ring.shutdown();ctx.shutdown();
    DestroyWindow(mirror);DestroyWindow(window);
    std::cout<<"MIRROR_RESULT passed="<<ok<<" debugErrors="<<errors<<std::endl;return ok?0:1;
}
