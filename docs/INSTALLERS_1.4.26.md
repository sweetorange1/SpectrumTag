# SpectrumTag 1.4.26 installer verification

Status: automated native and actual installer checks complete, 2026-11-08. User Mac project acceptance is pending. This version has not been designated Stable by the user.

## Scope and inherited work

The requested delivery is a Windows installation wizard and one macOS Universal PKG, with Chinese and English TXT installation instructions. Windows contains realtime VST3, ARA VST3 and standalone. macOS contains realtime/ARA VST3, realtime/ARA AU and standalone, for macOS 15 or later on Apple Silicon and Intel.

Windows standalone 1.4.24 retains its user-designated Stable status. Its completed Plan B remains frozen and has not been revisited. No new backup or Stable designation is implied by this release. Installed local products are not replaced during installer preparation.

The final product source is `b6fcd56`; subsequent installer validation and documentation changes do not alter product inputs. Windows processor and ARA regression results from 1.4.25 are inherited because the later changes affect the standalone encoder interface and POSIX frame transport, not the shared audio/rendering code.

## Platform fixes and evidence

- 1.4.25 fixed Clang selecting an unsupported JUCE SIMD overload for a scalar progress calculation, by using `std::max<size_t>`.
- 1.4.26 queries encoder capabilities for video frame timing: current encoders use `-fps_mode:v:0 passthrough`, while the embedded legacy Windows encoder retains `-vsync 0`. Tail checks compare original frames, black padding and audio alignment, rather than merely checking that a file exists.
- POSIX video FIFO backpressure now waits with `poll`, instead of imposing a fixed 5 ms delay per partial write. Cancellation remains bounded. On the Apple Silicon runner the same 3.2-second MP4 check completed in 1439.5 ms after previously exceeding 180 seconds. This is a short controlled check, not a promise for full-length exports or other hardware.
- Windows 1.4.26: both VST3s passed pluginval strictness 5; tail/negative-time Print and video export checks passed; spectrum video passed all four containers, four resolutions, 30/60 fps, frame count, colour, processed soundtrack alignment and cancellation protection.
- [Native run 42387867710](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/actions/runs/42387867710): all Apple Silicon checks passed. Intel passed build, processor/ARA, media and VST3 validation; its AU discovery check did not find newly copied components.
- [Resume run 42389088688](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/actions/runs/42389088688) reused the exact cached Intel build and completed Apple Silicon artifact. Source compatibility was checked before reuse. Refreshing the disposable runner's AU registrar resolved discovery; both Intel AUs passed auval. No cache deletion or signing-policy relaxation was used. Its subsequent packaging step exposed a `lipo` argument-order error, fixed without rebuilding the products.
- [PKG run 42389473698](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/actions/runs/42389473698) reused both validated native archives, verified both architecture slices and bundle versions, built and expanded the PKG, then actually installed it. Installed codec, both AUs and both VST3 code signatures passed. Installer source/packaging commit: `b7b0fc6009b975a9b0110925f16b31cf8c01ffc3`.
- [Windows installation run 42389606510](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/actions/runs/42389606510) installed the exact published Setup on a disposable Windows runner, verified all three product versions and the installed guides, then passed embedded codec and standalone original-format audio/video checks. All three installed binary hashes exactly match the final local builds below.

## Final Windows component identity

| Component | Product version | SHA-256 |
| --- | --- | --- |
| Realtime VST3 | 1.4.26 | `D8B363728E13B751E39EDCC5CA993CC6961069781ED152E8013A1681E0D4E332` |
| ARA VST3 | 1.4.26 | `9F7D5927DED2F580D4B3AAC85D688EB262EF396B50285AD50D3A495FDA1C08F8` |
| Standalone | 1.4.26 | `B4F893738B8166A0A9CBEBF35A0FDD6D935D10A501EC3A7D3A603E34CB29FEDB` |

Final Windows Setup: 52,539,932 bytes, SHA-256 `CE787DB49AE356FDF6030F9F9E8D7E405F8027F1D9D3FD2F40718B8A4FB1D521`.

Final Universal macOS PKG: 123,968,220 bytes, SHA-256 `A9EC662E6918727F96D3B32BB107787AD2E1A350C38B0EE5490DB17DF16C9256`.

The previous 1.4.25 installer check remains historical evidence and is not represented as a 1.4.26 result. Final files and bilingual TXT guides are in `D:\Codex\Outputs\SpectrumTag-1.4.26-Installers`; logs and diagnostic media are kept outside that delivery directory.

## Acceptance boundaries

The Mac application/plugins use ad-hoc signatures. The PKG has no Developer ID Installer signature or Apple notarization; Windows Setup has no commercial code signature. Installation instructions document the resulting prompts, standard plugin paths, duplicate-version checks, and Logic ARA/Rosetta requirements.

Automated native, format and installer checks do not constitute the user's Cubase/Logic project acceptance. Upstream PR #2 remains closed. **No new upstream PR is to be created until the user personally tests the Mac version and explicitly confirms that it works.**
