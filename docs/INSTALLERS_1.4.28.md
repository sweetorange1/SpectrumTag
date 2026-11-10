# SpectrumTag 1.4.28 — cross-platform installers

Completed 2026-11-09. User-designated Stable. Product/packaging source: `1bd36269f90bd3e99551620511ec12014d83a3c4`. Later completion/README changes do not alter the delivered binaries or rebuild them.

## Reused and newly completed

Reused: the previously delivered Windows standalone and its video/UI checks, recorded in [WINDOWS_1.4.28.md](WINDOWS_1.4.28.md). Its SHA-256 remains `D1A9D74C3A1D7A86F820C8AE45543A79938196BC56E8114B5A3109216012FA1F`.

Newly completed: version-matched Windows realtime/ARA VST3, Mac native realtime/ARA VST3 and AU plus standalone, Windows Setup and Universal Mac PKG, Chinese-named Chinese TXT and English TXT guides, public Release and real-file Desktop delivery. Plugin audio processing is unchanged; the feature is standalone 4:3, 3:4, 16:9 and 9:16 spectrum-video export.

The explicitly requested Plan B completed before publishing: source `b762eb9ef12a5010d16cc9cd8107d6bad320db06`, 479 files, 74,914,433 bytes, 17 key hashes matched. Its frozen location is `D:\备份文件\Vibe Coding\SpectrumTag\源代码\SpectrumTag-1.4.28-Stable`. Later work only cited `D:\Codex\Temp\SpectrumTag-1.4.28\PLAN_B_COMPLETION.md`; no backup was reread, refreshed or modified.

## Validation evidence

- Windows local processor/UI and ARA harnesses passed; both VST3 bundles passed pluginval 1.0.4 strictness level 5.
- Windows Setup built with Inno Setup 6.7.3. Innounp 2.71.3 integrity test passed and all 11 extracted payload files matched the staged files by SHA-256. The obsolete innoextract 1.9 could not parse this installer format; the compatible integrity check resolved that tooling limitation without changing the installer.
- [Windows actual installation](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/actions/runs/42564174659): the published exact Setup SHA-256 was checked, all three components installed successfully on a disposable runner, versions and both guides verified. Installed EXE/realtime VST3/ARA VST3 hashes matched the local final binaries. Embedded codec, audio/video media, four video aspect ratios and fileless UI checks passed.
- [Mac native and Universal package pipeline](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/actions/runs/42562703901): arm64 and x86_64 native builds, processor/UI, ARA, media, initial view, tail extension, missing media, fileless UI and video checks all passed. Both AUs passed auval and both VST3s passed pluginval level 5 on each architecture.
- Mac video checks included the four output aspect ratios and physical canvas dimensions, decoded 9:16 Tag pixels, audio/video synchronization, remembered settings, cancellation and safe destination handling.
- Universal bundles contain arm64 and x86_64; matching codec helpers/dependencies are packaged and bundles ad-hoc signed. The PKG actually installed on the disposable Mac runner; installed codec, both AU validations, VST3 strict signature checks and both language guides passed.
- Downloaded Mac artifacts and Windows install evidence matched GitHub's artifact SHA-256. All four Desktop files matched the D-drive deliverables. The optional Release ZIP was checked file-by-file against those same four deliverables.

Windows installed product hashes:
- Standalone: `D1A9D74C3A1D7A86F820C8AE45543A79938196BC56E8114B5A3109216012FA1F`
- Realtime VST3: `9E20EAE4BF9530509422D62E401503856DCA62CFE2574D1FBD70D84947EF88DB`
- ARA VST3: `A3D16F78FFF34CBAD0078B6A4F5BE36AC378048BEF1482464E5400E8BF50C956`

## Delivery

[Stable Release](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/releases/tag/v1.4.28), publicly published only after the user expressly authorized public binary distribution and pre-publication checks completed. Subsequent Windows actual-install checks above also passed.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| 安装说明.txt | 12278 | `4875324FB3340CEA75E96A79E99672A93CABE6BAAAFE05A84EE579B87DD07B85` |
| Installation-Guide.txt | 11692 | `97E05F4FC5C1B50E411A79610B7AB6BD4987D6A6CB08061BB6D160049E1A20F4` |
| SpectrumTag-1.4.28-macOS.pkg | 124183664 | `996A1E68A3FE19A4009076C167D3DBBE15D80891E22D75F576E0957124E629C8` |
| SpectrumTag-1.4.28-Windows-Setup.exe | 52561609 | `318BB7A047D744CED071A95CE7CB39C2E157165BA4701D95243CFECDAF63F7C5` |

Windows Setup includes realtime VST3, ARA VST3 and standalone. Mac PKG includes realtime/ARA VST3, realtime/ARA AU and standalone for Apple Silicon and Intel, targeting macOS 15+.

Lean four-file delivery: `安装说明.txt`, `Installation-Guide.txt`, `Win/SpectrumTag-1.4.28-Windows-Setup.exe`, `Mac/SpectrumTag-1.4.28-macOS.pkg`. GitHub normalizes the Chinese direct-upload asset to `default.txt` despite its Chinese display label; the additional complete-download ZIP preserves `安装说明.txt` and the Win/Mac folder structure. Local delivery and both installed resources use the actual Chinese filename.

Output: `D:\Codex\Outputs\SpectrumTag-1.4.28-Installers`.

Real-file Desktop copy: `C:\Users\86189\Desktop\SpectrumTag-1.4.28-Installers`.

Logs/artifact hashes/completion records: `D:\Codex\Temp\SpectrumTag-1.4.28`. No local installed plugins were overwritten for this packaging run. Existing 1.4.27 products and frozen backups remain intact.

## Scope limits and upstream

No commercial Windows publisher signature or Apple Developer ID/notarization is provided. Mac bundles use ad-hoc signing. Automated validation is separate from DAW/project field acceptance; Mac standalone 1.4.27 field confirmation is inherited historical evidence, not a new claim for all hosts. ARA integration and host sizing still depend on the host. Standalone installer destination refinements remain deferred.

The user-named `Ziqing-Gu/Spectrum-Tag-Private` repository remains public; no visibility change. Upstream PR #3 already merged. A new incremental PR is authorized for the original author's review; preserve their README dedication, project name, original authorship and AGPL-3.0. No automatic merge or upstream Release is performed. See [1.4.28 handoff](UPSTREAM_HANDOFF_1.4.28.md).
