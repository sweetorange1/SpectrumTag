# SpectrumTag v1.2.3

**SpectrumTag** 是一款基于 [JUCE](https://juce.com) 框架的音频效果插件（VST3 / AU / Standalone），核心能力是把 **图片轮廓** 实时映射为 **频域掩码**，并通过 STFT/OLA 在音频信号上"印章"（Print）出对应的频谱图形。

> v1.2.0 为本插件的 **首个正式发布版本**，v1.2.3 为当前最新版本。

---

## 1. 核心特性

### 1.1 实时频谱可视化（瀑布图）
- 独立的 FFT 显示路径，不参与音频处理；调整显示参数不会影响音质。
- 可选 `FFT Size`（1024 / 2048 / 4096 / 8192）与显示刻度（线性 / Mel）。
- 频谱区域**初始即填充纯黑背景**，避免加载初期出现宿主软件灰色底色。
- 频谱**水平滚动速度与 FFT Size 解耦**：调整 FFT Size 仅改变频率分辨率，时间轴滚动速率保持稳定。

### 1.2 图片驱动频谱印章（Print）
- 在频谱视图中放置图片框，支持**鼠标拖拽**与**等比缩放**。
- 图片经预处理生成掩码后，沿时间列推进、对 STFT bin 增益做逐帧调制。
- Print 启动具有**预热机制**，先填充 OLA 管线再消费掩码列，避免"从中间开始绘制"的问题。
- 不同 FFT Size 下图像边缘均保持清晰，图像首字符不再出现"半截绘制"。

### 1.3 参数控制
| 参数 | 类型 | 说明 |
| --- | --- | --- |
| `FFT Size` | Choice | 1024 / 2048 / 4096 / 8192，影响时频分辨率与算法延迟 |
| `FFT scale` | Choice | 频谱显示刻度：linear / mel（仅影响 UI） |
| `Speed` | Float | 图片沿时间轴推进的速度 |
| `Amplitude Ratio` | Float | 掩码作用强度（频域增益映射），范围 0.0 – 1.5 |
| `Invert` | Bool | 掩码反相 |
| `Print Trigger` | Bool | **可被 DAW 自动化、MIDI Learn 控制** 的 Print 触发器（v1.2.0 新增） |

### 1.4 平滑与切换机制
- **逐 bin 增益平滑**：降低频域突变带来的粗糙感。
- **Dry/Wet 交叉渐变**：覆盖 Print 进入与退出，避免硬切换。
- **Dry 路径延迟对齐**：dry 信号通过延迟环对齐到 `N - hop`，与 wet 共享时间基准，消除切换瞬间的相位错位与点击声。
- **WOLA 重建归一化**：输出叠加时同步累计窗平方能量、逐样本归一化，减少整体增益漂移。

### 1.5 工程持久化（v1.2.0 新增）
- **图片选择状态自动保存**：DAW 工程保存时会一并记录当前选中的图片路径与图片框位置；重新加载工程时自动恢复。
- **图片文件丢失保护**：若工程迁移到其它机器或图片被移动，加载工程不会阻塞，会在日志中给出非阻塞提示，UI 自动回退到"Choose picture"占位状态。

### 1.6 Print 自动化与 MIDI Learn（v1.2.0 新增）
插件向宿主软件暴露布尔类型自动化参数 **`Print Trigger`**，支持：
- DAW **自动化曲线录制 / 回放**；
- DAW **MIDI Learn**（用 MIDI 控制器物理触发 Print）；
- 与 UI Print 按钮**双向同步**：手动点击会回写参数，外部触发会同步到按钮状态。

为保证可靠性，触发链路设计了 **五层防误触保护**：

| 层级 | 机制 | 作用 |
| --- | --- | --- |
| L1 | **边缘触发** | 仅在 `false → true` 上升沿响应，忽略保持态与下降沿 |
| L2 | **状态守门** | Print 进行中再次触发会被忽略，杜绝重复启动 |
| L3 | **资源守门** | 未选择图片时触发不生效 |
| L4 | **自动复位** | Print 结束后自动把参数复位为 `false`，下一次上升沿才能再次触发 |
| L5 | **加载抑制** | 工程加载完成后约 120 ms 内忽略所有上升沿，避免 DAW 参数回放产生伪触发 |

### 1.7 离线渲染 / 导出自动化支持（v1.2.3 新增）
v1.2.0 的 Print Trigger 依赖 Editor timer（消息线程）消费触发请求，在 DAW **离线渲染**（bounce / freeze / 导出）时 Editor 通常不会被创建，导致自动化曲线触发的 Print 水印**不会写入输出音频**。v1.2.3 彻底解决了这一问题：

- **双路径触发分发**：
  - **实时模式（UI 在场）**：保持原有行为，由 Editor 30Hz timer 在消息线程取走 `automationPrintRequest`，可复用 UI 当前的实时参数状态；
  - **离线/导出 / 无 UI**：Processor 在 `processBlock` 入口由**音频线程自行消费**触发请求，内部完成图片加载 → 二值化 → mask 生成 → `startPrint` 全流程，确保导出音频中水印正确写入。
- **Editor 感知机制**：Processor 通过 `notifyEditorAttached()` / `notifyEditorDetached()` 获知当前是否有活跃 Editor，据此切换触发消费路径。
- **像素宽度精确对齐**：`EditorState` 新增 `imgBoxWidthPx` 字段，持久化图片框的**实际屏幕像素宽度**，使离线路径的 `cols` 和 `duration` 计算与预览路径完全一致，导出效果与 UI 所见一模一样。
- **加载抑制窗口改用 sample-clock**：L5 保护机制从基于墙钟（`juce::Time::currentTimeMillis`）改为 `triggerSuppressSamplesRemaining`（在 `processBlock` 入口按 `numSamples` 扣减），确保实时与离线场景下抑制窗口时长等价，避免离线导出时误吞自动化曲线的首次上升沿。

---

## 2. 算法与音频链路

### 2.1 总体处理流程

```
Input PCM
   │
   ├──▶ STFT 分帧 (N-point, hop = N/4, 75% overlap)
   │      │
   │      ├──▶ FFT  ─▶  保留相位，仅调制幅度
   │      │              │
   │      │              ▼
   │      │         图像列 → bin 目标增益
   │      │              │
   │      │              ▼
   │      │         逐 bin 平滑
   │      │              │
   │      │              ▼
   │      │           IFFT
   │      │              │
   │      │              ▼
   │      └────▶  WOLA 重建（窗平方能量归一化）
   │                     │
   │                     ▼
   │                   Wet FIFO
   │                     │
   ├──▶ Dry 延迟环 (N - hop) ─────▶  Crossfade  ──▶ Output
   │                                     ▲
   └──── Print 状态 / Trigger ───────────┘
```

### 2.2 关键设计要点
- **相位保留**：仅修改幅度，不重写相位，避免重建伪影。
- **WOLA 归一化**：每帧叠加时维护窗平方累加器，逐样本除以归一化因子，输出能量稳定。
- **预热机制（warmup）**：Print 开启时先填充管线再开始消费掩码列，防止首字符被"截断"。
- **列游标单次推进**：sample-major 逻辑中统一推进，多通道不会重复推进时间轴。
- **dry/wet 交叉渐变**：进入 / 退出 Print 时 `dryWetMix` 向目标值平滑过渡（默认约 8 ms），消除点击噪声。
- **OLA 帧切换连续性**：归一化与累加器在帧边界保持连续，避免每个 hop 产生周期性冲激（频谱图上的"竖直细线"）。

---

## 3. 工程结构

| 路径 | 作用 |
| --- | --- |
| [PluginProcessor.h](D:/SpectrumTag/PluginProcessor.h) | 处理器声明：参数 ID、Print 状态、STFT 通道状态、crossfade 状态、Print Trigger 自动化接口 |
| [PluginProcessor.cpp](D:/SpectrumTag/PluginProcessor.cpp) | DSP 主体：`prepareToPlay` / `processBlock` / STFT / OLA / 掩码列推进 / 自动化监听 |
| [PluginEditor.h](D:/SpectrumTag/PluginEditor.h) | 编辑器声明：UI 组件、Print Trigger 同步状态 |
| [PluginEditor.cpp](D:/SpectrumTag/PluginEditor.cpp) | UI 实现：参数控件、频谱视图、图片框、Print 按钮、自动化 ↔ UI 双向同步 |
| [CMakeLists.txt](D:/SpectrumTag/CMakeLists.txt) | JUCE 工程配置与版本号源（**当前 1.2.3**） |
| [SpectrumTag_installer.iss](D:/SpectrumTag/SpectrumTag_installer.iss) | Windows 安装包脚本（Inno Setup 6） |
| [build_installer.bat](D:/SpectrumTag/build_installer.bat) | Windows 一键打包脚本 |
| [build_macos_installer.sh](D:/SpectrumTag/build_macos_installer.sh) | macOS `.pkg` / `.dmg` 一键打包脚本 |

---

## 4. 构建

### 4.1 依赖
- CMake 3.22+
- 支持 C++17 的编译器（MSVC 19.30+ / Clang 13+ / GCC 10+）
- JUCE：通过 `FetchContent` 自动拉取，无需手动安装

### 4.2 通用流程
```bash
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release --config Release --target SpectrumTag
```
构建产物：`cmake-build-release/SpectrumTag_artefacts/Release/{VST3,AU,Standalone}/`。

---

## 5. 安装包

### 5.1 Windows
1. 安装 Inno Setup 6（`winget install --id JRSoftware.InnoSetup -e`）。
2. 先完成 Release 构建（产物输出到 `cmake-build-release\SpectrumTag_artefacts\Release\VST3\SpectrumTag.vst3`）。
3. 双击运行 [build_installer.bat](D:/SpectrumTag/build_installer.bat)，安装包将输出到 `dist\SpectrumTag_Setup_1.2.4_x64.exe`。

默认安装路径：`%CommonProgramFiles%\VST3\iisaacbeats.cn\SpectrumTag.vst3`；预设安装到 `%UserProfile%\Documents\spectrumtagpreset`。

### 5.2 macOS
完成 Release 构建后运行 [build_macos_installer.sh](D:/SpectrumTag/build_macos_installer.sh) 即可生成 `.pkg` 与 `.dmg`。

---

## 6. 参数与行为说明（开发视角）

- **FFT Size**：影响频率分辨率、时间分辨率、算法延迟（`latency = N - hop`）。当前实现仅允许在**非 Print 状态**下重建 FFT，避免重建瞬间产生爆音。
- **Amplitude Ratio**：经内部映射转成频域目标增益；与 `Invert` 组合决定掩码亮暗区域的"抑制/保留"方向。
- **Speed**：决定 `printColPerSample`，间接决定图像沿时间轴的推进速度。
- **Print Trigger**：仅响应上升沿；Print 流程结束后插件会自动把它写回 `false`（DAW 会录到这一次复位）。

---

## 7. 后续维护指引

### 7.1 改动切换逻辑时的最小回归清单
- [ ] Print 快速连点是否有点击声；
- [ ] Print 开始 / 结束是否有波形错位；
- [ ] 掩码绘制起点是否稳定（不"从中间开始"）；
- [ ] `Amplitude Ratio = 0` 时是否仍出现异常增益抬升；
- [ ] DAW 工程加载瞬间是否触发了非用户预期的 Print；
- [ ] 自动化曲线 0→1 的上升沿是否稳定触发一次 Print；
- [ ] Print 结束后参数是否被复位为 0。

### 7.2 性能注意
- 音频线程中**禁止**任何动态分配、文件 I/O、锁竞争；
- 频谱日志（math log）默认关闭，仅在调试时打开；
- FFT 重建限制在非 Print 状态进行。

### 7.3 可扩展方向
- 更平滑的参数自动化（跨 block 插值）；
- 多种窗函数 / 重建策略切换；
- 更细粒度的频段映射与心理声学加权；
- 离线渲染模式（非实时）以获得更高质量印章效果；
- 工程迁移时的图片 fallback 查找（按文件名在用户预设目录搜索）。

---

## 8. 版本信息

- **当前版本**：`v1.2.3`
- v1.2.3 更新要点：
  - 离线渲染 / 导出自动化支持：双路径触发分发（实时由 Editor timer 消费，离线由音频线程自行消费），确保导出音频中 Print 水印正确写入；
  - 加载抑制窗口改用 sample-clock，实时与离线行为一致；
  - `EditorState` 新增 `imgBoxWidthPx` 字段，离线路径 cols/duration 与预览精确对齐；
  - Processor 新增 Editor 感知机制（`notifyEditorAttached/Detached`）。

- v1.2.0 发布要点：
  - 完整的 STFT / OLA / WOLA 归一化与平滑切换机制；
  - 频谱可视化背景修复与时间轴解耦；
  - 图片打印起始完整性修复，多 FFT Size 下图像质量一致；
  - 工程持久化（图片路径与图片框位置自动保存 / 恢复）；
  - Print 自动化与 MIDI Learn 支持，五层防误触保护；
  - Windows / macOS 一键打包脚本完善。

---

## 9. 许可证（License）

### 9.1 许可协议

本项目采用 **GNU Affero General Public License v3.0（AGPL-3.0）** —— 这是目前公认最严格的开源 Copyleft 协议。完整条款见项目根目录下的 [LICENSE](LICENSE) 文件。

**核心约束（摘要）**：

| 条款 | 说明 |
| --- | --- |
| **源码强制公开** | 任何分发（含二进制 / 编译产物）必须随附完整源代码，或提供可获取源码的书面承诺 |
| **衍生作品必须继承** | 任何基于本项目的修改、衍生或二次开发，无论以源代码还是目标码形式发布，**必须同样以 AGPL-3.0 协议开源** |
| **网络交互触发 Copyleft** | 即使通过远程网络服务（SaaS / 云端）提供本软件的功能，也**必须向所有使用者提供完整源代码**（AGPL 第 13 条，这正是 AGPL 区别于 GPL 的关键） |
| **禁止附加限制** | 不允对 AGPL 授予的权利施加额外收费、授权费或其它限制条件 |
| **无担保** | 本软件不提供任何形式的明示或默示担保，使用者自行承担全部风险 |

> ⚠️ **简要理解**：你可以自由使用、修改、学习本项目代码，但 —— **任何形式的再分发（包括直接销售、打包进商业产品、或通过云服务提供），都必须将你的全部修改以 AGPL-3.0 协议完整开源**。这是法律强制要求，不是道德建议。

### 9.2 反商业化滥用声明

本项目的源代码公开发布，旨在促进音频 DSP 技术社区的学习、交流与进步。**我们明确保留追究以下行为的法律权利**：

- ✗ 未经授权将本项目编译产物打包为商业软件并**收取授权费用**；
- ✗ 在未遵守 AGPL-3.0 完整义务（包括公开衍生源代码）的情况下，将本项目或其衍生作品投入**商业分发渠道**（如应用商店、付费插件平台等）；
- ✗ 去除、隐藏或篡改本项目的版权声明、许可证声明以掩盖来源。

如果你希望通过商业授权方式使用本项目（即不在 AGPL-3.0 约束下发布你的修改），请联系作者协商**双许可证（Dual Licensing）**事宜。

### 9.3 版权

© iisaacbeats.cn — 保留所有权利。
