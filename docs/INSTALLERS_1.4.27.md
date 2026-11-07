# SpectrumTag 1.4.27 diagnostics and installer verification

2026-11-08. Product source: `f7e4b1e17f2aeab73fe3a0f6b2c51d01246ec363`. This is a numbered diagnostic delivery, not a user-designated Stable release. The friend's Mac display failure is still under investigation; exported audio reportedly contains the Print. The user explicitly excludes opposite-polarity source music. The separate phase-cancellation test must not be presented as the proven cause of that field report.

## Diagnostic workflow

All editors have a top-right **导出诊断 / Diagnostics** button. Reproduce the issue and use it before closing the window, restarting playback or switching scales. The report is captured before the native Save dialog opens, without a forced tick, render or repaint. It saves a human-readable JSON to a user-selected destination (Desktop initially). Nothing is uploaded automatically.

The allow-list includes product/JUCE/OS version, host executable name, wrapper type, architecture/Rosetta, channels and sample rate, parameters, engine progress/revision, requested/result/applied raster generations, snapshot matching, visibility/scale/viewport, paint counters, and aggregate audio/FFT/pixel statistics for up to 64 Prints. At most 120 half-second state samples are held in memory while the editor is open. No source audio, image, screenshot, project state, source file name/path, source identifiers, credentials or environment dump are serialized. The report does not change recorded audio or start Print/FFT work. A normal single-Print test report was approximately 8 KB; longer histories are larger but bounded.

Relevant implementation: `shared/Diagnostics.h`, `PluginEditor.cpp` and `PluginEditor.h`. Tests: `tests/Revision1427Checks.h` plus the shared realtime/ARA/standalone checks in `tests/Revision1416Checks.h`.

## New verification

- Windows: full processor/UI regression, ARA host/UI regression, standalone audio/video self-test, first-Load view and fileless Print UI checks passed. Both VST3s passed pluginval level 5. A sandbox-only standalone test could not write the application's fixed D-drive scratch directory; the same final binary passed with that test process allowed to write its scratch files. No source workaround or relaxed assertion was used.
- Normal stereo, silence/Noise Fill and opposite-polarity stereo test patterns independently check actual changed audio and final spectrum pixels with playback stopped. Normal stereo final bright-minus-dark levels: approximately 0.697 on Windows, Apple Silicon and Intel. These are synthetic tests, not the friend's original project.
- Both Mac native architectures passed build, processor/UI, ARA, all standalone media/view/tail/missing/fileless/video checks, both AU checks and both VST3 checks in [run 42397569525](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/actions/runs/42397569525). Native JSON reports identify arm64/macOS 15.8.2 and x86_64/macOS 15.7.9 correctly.
- The same run merged Universal bundles, verified versions and architecture slices, created/expanded the PKG and actually installed it on a disposable Mac runner. Installed codec, AU validation and VST3 signatures passed.
- [Windows installation run 42398042515](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/actions/runs/42398042515) installed the published Setup on a disposable runner, verified all three versions/guides, and passed embedded codec and original-format audio/video checks. Installed binary hashes match the local final products below.
- Earlier .27 run 42396004841 built both native architectures but failed a strict selection-size assertion. The follow-up preserves subpixel pixel-rounding when already aligned; all current tests pass. That first run is not delivery evidence. Its build cache was reused for the incremental rebuild.

## Windows identity

| Component | Version | SHA-256 |
| --- | --- | --- |
| Standalone | 1.4.27 | `F4463E5E45BF28CC3A9A54485DC19B62184B1D27CC158EFB3285F591B6CA8A9D` |
| Realtime VST3 | 1.4.27 | `CA7A7E5E36E1BED96E6398C7A52E3E5CF43E126A3A524B66E6E08309456127DF` |
| ARA VST3 | 1.4.27 | `AC396D51763CF2E03EA4E0973E1B44971FA391A7DE7F85687C2C2B43FE388774` |

Windows Setup: 52,560,260 bytes; SHA-256 `C37A33DEF07CA274A48C91F3272D276944E7A1300F17C9D9DE012942B3D18BAF`.

Universal macOS PKG: 124,178,337 bytes; SHA-256 `E390519CAD3AD2FA32ECDED18F02590381D0D4A61F21561C639433C98C00F233`. Its Actions artifact ZIP matched the server digest `19AD2C0EC45066DD804F0DA183CAFD7E198F290D0BA91D8117AA7FAF03B02864`. The slow direct CLI download was replaced by the same artifact obtained through the GitHub connector; no rebuild or package modification occurred.

## Scope boundaries

Final installers and bilingual TXT guides are in `D:\Codex\Outputs\SpectrumTag-1.4.27-Installers` and `C:\Users\86189\Desktop\SpectrumTag-1.4.27-Installers`. Both copies contain exactly four files: two root TXT guides, `Win/SpectrumTag-1.4.27-Windows-Setup.exe` and `Mac/SpectrumTag-1.4.27-macOS.pkg`; all copies were compared by SHA-256. Build/test evidence stays in `D:\Codex\Temp\SpectrumTag-1.4.27`. Local installed apps/plugins were not overwritten. Previous Plan B is inherited as a frozen state and was not accessed or refreshed; there is no new backup request or Stable designation. Existing old packages remain intact.

Mac applications/plugins use ad-hoc signatures, with no Developer ID Installer signature or notarization for the PKG. Windows Setup has no commercial publisher signature. The guides retain those installation boundaries.

Upstream PR #2 remains closed. No new upstream PR until the user personally validates Mac and explicitly authorizes it. A successful automated normal-stereo rendering test is not proof that the friend's actual host/project issue is fixed; use the returned JSON to continue diagnosis.
