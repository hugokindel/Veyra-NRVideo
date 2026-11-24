# Elgato 4K Pro HDR chroma (2026-11-24)

Elgato 4K Pro 采集 P010 HDR：改为标准限制范围色度，并加入每设备色度中心校正（Cb +0.94、Cr +1.20 码值）。分支 `feature/mirror-output`，提交 `33a47f46`，标签 `v2.2.1-mirror4`。

## Problem

- The non-MK.2 Elgato rule (Nitlink) decoded every Elgato card except the 4K60 Pro MK.2 as limited luma with
  **full-range chroma**. On the 4K Pro this made colour about 12% too weak with Input range = Auto.
- Neutral grey decoded about 1 code off centre (green 2-3% high on clean grey steps).

## Measurement (user, before the fix)

PS5 Media Gallery, HDR Always On, `D:/Projects/Veyra-work/tests/hdr-pattern/` (`Veyra SDR Test Pattern.mp4`,
`measure_sdr.ps1`; the script predicts each patch from its BT.709 codes through the PS5's measured SDR->HDR
curve and compares Veyra's decoded HDR screenshot). Device name in the log: `Elgato 4K Pro`.

- Input range = Limited (`fullRangeChroma=false`): chroma gain Cb 1.004, Cr 1.012; luma error 0.7 codes mean.
  So the card sends standard 64..960 chroma.
- Neutral error on flat (uniform) patches, 10-bit codes: Cb -0.94, Cr -1.20/-1.21, identical on three PS5 HDR
  pages and the pattern, from 0.4 nits to the 9908-nit clip, so a constant code offset, not a gain.
  Screenshots `veyra-20261124-213244-616.jxr`, `-213522-694`, `-213757-923`, `-220205-168` (user's Pictures\Veyra,
  not in Git). The pattern's colour-patch fit gave Cb -1.95 (pulled by dithered steps; see Remaining).

## Change

- `isElgato4kPro()` (exact names `Elgato 4K Pro`, `Game Capture 4K Pro`, `Elgato Game Capture 4K Pro`; not the
  4K60 Pro family) is excluded from the non-MK.2 full-range chroma rule (`src/source/ElgatoHdrControl.cpp`).
- `ColorDescription::chromaCentreCodes[2]` (`include/veyra/pipeline/FramePacket.h`): Cb/Cr correction in 10-bit
  codes added before the YUV decode; zero for every other source. Set only for the 4K Pro on P010
  (`kElgato4kProChromaCentreCodes = {0.94, 1.2}`), independent of the range override and of the vendor property
  set; 8-bit (card tonemapped) modes were not measured and get none.
- Shader `YuvToLinearRgb.hlsl`: `yuvDimensions.w` = chroma location in bits 0..7, signed Cb/Cr steps of 1/16 code
  in bits 8..15 / 16..23 (`packYuvChromaWord`), so no extra root constant (the pass is at 60 of 64). Effective
  values 0.9375 / 1.1875 codes. Rescaled for 8- and 16-bit inputs. Log:
  `[capture-elgato] P010 ... fullRangeChroma=false chromaCentre=+0.94/+1.20`.
- Patch packages must now ship `shaders/YuvToLinearRgb.dxil` with the exe (it is the only DXIL that differs from
  official 2.2.1; the other 88 are byte-identical).

## Verification

- `veyra_capture_color_tests`: failures=0, new cases for name matching, standard chroma in Auto, correction
  assigned (not accumulated), no correction for 8-bit or other Elgato cards, word packing/clamping.
- `veyra_hdr_color_tests`: new `HDR_P010_CHROMA_CENTRE` (grey at Cb=Cr=511 is off-neutral uncorrected and
  equals the centred grey with +1 code, relative error 0); all existing HDR cases pass.
- Mirror GPU test passed=1 debugErrors=0; VFG GPU test 2521 checks 0 failures; smoke on official 2.2.1 portable.
- User re-measurement with the installed build (Input range = Auto, colour panel off):

| Check | Before | After |
|---|---|---|
| Clean grey steps R/G/B | green 2-3% high | equal (e.g. 73.128 / 73.128 / 73.128 nits) |
| Chroma gain in Auto | ~12% too weak | 1.003 / 1.013 |
| Neutral, Cr | -1.1 codes | +0.1 |
| Neutral, Cb (colour-patch fit) | -2.0 codes | -1.0 |
| Patch luma error (method check) | 0.7 codes | 0.7 codes |

## Remaining / not verified

- About -1 Cb code remains on colour patches and on dithered grey steps, while clean greys read 0. A constant
  card offset would affect all equally, so this one depends on content: rounding in the PS5's SDR->HDR conversion
  or in the card's 4:2:0 conversion; this test cannot tell which. Next check: play
  `Veyra HDR10 Test Pattern (2026).mp4` (native HDR, no PS5 conversion) and run `measure_pattern.ps1`.
  Visually negligible (1/896 of chroma span on one channel).
- Calibrated on one card, driver and PS5; other 4K Pro units may differ slightly. Consider a user override before
  proposing this upstream.
- With the correction built in, the Tint/Saturation sliders should stay at 0 for this card.
