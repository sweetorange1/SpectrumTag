# SpectrumTag Windows standalone 1.4.24 — Stable / Plan B

User authorisation on 2026-11-07: “执行Plan B 设为Stable”.

## Current status

- Windows standalone 1.4.24 is user-designated Stable. No product input, numerical version or existing binary was changed for this designation.
- Previously delivered VST3/ARA 1.4.22 remain unchanged. This designation does not label an unbuilt 1.4.24 plugin as a delivered Stable plugin.
- The immediately preceding standalone delivery was 1.4.23; its original output remains at `D:\Codex\Outputs\SpectrumTag-1.4.23-Windows`. Its identity is retained, not retrospectively renamed Stable. The earlier user-designated standalone Stable was 1.4.19.
- Mac/AU building, GitHub publication and later Plans remain paused/out of scope.

## Inherited Plan A — not rerun

The completed delivery record is `D:\Codex\Temp\SpectrumTag-1.4.24\DELIVERY.md`. It records the Release build, actual-control deletion/undo checks, spectrum-video formats and pixel comparison, soundtrack alignment, cancellation, standalone format/session regression and extended-prefix/tail tests. Reuse these results; Plan B is not a reason to rerun them.

Delivered EXE: `D:\Codex\Outputs\SpectrumTag-1.4.24-Windows\SpectrumTag.exe`.

Installed EXE: `D:\Program Files\iisaacbeats.cn\SpectrumTag\SpectrumTag.exe`.

Recorded ProductVersion/FileVersion: `1.4.24`; size `29855232` bytes; SHA-256 `2799DC08C302AB7A5F242C7D1975AA80BD82C591BC7FD716DC7EAC5624108CC1`. These identifiers are inherited from the prior delivery, not a new binary audit.

## New Plan B

Source: `D:\Codex\Workspaces\SpectrumTag-1.4.20-CrossPlatform`.

Destination: `D:\备份文件\Vibe Coding\SpectrumTag\源代码\SpectrumTag-1.4.24-Stable`.

The snapshot contains the full current source/assets/docs/scripts/workflows and existing Git history, including uncommitted development through 1.4.24. Build caches, test media, installed binaries and output packages are not part of this source snapshot. The required Windows codec input and its notices are included under `RequiredBuildInputs/WindowsCodec`; reusable JUCE/ARA dependency caches are excluded.

Completion evidence and quick copy verification: `D:\Codex\Temp\SpectrumTag-1.4.24\PLAN_B_COMPLETION.md`.

Upon the completion report this backup is frozen. Do not revisit, enumerate, hash, refresh, overwrite or delete it in later development/Plans. Later work uses the active source and this completion record. Earlier frozen backups were not read or changed for this request.

## Reproduction inputs

The inherited Windows build used MSVC 2022, Visual Studio 17 2022 x64 / Release, JUCE 8.0.15, network functionality OFF, tests ON, and ARA enabled in the shared build tree. Current CMake pins JUCE 8.0.15; ARA SDK 2.3 configuration and platform scripts remain in the source. Existing local dependency paths are recorded in `docs/CROSS_PLATFORM_1.4.20.md` and the build scripts; dependencies may be restored or paths explicitly supplied without modifying source.

For the standalone use `build-standalone-windows.ps1` with a separate D-drive `-BuildDir`, a JUCE 8.0.15 `-JuceDir`, and `-RuntimeDir <snapshot>\RequiredBuildInputs\WindowsCodec`. The codec directory must contain `ffmpeg.exe`, `COPYING.GPLv3`, and `FFmpeg-NOTICE.txt`; preserve all three. The notice records the original codec provenance. Do not substitute a different codec and assume it reproduces this build. All generated intermediate files belong outside the frozen snapshot.

Existing historical installer/1.4.20 cross-platform documents retain their historical scope. This Plan B does not generate installers or claim macOS/AU verification. Repository AGPL-3.0 and third-party notices remain unchanged.
