#pragma once
// Mirror output window (see include/veyra/engine/MirrorOutput.h): a plain
// top-level Win32 window created on the Qt GUI thread, whose event loop
// dispatches its messages. The engine presents into it on its own thread.
// The window is never destroyed while the engine may hold a swap chain on it:
// closing hides it; destroy() is only called once the engine released it.
#include <windows.h>
#include <functional>
#include <optional>
namespace veyra::ui {
class MirrorWindow {
public:
    // Windowed client rectangle (screen pixels) plus fullscreen, restored on
    // the next show when that rectangle is still on a connected monitor.
    struct Placement { RECT windowed{}; bool fullscreen=false; };
    MirrorWindow()=default;
    ~MirrorWindow();
    MirrorWindow(const MirrorWindow&)=delete;
    MirrorWindow& operator=(const MirrorWindow&)=delete;
    // Creates the window on first use. Without a saved placement it opens on
    // a monitor other than the one `owner` is on, when there is one.
    bool show(HWND owner,const std::optional<Placement>& saved);
    void hide();
    bool visible()const{return hwnd_&&IsWindowVisible(hwnd_);}
    HWND hwnd()const{return hwnd_;}
    void setFullscreen(bool on);
    void toggleFullscreen(){setFullscreen(!fullscreen_);}
    bool fullscreen()const{return fullscreen_;}
    Placement placement()const;
    void destroy();
    // The engine still held a swap chain at shutdown: keep the window alive
    // (DXGI requires the window to outlive the chain) and let process exit
    // reclaim it.
    void abandon(){hwnd_=nullptr;}
    // Called (on the GUI thread, from the window procedure) when the user
    // closes the window with its X button or Alt+F4; it is already hidden.
    std::function<void()> onUserClosed;
    std::function<void()> onFullscreenChanged;
private:
    static LRESULT CALLBACK windowProc(HWND,UINT,WPARAM,LPARAM);
    LRESULT handle(UINT,WPARAM,LPARAM);
    void showCursor();
    HWND hwnd_=nullptr;
    bool fullscreen_=false,cursorHidden_=false;
    RECT windowedRect_{};
    POINT lastCursor_{-1,-1};
};
}
