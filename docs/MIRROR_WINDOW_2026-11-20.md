# Mirror output window (2026-11-20)

镜像窗口：在第二个可拖动窗口显示同一最终画面（例如显示器 + 电视，Windows 保持“扩展”模式，两块屏幕都可保留 HDR）。

## What it does

- **Pro page header → 镜像窗口 (Mirror window)** switch, or **Ctrl+M** from any page, opens a second
  top-level window titled *Veyra Mirror*. Without a saved position it opens on a monitor other than
  the one Veyra is on.
- Drag it to the other screen; **double-click**, **F11** or **Alt+Enter** toggles borderless
  fullscreen on the monitor it is on; **Esc** leaves fullscreen. The cursor hides after 2 s in
  fullscreen. Closing it with its X is the same as turning the switch off. Position and fullscreen
  are remembered (`mirrorWindowPlacement` in the QML preferences) and restored only when that
  rectangle is still on a connected monitor.
- The mirror shows the same final frame as the player window: NR/SR/colour/HDR curve, view mode,
  rotation, zoom/pan, A/B comparison, and application-generated frame-generation frames (DLSS-G,
  FSR, VFG, AMD FRC), at up to the mirror monitor's refresh rate.

## How it works

- `include/veyra/engine/MirrorOutput.h`: `MirrorOutputLink` (atomics) carries the UI's requested
  HWND to the engine and the state/counters back. `EngineController::setMirrorWindow()` /
  `mirrorStatus()` / `mirrorReleased()`.
- `VideoPresenter` owns a second `gfx::PresentSink` bound to the borrowed mirror HWND on the
  persistent native presentation queue. Each frame it re-records the PresentBlit draw (and the
  comparison draw) with the mirror's client geometry into the same command list as the player
  blit, so the existing consumer fence covers the output-pool read. The mirror presents with
  `Present(0,0)` right after the player.
- **Never blocks the player**: the mirror chain has frame latency 1 and is polled with a
  zero-timeout wait. If its display has not taken the previous frame yet (e.g. 120 Hz TV next to a
  240 Hz monitor) or the window is hidden/minimised, the mirror skips that frame. No pixel
  readback, no per-frame CPU fence wait, no queue.
- Same swap-chain format and colour space as the player (SDR G2.2 RGBA8, scRGB FP16, HDR10
  PQ/BT.2020 R10G10B10A2).
- Mirror resizes wait until the size has been stable for 120 ms (DXGI stretches meanwhile); the
  resize drains the presentation queue once, like a player resize. While paused, a newly attached
  or resized mirror gets the held frame once.
- Window lifetime: `src/ui/MirrorWindow.*` is a plain Win32 window on the Qt GUI thread. It is
  never destroyed while the engine may hold a swap chain on it (DXGI requires the window to outlive
  the chain): closing only hides it; at exit the bridge clears the request and waits up to 1 s for
  the engine to release it before destroying the window.

## Limits

- **XeSS frame generation** owns `Present` for its generated frames on its own proxy chain, so the
  mirror reports *unsupported* while XeSS-FG is active instead of interfering with its pacing.
- Subtitles, the format badge and protection outlines are separate layered GDI windows over the
  player and are **not** mirrored.
- HDR display tuning (curve peak, HDR10 metadata hint) follows the player window's monitor; the
  mirror chain gets no separate `SetHDRMetaData`.
- G-SYNC/VRR applies to the player window; the mirror runs at its monitor's fixed refresh.
- Keyboard shortcuts other than the fullscreen keys only work in the player window.
- The mirror is fed by the player's presents: when the player area has no size (window closing)
  the mirror is not updated; it still releases its swap chain when closed.

Test switch: `--mirror-window <open-ms>[,fullscreen-ms[,close-ms]]` drives the same bridge entry
as the switch, for unattended runtime checks.

## Verification

- `veyra_mirror_output_gpu_tests <private-dir>`: attach, SDR 8-bit and HDR10 10-bit pixel parity
  against the graph output, contain-fit pillarbox, hidden-window skips, settled resize, paused
  re-present, detach/reattach, close → waiting, D3D12 debug layer clean.
- Actual results of this round are in [WORKLOG](WORKLOG.md).
