# Change history

This is a concise record of the Windows development delivered after upstream 1.4.3. Features were iterated during development; the README and handoff describe the current behaviour. Windows plugins 1.4.18 and standalone 1.4.19 were designated Stable on 2026-11-06. Earlier Stable decisions and delivery versions are not retroactively renamed.

| Version | Delivered change |
| --- | --- |
| 1.4.19 (standalone) | Single Windows EXE with embedded compressed codec/notices; verified first-use extraction, reuse, repair and concurrent startup. Plugins remain 1.4.18. |
| 1.4.18 | Proportional corner resize for selected Prints with deferred rendering and one-action Undo; standalone video soundtrack import/drop and video/audio-only export. |
| 1.4.17 | First-click Print selection/scale recall without seeking; previous/next Print toolbar; zoom visibility fixes; ARA fresh Mel and same-track snapshot reuse; remove ARA percentage scaling; modern standalone with file import/export and local transport. |
| 1.4.16 | Selected Print B/W preview; stale/deleted raster cleanup; Speed/Fit correction; latest-edit cancellation; ARA Clear, host-sized layout, OnlyARA categorisation and incremental timeline caches. |
| 1.4.15 | First dedicated Windows ARA target, source/region/model/archive implementation, host seeking, model harness and platform font handling. |
| 1.4.14 | Correct Range option from 22 kHz to 24 kHz while retaining automation indices and existing Print data. Previously designated realtime Stable. |
| 1.4.13 | insight no longer changes Range. Previously designated realtime Stable. |
| 1.4.12 | Noise Fill preview uses original spectral power, mask and compensation instead of a fixed ghost brightness. |
| 1.4.11 | Remove selectable Super Vision, default Mel (RX), preserve legacy indices; continuous B/W preview accepts completed intermediate conversions. |
| 1.4.10 | One common time unit across frequency scales; visible duration display; Shift parameter fine adjustment; stopped realtime Print handling. |
| 1.4.9 | Mel/Bark/Log/Extended Log and screenshot-calibrated reference scales; subsequent versions remove Super Vision and retain Range on insight. |
| 1.4.8 | Clear Record resets live view; background raster/peak caching and eased pan; right-click toggles image, dedicated Fit icon; rendering/delete feedback improvements. |
| 1.4.7 | View-only modified wheel; selected Print movement; deferred simulations/progress; recorded-range overview and local-audition gap handling. |
| 1.4.6 | Image aspect/gesture handling; shared undo/redo; selected Print editing, replacement and noise removal; offline Record tail preservation; Clear Record. |
| 1.4.5 | Recorded timeline/editing foundation, 8 remembered palettes, UI scale, cached last image, preview/controls, reduced FFT menu and bilingual help. |
| 1.4.4 | Sharper WOLA masking and startup/Nyquist fixes; transport-aware display; shared image conversion and Invert preview; first realtime regression suite. |

Original 1.4.3 and earlier history remains in upstream Git history. Original license, attribution and unchanged historical installer/standalone source files are retained.
