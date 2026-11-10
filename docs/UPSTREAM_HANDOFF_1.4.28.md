# SpectrumTag 1.4.28 — incremental upstream handoff

2026-11-09. This contribution follows merged PR #3 and is based on upstream `7ad36f425d182fe04241213cc0e5af6852b3667a`. It preserves the original author's README dedication and project branding. The user explicitly requested an incremental merge request after designating 1.4.28 Stable, completing a new frozen Plan B and validating the requested installers.

## Product change

Standalone spectrum-video export can select 4:3, 3:4, 16:9 and 9:16. Short-edge quality (720/1080/1440/2160) determines the actual width and height displayed in the menu. Selection is remembered; existing settings retain 16:9. Tag camera proportions and right-edge reveal remain correct in landscape and portrait. Original-video export keeps its original picture dimensions.

Entry points: `Standalone/ModernStandalone.cpp`, `Standalone/SpectrumVideo.h`, `Standalone/SpectrumVideo.cpp`, `tests/SpectrumVideoChecks.h`. The shared processor/ARA/audio Print implementation is unchanged. CMake sets every delivered product to 1.4.28.

## Packaging and documentation

Chinese installation instructions are now `docs/安装说明.txt`; Windows and Mac package resources and installer checks use that filename. English instructions remain `docs/INSTALL-EN.txt`. Windows actual-install CI additionally runs video and fileless UI regression checks. Development decisions/history are in `PROJECT_OVERVIEW.md`; completion evidence is in [INSTALLERS_1.4.28.md](INSTALLERS_1.4.28.md).

[Windows Setup and Universal Mac PKG with bilingual TXT guides](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/releases/tag/v1.4.28) are available from the contributor's Fork. The original author decides upstream publication and merging. The complete download ZIP preserves the Chinese guide filename, which GitHub normalizes on direct asset upload.

## Verification

- Windows processor/UI, ARA harness and both VST3 pluginval level 5 checks passed. Setup integrity and all 11 extracted payloads matched. [Actual installation and installed media/video/UI checks](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/actions/runs/42564174659) passed; three installed product hashes matched the local final binaries.
- [Mac arm64/x86_64 builds and tests, both AU and VST3 validations, Universal PKG creation and actual installation](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/actions/runs/42562703901) all passed. The new checks include four canvas ratios and decoded portrait Tag pixels.
- Package source `1bd36269f90bd3e99551620511ec12014d83a3c4`; final delivery records are documentation-only additions. The previously delivered Windows standalone is reused unchanged, and completed Plan B snapshots are frozen.

## Limits retained

Mac baseline is 15+, with ad-hoc bundle signatures and no Developer ID/notarization. Windows Setup is unsigned. Automated checks do not replace actual DAW acceptance, particularly host-specific ARA activation/sizing and Logic/Rosetta integration. Installation guides describe these boundaries. No source media, raw user diagnostics, SDK/codec binaries, installers or credentials are committed. Existing AGPL-3.0 and third-party notices are retained. Installer destination refinements remain deferred; this change does not claim to implement them.
