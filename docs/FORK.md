# Fork guide: hugokindel/Veyra-NRVideo

Fork-only file (upstream never edits it, so it never conflicts). It is the source of truth for what this fork
adds and how to take a new upstream release without losing it. If an upstream merge drops the fork section of
`AGENTS.md`, `docs/CURRENT_STATUS.md` or `docs/WORKLOG.md`, restore it from here.

## What the fork adds

| Feature | Doc | Guarding tests |
|---|---|---|
| Mirror window (second window, same final frame, HDR on both screens) | [MIRROR_WINDOW_2026-11-20.md](MIRROR_WINDOW_2026-11-20.md) | `veyra_mirror_output_gpu_tests` (`MIRROR_RESULT passed=1 debugErrors=0`) |
| Elgato 4K Pro: standard limited chroma + chroma centre correction | [ELGATO_4K_PRO_CHROMA_2026-11-24.md](ELGATO_4K_PRO_CHROMA_2026-11-24.md) | `veyra_capture_color_tests` (4K Pro cases), `veyra_hdr_color_tests` (`HDR_P010_CHROMA_CENTRE`) |
| Full-feature build (every optional SDK of the official NVIDIA exe) | MIRROR doc, "Full-feature build" | configure log, dumpbin comparison |

Branch `feature/mirror-output`. `origin` = this fork (push allowed), `upstream` = `Likely7/Veyra-NRVideo`
(never push, comment or release there). Patch tags `v2.2.1-mirror1..4`; never move a pushed tag.

## Files the fork changes, and what to check in each after a merge

Textual conflicts are the easy part; the 2.2.1 merge merged cleanly and still broke the mirror. Re-check:

- `src/engine/VideoPresenter.cpp`, `include/veyra/engine/VideoPresenter.h`, `include/veyra/engine/MirrorOutput.h`:
  the mirror is a second `PresentSink` recorded after the main present. If upstream changes the present path
  (new swap chain types, HDR output, scaling), make the mirror follow it. Since 2.2.2 the blit has two pipeline
  states (`pass_.pso` and `adaptiveBlit_` for the adaptive ultrawide view, ratio in `dims[14]`): the mirror
  computes its own `adaptiveWidthRatio` on the mirror client size and picks its own PSO.
- `src/engine/EngineController.cpp` (+ `EngineControllerImage.cpp`, `EngineController.h`): **every place a
  `VideoPresenter` is created, replaced or swapped must hand the mirror over** (`old->releaseMirror()`,
  `new->setMirror(&mirror_)`). 2.2.1 added the capture "promote" swap (raw -> enhanced presenter) and the mirror
  went black until fixed. After a merge: `git grep -n "presenter" src/engine/EngineController*.cpp` and check
  each new swap.
- `src/ui/MirrorWindow.*`, `src/ui/bridge/Mirror.cpp`, `src/ui/QmlPlayerBridge*`, `include/veyra/ui/QmlPlayerBridge.h`,
  `apps/veyra-qml/main.cpp` (`--mirror-window` test switch): Win32 window + QML bridge.
- `qml/Veyra/Main.qml` (Ctrl+M), `qml/Veyra/ProPage.qml` (header switch), `i18n/catalog.json` (6 strings x 9
  languages; run `python scripts/i18n/extract.py --check`).
- `CMakeLists.txt`: mirror sources and the `veyra_mirror_output_gpu_tests` target.
- `src/source/ElgatoHdrControl.cpp`, `include/veyra/source/ElgatoHdrControl.h`: `isElgato4kPro()` must stay
  excluded from the non-MK.2 full-range chroma rule, and the 4K Pro P010 branch must keep setting
  `chromaCentreCodes`. If upstream reworks the Elgato rules, keep both behaviours.
- `include/veyra/pipeline/FramePacket.h` (`chromaCentreCodes`, `packYuvChromaWord`), `src/pipeline/EnhanceGraph.cpp`
  (YUV constants lane 7 = `packYuvChromaWord(...)`), `shaders/YuvToLinearRgb.hlsl` (`yuvDimensions.w`: bits 0..7
  chroma location, 8..15 / 16..23 Cb/Cr steps). If upstream adds chroma locations above 255, uses those bits,
  or rearranges the YUV root constants, re-pack them; the pass is near the 64 root-constant limit.
  2.2.2 builds the same shader a second time as `P210ToLinearRgb.dxil` (`VEYRA_P210`): its `ReconstructChroma`
  branch must also use `yuvDimensions.w & 0xFF`, and that DXIL differs from the official one (goes in the patch).
- Tests: `tests/integration/MirrorOutputGpuTests.cpp`, `tests/integration/HdrColorTests.cpp`, `tests/unit/ElgatoHdrCases.h`.
- Docs: fork sections at the top of `AGENTS.md`, `docs/CURRENT_STATUS.md`, `docs/WORKLOG.md` (upstream often
  rewrites or archives these; keep upstream's text and re-add the fork sections).

## Upstream sync procedure

1. `git fetch upstream --tags`; fast-forward the fork's `main` to upstream if wanted (`git push origin <tag>` for
   the release tag). Merge the **release tag** (e.g. `vX.Y.Z`), not upstream main, into `feature/mirror-output`.
