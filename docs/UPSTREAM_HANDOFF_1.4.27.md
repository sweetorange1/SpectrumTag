# SpectrumTag 1.4.27 — upstream merge handoff

2026-11-08. The user explicitly authorized submitting the current work to `sweetorange1/SpectrumTag` for review and merge. This supersedes the earlier PR hold. The previous PR #2 remains closed and unmerged; this submission carries the complete development since upstream `ac7d32470d1e7d5ae447e00fb01ebb8ee12631e6` (1.4.3).

## What this contribution delivers

- Realtime VST3/AU: transport-aware live spectrum/Print, optional recording aligned to the DAW timeline, coverage-aware playback and internal audition, and editable non-destructive Prints with undo/redo. Unrecorded live mode remains available.
- Dedicated ARA VST3/AU: separate identities, Load/Clear instead of Record, same-track region handling, host seeking, source offsets/rates, document-owned edits and archive restoration. Host-provided regions remain the authority for available audio.
- Standalone: audio/video soundtrack load and drop, session restoration, local transport, original-format-family export, optional video preservation and pure animated spectrum export with selectable format/resolution/fps, progress and cancellation. Source-free and out-of-source-range Prints extend exported audio; video extension uses black frames with retained sync.
- Shared editing: improved B/W conversion and Invert/parameter previews; 8 palettes, bilingual UI/help, consistent time scaling, Mel/Linear/Log/Bark/Extended Log/insight scales, constrained noise compensation, Print selection/move/proportional resize/delete, background rendering and content-validated caches.
- Cross-platform delivery: native Apple Silicon/Intel builds combined into Universal bundles, macOS helper/dependency packaging, Windows embedded media codec, Windows Setup and macOS PKG, and bilingual installation guides.
- 1.4.27: stereo-energy spectrum analysis, display-only migration of old derived caches, corrected selected-image bounds and manual aggregate-only diagnostics. No user audio, images, project state, credentials or automatic uploads are included in diagnostics.

## Reviewer entry points

| Area | Entry |
| --- | --- |
| Development decisions and historical failures | `PROJECT_OVERVIEW.md`, `CHANGELOG.md` |
| Processor / editor | `PluginProcessor.*`, `PluginEditor.*` |
| Original audio, edits, render/cache and display workers | `shared/Recording.*`, `RenderCache.h`, `ViewRenderer.h`, `TargetFill.h` |
| ARA model / playback / archive | `ARA/ARAIntegration.*` |
| Standalone media and video export | `Standalone/ModernStandalone.cpp`, `AudioFileSession.h`, `SpectrumVideo.*`, `EmbeddedCodec.*` |
| Current platform evidence | `docs/INSTALLERS_1.4.27.md`, `docs/MAC_DIAGNOSTIC_1.4.27.md` |
| Build / installer entry points | `CMakeLists.txt`, `build-*-windows.ps1`, `.github/workflows/cross-platform.yml`, `packaging/windows/`, `packaging/macos/` |
| User installation notes | `docs/INSTALL-ZH-CN.txt`, `docs/INSTALL-EN.txt` |

All current product targets use **1.4.27**. The user designated this version Stable. Earlier handoffs and overview entries retain their historical version numbers and pending statements; this document and the overview's current-status section take precedence.

## Verification reused from the delivered products

Product source commit: `f7e4b1e17f2aeab73fe3a0f6b2c51d01246ec363`. Subsequent preparation changes documentation only; product inputs and delivered installers are reused.

- Windows: processor/UI and ARA regressions, standalone audio/video and view checks, and both VST3 pluginval level-5 checks passed. Windows Setup was actually installed and its three installed product hashes matched the local final builds in [run 42398042515](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/actions/runs/42398042515).
- macOS: Apple Silicon and Intel native regressions, media/video checks, both AU and both VST3 checks passed in [run 42397569525](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/actions/runs/42397569525). Universal PKG creation and actual installation checks passed in the same run.
- Field feedback: the user confirmed that the Print was visible with the original image hidden in the reported **Mac standalone 1.4.27** case. Its diagnostic data agrees. This does not prove every VST3/AU/ARA host case has been checked, nor establish the unique cause of the original failure.
- Existing [Windows and Mac installers with TXT guides](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/releases/tag/v1.4.27) are available for review. This PR does not rebuild, replace or republish those products.

## Remaining boundaries for the maintainer

- macOS installer baseline is 15+, with arm64 and x86_64 slices. Bundles use ad-hoc signatures; the PKG has no Developer ID Installer signature or notarization. Windows Setup has no commercial publisher signature.
- DAW-specific ARA registration, visibility in ordinary insert menus, first-open sizing, track/region edits, host undo and archive behaviour still warrant host testing. Logic ARA/Rosetta constraints are described in the installation guide. OnlyARA metadata alone is not proof of menu hiding.
- Standalone destination selection improvements were deferred by the user. Windows upgrades may skip the directory page; the Mac package currently places the App in `/Applications`. Do not describe that requested change as implemented.
- Windows development defaults include local D-drive paths; the scripts accept explicit dependency/build/runtime paths. Mac uses platform paths and bundled native helpers. Configure a suitable Windows location on machines without D: before general distribution.
- The original author attribution and AGPL-3.0 license remain intact. Third-party SDK and codec licenses/notices retain their own boundaries. No SDK/codec binaries, generated installers, source media or raw user diagnostic report are committed in this PR.
- Frozen Plan B backups are historical records, not build or release inputs. No backup is accessed or refreshed by this submission. Product changes after the delivered 1.4.27 require a new numerical version; review/documentation-only updates do not.

The request is for the original author's review and merge decision. No automatic merge or upstream release is performed.
