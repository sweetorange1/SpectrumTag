# Spectrum Tag（私人）

**把图片打印进声音的频谱。 / Print images into the spectrum of sound.**

[中文](#中文) · [English](#english) · [下载 / Downloads](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/releases) · [更新记录 / Changelog](CHANGELOG.md) · [开发总览 / Development overview](PROJECT_OVERVIEW.md)

基于 [sweetorange1 / SpectrumTag](https://github.com/sweetorange1/SpectrumTag)，原作者 **iisaacbeats.cn**。本仓库是 **Ziqing-Gu 维护的 Fork**，保留原项目署名与 AGPL-3.0 许可。

Based on [sweetorange1 / SpectrumTag](https://github.com/sweetorange1/SpectrumTag), originally by **iisaacbeats.cn**. This is the **Ziqing-Gu fork**, retaining the original attribution and AGPL-3.0 license.

## 中文

### 这是什么？

SpectrumTag 将图片转换成黑白掩模，并通过处理音频的频率成分，在频谱中留下图形。可以实时试听，也可以在录制或加载音频后编辑 Print，再导出处理结果。独立版还支持直接在空白时间轴上打印，并导出声音或纯频谱动画。

### 版本与下载

| 项目 | 当前状态 |
| --- | --- |
| 当前交付 | **1.4.27**，带“导出诊断”的 Windows Setup 与 macOS Universal PKG，附中英文 TXT 安装说明 |
| 已确认 Stable | **Windows 独立版 1.4.24**；其他组件沿用各自历史状态，详见[开发总览](PROJECT_OVERVIEW.md) |
| Windows 安装包 | 实时 VST3、ARA 专用 VST3、独立程序；适用于 Windows 10/11 x64 |
| macOS 安装包 | 实时/ARA VST3、实时/ARA AU、独立 App；目标为 macOS 15+，Apple Silicon + Intel |

下载 [1.4.27 安装包与 TXT 说明](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/releases/tag/v1.4.27)。Windows 实际安装检查、两个 Mac 架构的音视频及 AU/VST3 检查、Universal PKG 实际安装检查均已通过，详见[验证记录](docs/INSTALLERS_1.4.27.md)。**本版用于调查 Mac“Print 完成但内部不显示”的现场问题，尚未确认该问题修复，也未指定为 Stable。** 复现后点右上角“导出诊断”，将保存的 JSON 手动发回；报告不包含音乐、图片或工程，也不自动上传。上游旧 PR 已关闭，等待用户亲自确认 Mac 版后再提交新 PR。

### 三个版本怎么选？

| 版本 | 适合的工作方式 |
| --- | --- |
| **实时 VST3 / AU** | 插入 DAW 轨道，边播放边体验 Print。Record 后可在录制的时间线上编辑、删除、撤销 Print；Clear Record 返回未录制模式。 |
| **ARA** | 从支持 ARA 的宿主入口使用，Load 获取宿主音频，同轨事件保留工程位置。无录音按钮；支持请求宿主定位，Clear 后可重新加载。 |
| **独立版** | Load 或拖入音频/视频音轨，本地播放、Print 并另存导出。不需要 DAW，也可以不导入文件直接开始。 |

实时插件内部点击试听不会改变 DAW 指针。正常跟随宿主时，已录区域输出处理结果，未录区域保留宿主输入；内部独立试听进入未录空隙时静音，停止后重新跟随宿主。ARA 和独立版则使用各自的定位接口。

### 主要功能

- **图像转换**：自动阈值 Otsu、局部自适应、手动阈值和透明度剪影；支持黑白强度、反转及效果预览。
- **可编辑 Print**：移动、右下角等比例缩放、修改参数、删除、撤销与重做。拖动时先显示预览，再后台计算；同位置重新 Print 不叠加加深，删除同时移除对应补偿噪声。
- **噪声补偿**：为静音或能量不足的频段合成目标频谱，补偿量可调，仅作用于 Print 范围。独立版空白打印需要提高 Noise Fill 才会生成声音。
- **频谱视图**：默认 Mel (RX)、20 kHz；提供 Linear、Bark、Log、Extended Log 和 insight。共用时间尺度，insight 不改变显示上限；实际频率受采样率限制。
- **操作与外观**：8 种记忆配色、中英文界面和悬停说明、原图显示/隐藏、录音/加载范围全览。原图始终保持正确长宽比。
- **恢复与缓存**：保存 Print 编辑及处理缓存；独立版可恢复上次会话。可用缓存能够避免重复渲染，缓存缺失或内容变化时会重新分析；无法恢复的媒体静默留空。

### 独立版的音视频导出

支持常见音频及 MP4、M4V、MOV、MKV、WebM、AVI 的音轨。导入视频后，可选择保留原画面、替换处理后的音轨，或仅导出音频。它不包含视频画面编辑器。原格式导出保留受支持的编码族和适合的容器；压缩音频会重新编码，不承诺与原文件逐字节相同。

Print 可以超出原始文件的开始或结尾，导出时长会相应延长。视频超出原范围的部分补黑画面，原音画保持同步。源文件不被覆盖。

**导出频谱视频**生成满屏纯频谱，无标题、刻度和时间文字，从最右侧逐渐显现并向左滚动。画面按 Tag 的时间、比例和频率刻度平滑调整，声音保持正常速度；重叠 Tag 优先跟随较早开始者。可选 MP4/MOV/MKV/WebM、720p/1080p/1440p/4K、30/60 fps，提供进度弹窗及取消。导出默认打开桌面目录，也可自行选择位置。

### 常用操作

| 操作 | 功能 |
| --- | --- |
| 鼠标滚轮 | 左右平移频谱 |
| Ctrl / Shift + 滚轮 | 调整横向视图缩放；选中 Print 时只改变视图，不重新渲染 |
| 选中 Print 后拖动 Speed 拉杆 | 改变打印时长，完成操作后重新渲染 |
| 单击已打印图形 | 选中并恢复其参数和正常显示比例，不移动播放指针 |
| 拖动已选 Print / 右下角 | 移动位置 / 等比例缩放 |
| 双击原图 | 选择新图片 |
| 频谱右键 / 眼睛图标 | 显示或隐藏原图 |
| 频谱右上角全览图标 | 适应已录制/加载的范围 |
| Delete | 删除选中的 Print |
| Ctrl+Z / Ctrl+Shift+Z | 撤销 / 重做 |
| Shift 拖动参数 / Alt+左键 | 微调 / 恢复默认值 |

### 安装与常见问题

完整说明：[中文 TXT](docs/INSTALL-ZH-CN.txt) · [English TXT](docs/INSTALL-EN.txt)。安装前保存工程并完全退出宿主及独立版，安装时按需选择组件。

- **Windows**：运行 `SpectrumTag-1.4.27-Windows-Setup.exe` 安装向导。VST3 安装到系统标准目录，独立程序目录可选择；包含所需微软 x64 运行库和媒体编解码器。
- **macOS**：运行 `SpectrumTag-1.4.27-macOS.pkg`，安装到标准的 Applications / Audio Plug-Ins 目录，App 内含双架构媒体依赖，无需另装 Homebrew 或 FFmpeg。
- 当前没有 Windows 发布者签名或 Apple Developer ID 签名/公证。macOS bundle 使用 ad-hoc 签名。系统提示、重扫插件、旧版本冲突、Logic ARA/Rosetta 和字体等问题均见安装说明。
- ARA 需要宿主支持及正确的扩展入口。OnlyARA 标记不能保证所有宿主都从普通插入菜单隐藏它。自动化构建/验证通过也不代表所有 DAW 工程都已实测。

### 构建与开发记录

使用 CMake 3.22+、C++17、JUCE 8.0.15；Windows 使用 Visual Studio 2022 C++ 工具，Mac 使用 Xcode 命令行工具。ARA SDK 通过 CMake 的固定版本配置获取，也可指定本地 SDK。

```powershell
.\build-windows.ps1 -BuildDir D:\Codex\Temp\SpectrumTag-Windows
.\build-ara-windows.ps1 -BuildDir D:\Codex\Temp\SpectrumTag-ARA -AraSdkDir D:\Dependencies\ARA_SDK
.\build-standalone-windows.ps1 -BuildDir D:\Codex\Temp\SpectrumTag-Standalone -RuntimeDir D:\Dependencies\SpectrumTagRuntime
```

Windows 独立版的 RuntimeDir 需要 `ffmpeg.exe`、`COPYING.GPLv3` 和 `FFmpeg-NOTICE.txt`，构建时嵌入 EXE。Mac 的 [Cross-platform installers](.github/workflows/cross-platform.yml) 工作流分别构建两个架构，再合并并制作 PKG。当前安装入口位于 [packaging](packaging)，旧安装脚本和旧 Standalone 实现仅作历史参考。

- [项目总览与完整开发过程](PROJECT_OVERVIEW.md)
- [逐版更新](CHANGELOG.md)
- [Windows 架构与历史交接](docs/WINDOWS_HANDOFF.md)
- [Mac / AU 历史交接](docs/MAC_AU_HANDOFF.md)

---

## English

### What is SpectrumTag?

SpectrumTag converts an image to a monochrome mask and processes audio frequencies so the image appears in a spectrogram. Use it as a realtime effect, edit Prints in recorded or loaded audio, or start with an empty standalone timeline and export sound or a spectrum animation.

### Version and downloads

| Item | Status |
| --- | --- |
| Current delivery | **1.4.27**, Windows Setup and macOS Universal PKG with Diagnostics, plus Chinese/English TXT installation guides |
| User-designated Stable | **Windows standalone 1.4.24**; other components retain their documented historical status |
| Windows installer | Realtime VST3, dedicated ARA VST3 and standalone; Windows 10/11 x64 |
| macOS installer | Realtime/ARA VST3, realtime/ARA AU and standalone App; macOS 15+, Apple Silicon + Intel |

Download the [1.4.27 installers and TXT guides](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/releases/tag/v1.4.27). Actual Windows installation, native media/AU/VST3 checks on both Mac architectures, and Universal PKG installation have passed; see the [verification record](docs/INSTALLERS_1.4.27.md). **This version investigates the Mac report that completed Prints are missing from the internal display; that field issue is not yet confirmed fixed, and this version is not designated Stable.** Click Diagnostics immediately after reproducing the issue and send the saved JSON manually. No media, images, project files or automatic upload. The old upstream PR is closed; a new PR must wait for the user's personal Mac validation and explicit confirmation.

### Choose your edition

| Edition | Workflow |
| --- | --- |
| **Realtime VST3 / AU** | Print while your DAW plays. Record captures a timeline for editable Prints; Clear Record returns to live operation. |
| **ARA** | Load host-provided audio with same-track events at their project positions. No Record button; host seeking and Clear/reload are available. |
| **Standalone** | Load or drop audio/video soundtracks, play locally, edit and export. No DAW or source file is required to begin. |

Realtime internal audition does not move the DAW cursor. When following the host, recorded areas play processed audio and unrecorded areas pass through the host input. Independent internal audition is silent in unrecorded gaps; stopping restores host follow. ARA and standalone use their own seeking interfaces.

### Features

- **Image conversion:** Otsu, adaptive, manual threshold and alpha silhouette, with amount, invert and processing previews.
- **Editable Prints:** move, resize proportionally from the corner, change parameters, delete, undo and redo. Edits show a preview before background rendering; printing again at the same position replaces the prior operation, and deletion removes its Noise Fill too.
- **Noise Fill:** adjustable target-spectrum synthesis for silent or weak frequency content, confined to the Print area. Source-free standalone Prints need nonzero Noise Fill to produce sound.
- **Spectrum display:** Mel (RX) and 20 kHz by default; Linear, Bark, Log, Extended Log and insight are available. All share a time scale; insight leaves Range unchanged, and the source sample rate limits usable frequencies.
- **Interface:** eight remembered palettes, Chinese/English controls and tooltips, source-image visibility and recorded/loaded-range overview. The original image retains its aspect ratio.
- **Persistence:** saved edits and content-checked render caches, plus standalone session restoration. Missing or changed caches require analysis; unavailable media restores to a blank workspace without a missing-file prompt.

### Standalone export

Load common audio formats or the soundtrack of MP4/M4V/MOV/MKV/WebM/AVI files. Export processed audio alone or preserve the original video with a replaced soundtrack. There is no video picture editor. Original-format export retains supported codec families and suitable containers; compressed audio is re-encoded and is not byte-identical to the original.

Prints may extend before or after the source. Export duration expands accordingly; video receives black padding while the original audio and picture stay aligned. Source files are not overwritten.

**Spectrum video export** fills the frame with the actual processed spectrum, without titles, axes or time labels. New content enters from the right edge and scrolls left. The view smoothly follows each Tag's time, proportions and frequency scale without changing audio playback speed; earlier-starting Tags take priority during overlaps. Choose MP4/MOV/MKV/WebM, 720p/1080p/1440p/4K and 30/60 fps. A progress dialog supports cancellation. Export dialogs start at the Desktop and allow another destination.

### Controls

| Action | Result |
| --- | --- |
| Mouse wheel | Pan horizontally |
| Ctrl / Shift + wheel | Change horizontal view zoom; selected Prints do not re-render |
| Drag Speed with a Print selected | Change Print duration and render after the edit |
| Click a printed shape | Select it and recall its parameters and normal display proportions without seeking |
| Drag a selected Print / its lower-right corner | Move / resize proportionally |
| Double-click the original image | Choose another image |
| Right-click the spectrum / eye icon | Toggle the original image |
| Top-right overview icon | Fit the recorded/loaded range |
| Delete | Remove the selected Print |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Shift-drag a parameter / Alt-click | Fine adjustment / reset to default |

### Installation and development

Read [English TXT instructions](docs/INSTALL-EN.txt) or [中文 TXT](docs/INSTALL-ZH-CN.txt). Save your project and quit hosts and SpectrumTag before installing; select the components you need.

- **Windows:** `SpectrumTag-1.4.27-Windows-Setup.exe` is an installation wizard. It uses the standard VST3 location, allows a standalone destination, and includes the required Microsoft x64 runtime and media codec.
- **macOS:** `SpectrumTag-1.4.27-macOS.pkg` installs to standard Applications / Audio Plug-Ins locations. Bundled media dependencies require neither Homebrew nor a separate FFmpeg installation.
- There is no Windows publisher signature or Apple Developer ID signing/notarization. Mac bundles are ad-hoc signed. The guides cover system prompts, rescanning, duplicate versions, Logic ARA/Rosetta and fonts.
- ARA requires a compatible host and the correct extension entry point. OnlyARA metadata cannot guarantee hiding from every host's ordinary insert menu. Automated validation is distinct from testing real projects in every DAW.

Build with CMake 3.22+, C++17 and JUCE 8.0.15, using Visual Studio 2022 on Windows or Xcode command-line tools on Mac. See the Windows commands above. The standalone Windows codec input contains `ffmpeg.exe`, `COPYING.GPLv3` and `FFmpeg-NOTICE.txt`; it is embedded at build time. [Cross-platform installers](.github/workflows/cross-platform.yml) builds both Mac architectures and merges them into a PKG. Current packaging scripts are in [packaging](packaging).

See the [development overview](PROJECT_OVERVIEW.md), [changelog](CHANGELOG.md), [Windows handoff](docs/WINDOWS_HANDOFF.md) and [Mac/AU handoff](docs/MAC_AU_HANDOFF.md) for implementation details and historical validation boundaries.

## 署名与许可 / Credits and license

原项目与资源 / Original project and assets: **iisaacbeats.cn / sweetorange1**. Fork development: **Ziqing-Gu**.

项目许可保持 **GNU Affero General Public License v3.0 (AGPL-3.0)**，以 [LICENSE](LICENSE) 完整文本为准。第三方依赖及媒体编解码器保留各自许可与声明，随相应安装组件提供。

The project remains under **GNU Affero General Public License v3.0 (AGPL-3.0)**; see [LICENSE](LICENSE) for the full terms. Third-party dependencies and media codecs retain their own licenses and notices, supplied with the corresponding components.

© iisaacbeats.cn. 原作者署名与版权信息保留。 / Original author attribution and copyright notices retained.