2. Resolve conflicts, then walk the file list above, especially presenter creation/swaps.
3. Dependencies: diff upstream's pins since the last merge and update `D:/Projects/Veyra-work/deps` to match:
   `scripts/ci/fetch-sdks.ps1`, `scripts/{remoteplay,moonlight,xbox}/dependency-lock.json` (+ Chiaki patches,
   vcpkg commit/overlay), `docs/DEVELOPMENT.md` (libdovi, Magewell SDK), `docs/RIFE_DIRECTML.md` (ORT/DirectML).
   Fork copies of the build scripts: [scripts/fork/build-full.ps1](../scripts/fork/build-full.ps1) and
   [scripts/fork/fetch-sdks.ps1](../scripts/fork/fetch-sdks.ps1) (paths are this machine's
   `D:/Projects/Veyra-work`; keep the copies there and here in sync). Since 2.2.2 the fork `fetch-sdks.ps1` also
   stages the DLSS SDK and nv-codec-headers from GitHub archives at upstream's pinned commits.
4. Build: `powershell -File scripts/fork/build-full.ps1 -Targets veyra_qml_ui,veyra_mirror_output_gpu_tests,veyra_vfg_gpu_tests,veyra_capture_color_tests,veyra_hdr_color_tests -Jobs 2 *> D:/Projects/Veyra-work/logs/build-full-N.log`.
   Configure log must have no "unavailable in this build" / "not configured" line for XeSS, FSR, AMF, RIFE,
   libdovi, libass, DeckLink, Magewell, Remote Play, Moonlight, Xbox.
5. Tests (PATH += the official version folder, e.g. `C:/Shared/Veyra/versions/X.Y.Z`; TEMP/TMP under
   `D:/Projects/Veyra-work/tmp`): mirror GPU passed=1 debugErrors=0; VFG GPU 0 failures (needs the official
   `runtime/nvidia-vfg`); capture colour failures=0 including the 4K Pro cases; HDR colour all pass including
   `HDR_P010_CHROMA_CENTRE`.
6. Package audit against the new official NVIDIA portable ZIP:
   - `dumpbin /dependents` of the new exe must list only DLLs the official exe/package has (or Windows).
   - Compare **every** file the build produces that the package also contains (exe, `qml/**`, `shaders/*.dxil`)
     by SHA-256; the patch is exactly the set that differs. Do not assume it is only the exe + two QML files.
7. Smoke: copy of the official portable in `D:/Projects/Veyra-work/tests/...`, launch through `Veyra.exe` with
   a fresh copy of the user profile, `--mirror-window 4000,7000,11000 --exit-after 13000 <video>`; expect mirror
   attach/fullscreen/detach and exit code 0. Never launch the user's install to open the capture card.
8. Commit notes (WORKLOG fork entry), push the branch, new tag `vX.Y.Z-mirrorN`, patch ZIP + SHA256SUMS +
   RELEASE_NOTES under `D:/Projects/Veyra-work/releases/<tag>`. Install into `C:/Shared/Veyra` only when the
   user asks and Veyra is closed, backing up replaced files to `versions/<ver>/_*-backup`.

## Decisions that must survive a merge

- No update public key in fork builds (`VEYRA_UPDATE_PUBLIC_KEY` empty): the in-app updater would replace the
  patched exe with the official one. Pending the user's final decision.
- Patch over the official package, reusing its runtime DLLs and licenses; no extra runtime DLLs.
- 4K Pro correction values (Cb +0.94, Cr +1.20 codes) are measured; change them only after a new pattern
  measurement (`D:/Projects/Veyra-work/tests/hdr-pattern/measure_sdr.ps1`). Tint/Saturation stay at 0 for it.

## Open items

- Mirror handover during capture raw -> enhanced promote not yet confirmed on the capture card
  (log: `[mirror] detach ... next=1` followed by a new `attach`).
- Remote Play, Moonlight, Xbox, DeckLink, Magewell and AMD paths compiled but not exercised on hardware.
- Remaining ~-1 Cb code on 4K Pro colour patches (content-dependent); next check with the HDR10 pattern.
- 2.2.2 Y210/P210 capture: `CaptureCardSource` only runs `ElgatoHdrControl::configure` for P010 and 8-bit
  formats, so a 4K Pro in Y210/P210 (if its driver offers them) gets neither the standard-limited chroma rule nor
  the centre correction. Unmeasured; decide after a pattern test in those formats.
- Fork build compiles against FFmpeg 9.0.1 import libraries; official 2.2.2 ships 9.0.2 DLLs (same sonames,
  same major ABI). Rebuild `deps/ffmpeg-root` from 9.0.2 if a mismatch ever shows.
- GitHub Releases for the patch tags not created (browser step).
