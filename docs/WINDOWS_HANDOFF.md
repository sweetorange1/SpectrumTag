# Windows implementation handoff

> Current status (2026-11-08): all delivered targets now use **1.4.27**, designated Stable by the user. Existing Windows Setup, Universal macOS PKG and platform validation are documented in [INSTALLERS_1.4.27.md](INSTALLERS_1.4.27.md). The user has authorized a new upstream PR; start with [UPSTREAM_HANDOFF_1.4.27.md](UPSTREAM_HANDOFF_1.4.27.md). The version split and Windows-only delivery statements below are historical.

> Historical baseline: this document records the 1.4.18/1.4.19 Stable handoff. The subsequent 1.4.20 cross-platform work is tracked in PROJECT_OVERVIEW.md and the installation guides. Do not infer current target versions from the historical values below.

## Scope and version identity

Base: upstream `ac7d32470d1e7d5ae447e00fb01ebb8ee12631e6` (1.4.3). This source handoff contains the completed Windows development through plugin/ARA 1.4.18 and standalone 1.4.19, designated Stable by the contributor on 2026-11-06. Designating Stable did not rebuild, renumber or reinstall an existing product. The root CMake project version and plugin targets are intentionally 1.4.18; `SpectrumTagStandalone` and its displayed override are 1.4.19.

The request is an upstream pull request for review and merge. It is not an automatic merge, new binary release, or claim that the modified Mac targets have passed. Original attribution, license, resource assets and unchanged upstream files are retained. Local delivery logs, machine paths, source media, generated artifacts and dependency caches are excluded from the public submission.

## Architecture map

| Area | Source | Responsibility |
| --- | --- | --- |
| Processor | `PluginProcessor.*` | Host transport, live STFT/WOLA, parameters, recorded/ARA/file-player modes |
| Editor | `PluginEditor.*` | Image/Print gestures, controls, selection, host layout and view navigation |
| Recording/editing | `shared/Recording.*` | Absolute sample timeline, sparse coverage, original audio, replaceable Print edits, undo snapshots, worker rendering |
| View workers | `shared/ViewRenderer.h`, `PreviewWorker.h`, `PrintPreview.h` | Coalesced immutable spectrum snapshots and lightweight parameter/geometry previews |
| Conversion/presentation | `shared/ImageMask.h`, `FrequencyScale.h`, `TimeScale.h`, `Presentation.h`, `FineSlider.h`, `PlatformFonts.h` | Shared B/W conversion, frequency/time mappings, palette/help, fine gestures and platform fonts |
| ARA | `ARA/ARAIntegration.*` | Document-owned sources/modifications, region mapping, track selection, archive and host seek |
| Standalone | `Standalone/ModernStandalone.cpp`, `AudioFileSession.h`, `EmbeddedCodec.*` | Local transport, load/drop, source-format export, embedded Windows codec |
| Verification | `tests/` and standalone `--self-test` | DSP/model/gesture/media checks |

The legacy `StandaloneMain/StandaloneWindow/SharedUI/OfflineRenderer` sources remain in the tree but are not the modern standalone target's entry point. Upstream installer scripts also remain unchanged; adapt them before publishing new installers.

## Contracts to preserve

1. Original input is immutable. Each Print is an edit with a stable identity and deterministic noise seed; replacing it must not apply it repeatedly to already processed audio. Delete removes its noise too. Original/page/spectrum caches accelerate deletion and unaffected regions; overlapping surviving edits may still need recomputation.
2. Record uses DAW absolute positions, valid-sample coverage and source samples. Earlier/later passes preserve gaps; same-position capture replaces samples. Freeze/offline capture must not drop queued tail samples. Clear returns to live mode, invalidates stale FFT/raster work, and is undoable.
3. Host-aligned VST3 playback uses edited recordings only where recorded. Unrecorded input passes through. Local audition cannot seek the DAW and must output silence in an unrecorded gap rather than play unrelated host time. Stop, host seek and cycle reset the local offset.
4. ARA Load expands only through actual host region-sequence membership. Selecting another event in the same loaded track reuses the snapshot. Region moves, source replacement/rate changes and track destruction invalidate the right data. Never group by filename or source identity alone.
5. ARA source reads happen on the permitted model thread, not the audio callback/render worker. Print lifetime belongs to the document/modification, not the editor. Playback uses region placement, source offset/rate and coverage; mixing preserves overlap. Offline rendering works without a visible UI or `Playing=true`.
6. ARA archive holds masks, parameters and persistent-ID mappings; filtered restore and host undo restoration must not echo edits back. Windows recorded cache paths are not the ARA persistence format. Shared AudioModification edits affect its referencing events; independent edits require separate modifications supplied by the host.
7. Mouse wheel changes the view without re-rendering a selected Print. Speed slider/Print parameter edits can change audio; coalesce them after release. Original-image aspect is always preserved. Print selection recalls stored parameters/normal aspect in one UI operation without seeking.
8. Preview and progress are simulations of the pending result, not newly rendered samples. Audio remains on the prior revision during gestures. Accept only matching snapshot revisions and replace previews with the final spectrum; never leave deleted or superseded previews behind.
9. Noise Fill applies only inside the Print's time/frequency bounds with edge fades, and never manufactures audio in unrecorded timeline gaps. It is unavailable in unrecorded realtime mode.
10. Do not reinterpret saved host parameter indices or existing Print bounds casually. Removed FFT/Super Vision choices retain compatibility slots. SPT1/SPT2/SPT3 recording reads and frequency/time migrations have regression coverage.

## Build and checks

