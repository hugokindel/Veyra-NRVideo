#include "MirrorWindow.h"
#include "veyra/Log.h"
#include <format>
#include <vector>
namespace veyra::ui {
namespace {
constexpr wchar_t kClassName[]=L"VeyraMirrorWindow";
constexpr UINT_PTR kCursorTimer=1;
constexpr DWORD kWindowedStyle=WS_OVERLAPPEDWINDOW;
constexpr DWORD kFullscreenStyle=WS_POPUP;
bool registerClass(WNDPROC proc){
    static const bool registered=[proc]{
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style=CS_DBLCLKS;
        wc.lpfnWndProc=proc;
        wc.hInstance=GetModuleHandleW(nullptr);
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName=kClassName;
        return RegisterClassExW(&wc)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
    }();
    return registered;
}
std::vector<HMONITOR> monitors(){
    std::vector<HMONITOR> out;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR m,HDC,LPRECT,LPARAM p)->BOOL{
        reinterpret_cast<std::vector<HMONITOR>*>(p)->push_back(m);return TRUE;},reinterpret_cast<LPARAM>(&out));
    return out;
}
RECT workArea(HMONITOR monitor){MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);return info.rcWork;}
RECT monitorArea(HMONITOR monitor){MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);return info.rcMonitor;}
// Half of the work area, centered: a starting size the user drags or maximizes.
RECT defaultRect(HMONITOR monitor){
    const RECT work=workArea(monitor);
    const LONG w=(work.right-work.left)/2,h=(work.bottom-work.top)/2;
    const LONG x=work.left+(work.right-work.left-w)/2,y=work.top+(work.bottom-work.top-h)/2;
    return {x,y,x+w,y+h};
}
}
MirrorWindow::~MirrorWindow(){destroy();}
bool MirrorWindow::show(HWND owner,const std::optional<Placement>& saved){
    if(!hwnd_){
        if(!registerClass(&MirrorWindow::windowProc)){veyra::log::error("mirror-ui",std::format("RegisterClassExW failed err={}",GetLastError()));return false;}
        HMONITOR ownerMonitor=owner?MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST):MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY);
        RECT rect=defaultRect(ownerMonitor);
        bool fullscreen=false;
        if(saved&&saved->windowed.right>saved->windowed.left&&saved->windowed.bottom>saved->windowed.top&&
           MonitorFromRect(&saved->windowed,MONITOR_DEFAULTTONULL)){
            rect=saved->windowed;fullscreen=saved->fullscreen;
        }else{
            // Prefer another screen (the TV next to the desk monitor).
            for(auto m:monitors())if(m!=ownerMonitor){rect=defaultRect(m);break;}
        }
        // Unowned on purpose: minimizing the player must not minimize the mirror.
        hwnd_=CreateWindowExW(WS_EX_APPWINDOW,kClassName,L"Veyra Mirror",kWindowedStyle,
            rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,GetModuleHandleW(nullptr),this);
        if(!hwnd_){veyra::log::error("mirror-ui",std::format("CreateWindowExW failed err={}",GetLastError()));return false;}
        if(owner){
            const HWND root=GetAncestor(owner,GA_ROOT);
            if(auto iconBig=reinterpret_cast<HICON>(SendMessageW(root,WM_GETICON,ICON_BIG,0)))SendMessageW(hwnd_,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(iconBig));
            if(auto iconSmall=reinterpret_cast<HICON>(SendMessageW(root,WM_GETICON,ICON_SMALL,0)))SendMessageW(hwnd_,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(iconSmall));
        }
        windowedRect_=rect;
        ShowWindow(hwnd_,SW_SHOWNORMAL);
        if(fullscreen)setFullscreen(true);
        veyra::log::info("mirror-ui",std::format("created hwnd=0x{:X} rect={},{} {}x{} fullscreen={}",reinterpret_cast<uintptr_t>(hwnd_),
            rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,fullscreen_));
    }else if(!IsWindowVisible(hwnd_)){
        ShowWindow(hwnd_,SW_SHOWNORMAL);
    }
    SetForegroundWindow(hwnd_);
    return true;
}
void MirrorWindow::hide(){
    if(!hwnd_)return;
    KillTimer(hwnd_,kCursorTimer);showCursor();
    ShowWindow(hwnd_,SW_HIDE);
}
MirrorWindow::Placement MirrorWindow::placement()const{
    Placement p;p.fullscreen=fullscreen_;
    if(hwnd_&&!fullscreen_&&!IsIconic(hwnd_)&&!IsZoomed(hwnd_)){RECT r{};GetWindowRect(hwnd_,&r);p.windowed=r;}
    else p.windowed=windowedRect_;
    return p;
}
void MirrorWindow::setFullscreen(bool on){
    if(!hwnd_||on==fullscreen_)return;
    if(on){
        if(!IsIconic(hwnd_)&&!IsZoomed(hwnd_))GetWindowRect(hwnd_,&windowedRect_);
        if(IsZoomed(hwnd_))ShowWindow(hwnd_,SW_RESTORE);
        const RECT m=monitorArea(MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST));
        SetWindowLongPtrW(hwnd_,GWL_STYLE,LONG_PTR(kFullscreenStyle|WS_VISIBLE));
        SetWindowPos(hwnd_,HWND_TOP,m.left,m.top,m.right-m.left,m.bottom-m.top,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
        fullscreen_=true;
        SetTimer(hwnd_,kCursorTimer,2000,nullptr);
    }else{
        KillTimer(hwnd_,kCursorTimer);showCursor();
        SetWindowLongPtrW(hwnd_,GWL_STYLE,LONG_PTR(kWindowedStyle|WS_VISIBLE));
        const RECT& r=windowedRect_;
        SetWindowPos(hwnd_,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_FRAMECHANGED|SWP_NOZORDER|SWP_SHOWWINDOW);
        fullscreen_=false;
    }
    veyra::log::info("mirror-ui",std::format("fullscreen={}",fullscreen_));
    if(onFullscreenChanged)onFullscreenChanged();
}
void MirrorWindow::destroy(){
    if(!hwnd_)return;
    const HWND window=hwnd_;hwnd_=nullptr;
    SetWindowLongPtrW(window,GWLP_USERDATA,0);
    DestroyWindow(window);
}
void MirrorWindow::showCursor(){
    if(!cursorHidden_)return;
    cursorHidden_=false;SetCursor(LoadCursorW(nullptr,IDC_ARROW));
}
LRESULT CALLBACK MirrorWindow::windowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_NCCREATE){
        const auto* create=reinterpret_cast<const CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    // hwnd_ is assigned once CreateWindowExW returns; creation messages take the default path.
    auto* self=reinterpret_cast<MirrorWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(!self||self->hwnd_!=hwnd)return DefWindowProcW(hwnd,message,wp,lp);
    return self->handle(message,wp,lp);
}
LRESULT MirrorWindow::handle(UINT message,WPARAM wp,LPARAM lp){
    switch(message){
    case WM_CLOSE:
        // Never destroyed here: the engine may still hold a swap chain on it.
        if(fullscreen_)setFullscreen(false);
        hide();
        if(onUserClosed)onUserClosed();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:{
        // Black until (and between) engine presents; flip-model frames replace it.
        PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd_,&ps);
        FillRect(dc,&ps.rcPaint,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        EndPaint(hwnd_,&ps);return 0;
    }
    case WM_LBUTTONDBLCLK:
        toggleFullscreen();return 0;
    case WM_KEYDOWN:
        if(wp==VK_F11){toggleFullscreen();return 0;}
        if(wp==VK_ESCAPE&&fullscreen_){setFullscreen(false);return 0;}
        break;
    case WM_SYSKEYDOWN:
        if(wp==VK_RETURN){toggleFullscreen();return 0;} // Alt+Enter
        break;
    case WM_MOUSEMOVE:{
        POINT p{short(LOWORD(lp)),short(HIWORD(lp))};
        if(p.x!=lastCursor_.x||p.y!=lastCursor_.y){
            lastCursor_=p;showCursor();
            if(fullscreen_)SetTimer(hwnd_,kCursorTimer,2000,nullptr);
        }
        break;
    }
    case WM_TIMER:
        if(wp==kCursorTimer){
            KillTimer(hwnd_,kCursorTimer);
            if(fullscreen_){cursorHidden_=true;POINT p{};GetCursorPos(&p);if(WindowFromPoint(p)==hwnd_)SetCursor(nullptr);}
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if(cursorHidden_&&LOWORD(lp)==HTCLIENT){SetCursor(nullptr);return TRUE;}
        break;
    case WM_DPICHANGED:
        if(!fullscreen_){
            const auto* r=reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        }
        return 0;
    case WM_DISPLAYCHANGE:
        // Keep a fullscreen mirror covering its monitor after a mode change.
        if(fullscreen_){
            const RECT m=monitorArea(MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST));
            SetWindowPos(hwnd_,HWND_TOP,m.left,m.top,m.right-m.left,m.bottom-m.top,SWP_NOACTIVATE);
        }
        break;
    default:break;
    }
    return DefWindowProcW(hwnd_,message,wp,lp);
}
}
