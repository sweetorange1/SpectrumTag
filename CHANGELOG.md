# Change history

2026-11-07: the user designated the delivered Windows standalone **1.4.24 Stable** and requested a new Plan B source snapshot. This is a status/documentation change only; the existing EXE and version are retained. See `docs/STABLE_1.4.24.md`. The earlier version history below retains its original identities.

This is a concise record of the Windows development delivered after upstream 1.4.3. Features were iterated during development; the README and handoff describe the current behaviour. Windows plugins 1.4.18 and standalone 1.4.19 were designated Stable on 2026-11-06. Earlier Stable decisions and delivery versions are not retroactively renamed.

| Version | Delivered change |
| --- | --- |
| 1.4.27 | In validation: phase-safe per-channel spectrum analysis, one-time display-cache refresh preserving rendered audio, and matching source/Print bounds when selecting. / 验证中：频谱改为分别分析声道后合并能量，避免反相抵消图形；保留旧缓存音频只更新显示；修正选中原图与打印区域大小不一致。 |
| 1.4.26 | Encoder-compatible video timing, responsive POSIX video streaming, Windows Setup and Universal macOS PKG with realtime/ARA VST3, AU and standalone; automated native and installation checks passed. User Mac acceptance and Stable designation remain pending. / 兼容新旧编码器、修复 Mac 频谱视频传输超时；交付 Win 安装程序及 Mac 双架构 PKG，附 TXT 说明。原生及实际安装检查通过，等待用户 Mac 工程验收，不自动设 Stable。 |
| 1.4.25 | Fix Clang/macOS scalar progress calculation selecting an unsupported JUCE SIMD overload; unify installer component versions. Packaging and platform validation in progress. / 修复 macOS 编译时进度计算误匹配 JUCE SIMD 模板，统一安装包组件版本；打包及平台验证进行中。 |
| 1.4.24 | Full-frame spectrum video enters at the right edge, with pixel-exact cached rendering and native RGB/BGR streaming; Desktop export default, cancellable progress dialog and immediate stopped-Delete cleanup. / 满屏纯频谱从最右侧显现、缓存加速、桌面默认导出、可取消进度弹窗与删除残影修复。Windows standalone user-designated Stable; cross-platform installers are being validated separately. |
| 1.4.23 | Source-free Print and animated spectrum video export, automatic Tag camera matching, overlap priority, selectable format/resolution/fps, and real processed soundtrack. / 无源文件 Print、纯频谱视频导出、视图匹配与真实处理音轨。 |
| 1.4.22 | Consistent Speed on first Load, explicit Fit, Print before/after source bounds, extended exports, video black prefix/tail with preserved sync, silent blank recovery for unavailable media. / 统一首次加载速度、独立全览、超范围打印和导出、视频前后补黑及丢失媒体空白恢复。 |
| 1.4.21 | Target-spectrum Noise Fill, content-verified rendered audio/spectrum cache, faster initial analysis, and standalone session restoration. / 目标频谱噪声补偿、内容校验渲染缓存、首次分析优化与独立版会话恢复。 |
| 1.4.20 | Windows/macOS shared build and installer foundation, native Apple/Intel/AU targets, codec packaging, platform path/font/pixel handling. Cross-platform delivery was paused pending later work. / 跨平台构建与安装基础、双架构/AU、编码器封装和平台兼容；此前交付暂停，未宣称 Mac 已验收。 |
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
