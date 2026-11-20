#pragma once
// Mirror output window: a second, user-draggable top-level window that shows
// the same final frame as the player window (e.g. a monitor plus a TV in
// Windows Extend mode, where HDR stays available on both screens).
//
// The UI thread owns the HWND and never destroys it while the engine may hold
// a swap chain on it (DXGI requires the window to outlive the chain); closing
// only hides it and clears `window`. The engine thread reads `window` once per
// presented frame and publishes its state back through the other atomics.
// The main presentation never waits for the mirror: its swap chain is polled
// with a zero-timeout latency wait and skipped while busy.
#include <windows.h>
#include <atomic>
#include <cstdint>
namespace veyra::engine {
enum class MirrorState : int {
    Off = 0,           // no mirror window requested
    Waiting = 1,       // window requested; no session is presenting
    Active = 2,        // presenting into the mirror window
    Unsupported = 3,   // provider-owned presentation (XeSS) cannot be mirrored
    Failed = 4,        // swap chain creation failed for this window (retried later)
};
struct MirrorOutputLink {
    std::atomic<HWND> window{nullptr};
    std::atomic<int> state{int(MirrorState::Off)};
    // Frames presented into the mirror and frames skipped because its swap
    // chain was still busy (a slower mirror display) or the window was hidden.
    std::atomic<uint64_t> presented{0}, skipped{0};
    std::atomic<uint32_t> width{0}, height{0};
};
struct MirrorOutputStatus {
    bool requested = false;
    MirrorState state = MirrorState::Off;
    uint64_t presented = 0, skipped = 0;
    uint32_t width = 0, height = 0;
};
inline MirrorOutputStatus mirrorSnapshot(const MirrorOutputLink& link) {
    MirrorOutputStatus s;
    s.requested = link.window.load() != nullptr;
    s.state = MirrorState(link.state.load());
    s.presented = link.presented.load();
    s.skipped = link.skipped.load();
    s.width = link.width.load();
    s.height = link.height.load();
    return s;
}
}
