# SpectrumTag 1.4.28 — Windows standalone video aspect ratios

Date: 2026-11-09. This version adds a standalone spectrum-video export setting. Stable remains the user-designated 1.4.27; its frozen Plan B is not accessed or changed. Upstream PR #3 remains on 1.4.27.

## User-visible behaviour

Export → Export spectrum video → Aspect ratio / 导出 → 导出频谱视频 → 画面比例.

The choices are 4:3 Landscape, 3:4 Portrait, 16:9 Landscape and 9:16 Portrait. Changing aspect keeps the selected quality tier and updates the resolution menu to actual pixel dimensions:

| Short edge | 4:3 | 3:4 | 16:9 | 9:16 |
| --- | --- | --- | --- | --- |
| 720 | 960×720 | 720×960 | 1280×720 | 720×1280 |
| 1080 | 1440×1080 | 1080×1440 | 1920×1080 | 1080×1920 |
| 1440 | 1920×1440 | 1440×1920 | 2560×1440 | 1440×2560 |
| 2160 | 2880×2160 | 2160×2880 | 3840×2160 | 2160×3840 |

The setting defaults to 16:9 and is remembered after choosing the destination. Previous settings without an aspect attribute retain their existing format, quality and frame rate with a 16:9 canvas. Invalid stored aspect values fall back to 16:9.

All formats (MP4/MOV/MKV/WebM) use the selected physical frame dimensions, with no rotation-metadata workaround. The existing camera fits each Tag in the new canvas while preserving its proportions. The actual processed audio, full-frame spectrum, right-edge reveal, progress popup, cancellation and destination selection remain in place. Original-picture video export retains its source framing; this setting applies to generated spectrum videos.

## Implementation and validation

`Standalone/SpectrumVideo.h` owns aspect and dimension calculation. `Standalone/SpectrumVideo.cpp` persists the setting. `Standalone/ModernStandalone.cpp` provides bilingual controls and dimension updates. The existing renderer and encoder already consume the selected canvas dimensions.

Windows x64 Release build completed successfully. The existing resource-compiler warning about `JUCE_STANDALONE_APPLICATION` being redefined remains; no compilation error occurred. Checks used isolated preferences, not the user's saved session. This turn does not rebuild Mac products, plugins or installers.

- `--video-test`: passed. All 16 aspect/quality combinations have the expected canvas dimensions, and both tall and wide Tags retain their proportions and fit. The four aspects match independent scalar spectrum sampling. Actual encoded probes: MP4 960×720/30 fps, MOV 1080×1440/60 fps, MKV 2560×1440/30 fps, WebM 2160×3840/60 fps. A separate 720×1280 MP4 was decoded and compared with the expected Tag pixels. Baseline landscape frames/audio sync, settings migration/persistence and cancellation protection also passed.
- `--fileless-ui-test`: passed. Both languages select all four ratios, retain the quality tier, update dimensions and restore the remembered choice. All controls fit inside the enlarged dialog. Chinese and English snapshots were visually inspected; labels and controls are fully visible. Existing blank Print, Delete, undo/redo and progress/cancel checks passed.
- `git diff --check` (with `core.whitespace=cr-at-eol`): passed.
- Delivered EXE is byte-identical to the tested build; both Windows FileVersion and ProductVersion are `1.4.28`.

Initial delivery was a standalone EXE and short usage TXT. On the user's subsequent explicit request, the installed standalone was replaced on 2026-11-09 at `D:\Program Files\iisaacbeats.cn\SpectrumTag\SpectrumTag.exe`, after confirming SpectrumTag was closed. Its 1.4.28 version and SHA-256 match the tested build; see `INSTALLATION.json` in the log directory. Mac compilation and manual user acceptance of this new setting remain unverified.

EXE size: `29,911,552` bytes. SHA-256: `D1A9D74C3A1D7A86F820C8AE45543A79938196BC56E8114B5A3109216012FA1F`.

Evidence: `build.log`, `video/video-checks.log`, `video/portrait-decoded.png`, `ui/ui-checks.log`, `ui/export-settings-zh.png`, `ui/export-settings-en.png` in the new log directory below.

Working source: `D:\Codex\Workspaces\SpectrumTag-1.4.20-CrossPlatform`.

Build: `D:\Codex\Temp\SpectrumTag-1.4.20\windows` (reused incrementally).

New logs/media: `D:\Codex\Temp\SpectrumTag-1.4.28`.

Delivery: `D:\Codex\Outputs\SpectrumTag-1.4.28-Windows`.

## Subsequent cross-platform delivery

After the user designated 1.4.28 Stable and requested installers, the standalone binary above was reused in the validated Windows Setup; matched Mac Universal PKG and bilingual guides were also delivered. See [INSTALLERS_1.4.28.md](INSTALLERS_1.4.28.md). The earlier Windows-only scope above is retained as history.
