# SpectrumTag

把图片转换成黑白频域掩模，将图形打印进音频的频谱。基于 iisaacbeats.cn / sweetorange1 的 SpectrumTag，保留原项目署名、资源与 AGPL-3.0 许可。

## 本次 Windows Stable 交接

| 产品 | 版本 | 工作方式 |
| --- | --- | --- |
| SpectrumTag 实时 VST3 | 1.4.18 | 实时 Print 试听；Record 捕获工程时间轴后可编辑、删除和撤销 Print |
| SpectrumTag ARA | 1.4.18 | 独立 ARA 身份；Load / Clear 操作宿主音频，同轨事件按工程位置排列，可请求宿主定位 |
| SpectrumTag 独立程序 | 1.4.19 | Load / 拖入音频或视频音轨，本地播放、编辑并另存导出；Windows 单文件 EXE |

这些版本已由贡献者指定为当前 Windows Stable 交接基线。Mac VST3、AU、ARA 和独立程序的本轮修改尚未构建或验收，由上游继续处理；**此 PR 不附带 Mac 成品**。两款插件的版本仍为 1.4.18，只有独立程序的单文件封装升至 1.4.19。

- [构建、架构、验证记录与维护说明](docs/WINDOWS_HANDOFF.md)
- [Mac / AU 后续工作](docs/MAC_AU_HANDOFF.md)
- [1.4.4–1.4.19 变更摘要](CHANGELOG.md)

## 编辑体验

- 自动阈值 Otsu、局部自适应、手动阈值和透明度剪影；反转、保留音量和噪声补偿均有预览。噪声补偿仅用于已录制/加载的音频，并限制在 Print 矩形内。
- 默认 Mel (RX)、20 kHz 上限；可选 Linear、Bark、Log、Extended Log、insight。所有模式共用时间尺度，insight 保留当前 Range；实际频率不超过音源 Nyquist。
- 8 种记忆配色，中英文控件及悬停说明，记忆原图。眼睛按钮或频谱右键切换原图，右上角图标全览。
- 滚轮平移时间，Ctrl / Shift + 滚轮改变横向视图缩放。原图保持原始长宽比；点击已打印图形恢复其参数及正常显示比例，不移动指针。
- 选中 Print 后可拖动位置、拖动右下角等比例缩放、调整参数、按 Delete 删除。拖动时显示模拟预览，松手后后台渲染。重复同位置 Print 不叠加加深；删除同时移除对应噪声补偿。
- Ctrl+Z / Ctrl+Shift+Z 撤销与重做；工具栏箭头选择上/下一个 Print。Shift 拖动参数微调，Alt+左键恢复控件默认值。

## 三种播放逻辑

实时 VST3 未录音时是即时音效；停止播放后频谱停止、未完成 Print 结束。Record 保留 DAW 的绝对时间和片段空隙。正常播放时已录区域输出录制/渲染结果，未录区域保留宿主输入。插件内点击试听不改变 DAW 的指针；内部试听进入未录空隙时输出静音，停止后重新跟随宿主。

ARA 通过宿主接口请求定位。Load 使用宿主提供的同轨事件，Clear 清理所加载轨道范围内的打印状态，保留 DAW 原音频。ARA 的 OnlyARA 分类已设置，但不同宿主是否从普通插入菜单隐藏它仍需实测。

独立版从文件加载，频谱点击可直接定位本地播放指针。支持常见音频以及 MP4 / M4V / MOV / MKV / WebM / AVI 音轨；视频不含画面播放器。导入视频后可选择导出视频或仅导出音频：视频保留画面流，替换第一条音轨；音频保持源编码族及适合的容器。压缩音频需要重新编码，不承诺原码率或逐字节相同。源文件不会被覆盖。

## 构建入口

需要 CMake 3.22+、C++17、Visual Studio 2022 C++ 工具。JUCE 固定 8.0.15；ARA SDK 在启用 ARA 时获取固定版本，也可显式指定本地 SDK。

```powershell
.\build-windows.ps1 -BuildDir D:\Build\SpectrumTag
.\build-ara-windows.ps1 -BuildDir D:\Build\SpectrumTagARA -AraSdkDir D:\Dependencies\ARA_SDK
.\build-standalone-windows.ps1 -BuildDir D:\Build\SpectrumTagStandalone -RuntimeDir D:\Dependencies\SpectrumTagRuntime
```

`RuntimeDir` 需要 `ffmpeg.exe`、`COPYING.GPLv3`、`FFmpeg-NOTICE.txt`。这些二进制依赖不放进本源码 PR。1.4.19 将它们压缩嵌入 Windows EXE，首次使用自动释放并校验缓存，无须 EXE 旁的配套文件夹。当前 Windows 缓存/临时媒体默认使用 D 盘；移植与打包注意事项见维护说明。

上游旧安装器脚本和旧 `Standalone/` 实现保留作历史参考；当前独立版入口是 `Standalone/ModernStandalone.cpp`。旧安装器尚未适配本次 ARA / 新独立版交付，勿直接作为本轮发布入口。

## English overview

This contribution adds a recorded-timeline editor to the realtime VST3, a dedicated ARA extension, and a shared standalone audio/video-soundtrack editor. Windows plugins are 1.4.18; the single-file Windows standalone is 1.4.19. Existing source audio is retained and Print operations are replaceable, removable and undoable. Spectrum simulations during edits are replaced by the actual rendered result. Mac/AU compilation, codec packaging and host acceptance remain follow-up work for the upstream maintainer; see the linked handoff documents for exact boundaries and prior Windows validation.

---

## 9. 许可证（License）

### 9.1 许可协议

本项目采用 **GNU Affero General Public License v3.0（AGPL-3.0）**，完整条款见 [LICENSE](LICENSE)。

| 条款 | 说明 |
| --- | --- |
| **源码强制公开** | 任何分发（含二进制 / 编译产物）必须随附完整源代码，或提供可获取源码的书面承诺 |
| **衍生作品必须继承** | 任何基于本项目的修改、衍生或二次开发，无论以源码还是目标码发布，**必须同样以 AGPL-3.0 开源** |
| **网络交互触发 Copyleft** | 即使通过远程网络服务（SaaS / 云端）提供本软件功能，也**必须向所有使用者提供完整源代码**（AGPL 第 13 条） |
| **禁止附加限制** | 不得对 AGPL 授予的权利施加额外收费、授权费或其它限制 |
| **无担保** | 不提供任何明示或默示担保，使用者自行承担风险 |

> 简要理解：可以自由使用、修改、学习本项目代码，但**任何形式的再分发（销售、打包进商业产品、云服务提供），都必须将你的全部修改以 AGPL-3.0 完整开源**。这是法律强制要求。

### 9.2 反商业化滥用声明

源代码公开旨在促进音频 DSP 技术社区的学习与交流。**我们明确保留追究以下行为的法律权利**：

- 未经授权将本项目编译产物打包为商业软件并收取授权费用；
- 在未遵守 AGPL-3.0 完整义务（包括公开衍生源代码）的情况下投入商业分发渠道（应用商店、付费插件平台等）；
- 去除、隐藏或篡改版权与许可证声明以掩盖来源。

如需商业授权（不在 AGPL-3.0 下公开你的修改），请联系作者协商 **双许可证（Dual Licensing）**。

### 9.3 版权

© iisaacbeats.cn — 保留所有权利。