The three PowerShell build entry points locate Visual Studio through `vswhere`, accept independent build/JUCE/SDK/runtime paths, and keep dependencies outside the source tree. `CMakeLists.txt` pins JUCE 8.0.15 and ARA SDK commit `0005c532bb03eb210e7bb490329fc6d7ba1792c7` with `ARA_API` and `ARA_Library`. No third-party SDK source is vendored here.

Options:

- `SPECTRUMTAG_BUILD_TESTS=ON`: builds `SpectrumTagVerify`; with ARA enabled, also `SpectrumTagARAVerify`.
- `SPECTRUMTAG_BUILD_ARA=ON` and optional `SPECTRUMTAG_ARA_SDK_DIR`: separate ARA target.
- `SPECTRUMTAG_ENABLE_NETWORK=OFF`: default; existing upstream telemetry/update sources are retained but requests are disabled in these deliveries.
- `SPECTRUMTAG_STANDALONE_RUNTIME_DIR`: directory containing the codec executable and its two notices for the Windows standalone resource.

Example checks after building (substitute actual artifact paths):

```powershell
$env:SPECTRUMTAG_PREFS_DIR = 'D:\TestOutput\SpectrumTag\preferences'
& 'D:\Build\SpectrumTag\SpectrumTagVerify_artefacts\Release\SpectrumTagVerify.exe' --checks 'D:\TestOutput\SpectrumTag\realtime'
& 'D:\Build\SpectrumTagARA\SpectrumTagARAVerify_artefacts\Release\SpectrumTagARAVerify.exe' 'D:\TestOutput\SpectrumTag\ara\ui.png'
& 'D:\Build\SpectrumTagStandalone\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe' --self-test 'D:\TestOutput\SpectrumTag\standalone'
& 'D:\Build\SpectrumTagStandalone\SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe' --codec-check 'D:\TestOutput\SpectrumTag\codec.json'
```

`SPECTRUMTAG_RUNTIME_CACHE` isolates codec extraction tests. `SPECTRUMTAG_FFMPEG` is an explicit codec executable override for development; verify the delivered portable EXE with that override unset. Test outputs are generated fixtures, not user sessions. Build scripts enable tests, but do not automatically execute all checks.

### Validation inherited from the delivered builds

These are prior completed Windows results, not tests rerun solely for this PR:

| Product | Completed checks |
| --- | --- |
| Realtime VST3 1.4.18 | Full processor/shared-editing regression; actual VST3 pluginval 1.0.4 strictness 5, seed 20261106, GUI tests skipped |
| ARA VST3 1.4.18 | ARA source/model/audio/archive/UI harness, including 44 sources / 22 renderers, offsets, resampling, gaps, read failure, filtered restore, cancellation/cache and resize; same pluginval settings |
| Standalone 1.4.19 | Full audio/video and shared-editing regression; isolated single EXE cold extraction, warm reuse, corrupt-cache repair and concurrent startup |

Media regression covered nine audio formats (WAV, FLAC, MP3, OGG, M4A/AAC, AIFF, AAC, WMA/wmav2, Opus), six video containers, unchanged video packet/frame/timestamp checks, a preserved additional audio track, delayed first-track alignment and rejected no-audio input retaining the previous session. Tests also exercise final rendered attenuation, preview/undo and proportional Print resize. ALAC and PCM float support are implemented but not separately covered by those media cases.

Existing Windows artifact SHA-256 identifiers for this handoff:

- Realtime VST3 module: `D6884FDC749D5B63B5366630D92FD6A67B696423391DC5E5ABF06F5805D01DE3`
- ARA VST3 module: `8DB11E346D847DB78F5B6D9776BDFC68792D39E4CBA585179CAAAD6D22231C90`
- Standalone EXE: `0AE1B58D340AB4454E0C5B782658399D51BC7322218700550BFE35792A977689`

The standalone includes a small shared-editor version-label override added in 1.4.19; the installed plugin artifacts above remain the previously validated 1.4.18 builds. This source submission did not rebuild those artifacts.

## Runtime and known limits

Windows standalone 1.4.19 embeds a ZIP with `ffmpeg.exe`, `COPYING.GPLv3`, `FFmpeg-NOTICE.txt`; those external build inputs are not committed. The tested helper is the existing `git-2020-06-03-b6d7c4c` build (SHA-256 `ff0fcadd5a69d359041a21f024aa06dc5d3aeca40547b4b07262179f71c0de0b`). A maintainer-provided replacement must be retested for its codec availability and output behaviour. Retain its corresponding license/source notices when packaging; this PR does not distribute a codec binary.

The helper is extracted to a hash-named cache, verified and atomically published under an interprocess lock. It runs as a separate process. Windows default codec cache and temporary media are under `D:/Codex/Temp/SpectrumTag-Standalone`; `AudioFileSession::workFolder()` currently fixes that media path. Do not assume a machine without D: is supported by this delivery. A general installer should introduce an appropriate writable storage policy in a separately versioned product update.

Mono/stereo only, 8–384 kHz, 64 million samples per source and 15 minutes / 64 million samples for a loaded combined view. ARA does not advertise time-stretch support; stretched regions with differing source/playback duration require host rendering first. No video picture viewer is implemented. Edited compressed audio is re-encoded; original bitrate, complete metadata and bit-identical compressed data are not guaranteed. Unsupported output encoders report errors instead of silently changing codec families.

Actual Cubase event enumeration, normal-insert hiding of OnlyARA, first/reopened window geometry, native host undo/freeze and device listening remain explicit host acceptance items. Automated harness success is not a claim that all these manual operations have passed. See MAC_AU_HANDOFF.md for platform work.
