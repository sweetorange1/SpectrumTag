## SpectrumTag v1.1.0

`SpectrumTag` 是一个基于 JUCE 的音频效果插件（VST3 / AU / Standalone），核心能力是将图片轮廓映射为频域掩码，并在实时音频上执行 STFT 频谱“印章”（Print）处理。

项目目标是：
- 在不影响正常播放体验的前提下，把视觉形状稳定地“刻”进声音频谱；
- 在实时场景中尽可能避免点击噪声、音量突变和时序错位；
- 让后续开发者能快速理解并扩展算法链路与工程结构。

---

## 功能概览

- **实时频谱可视化（瀑布图）**
  - 独立显示 FFT 路径，不直接参与音频处理。
  - 可选 `FFT Size` 与显示刻度模式（linear / mel）。

- **图片驱动频谱印章（Print）**
  - 用户在频谱视图中放置图片框（支持拖拽与等比缩放）。
  - 图片经预处理生成二值/灰度掩码后，映射到目标频段与时间列。
  - Print 按下后，按时间推进掩码列，对 STFT bin 增益做逐帧调制。

- **参数控制**
  - `FFT Size`：窗口长度（影响时频分辨率与处理延迟）。
  - `FFT scale`：显示坐标（线性/Mel，主要影响 UI 显示）。
  - `Speed`：图片在时间轴推进速度。
  - `Amplitude Ratio`：掩码作用强度（映射到频域增益）。
  - `Invert`：掩码反相映射逻辑。

- **平滑与切换机制（v1.1.0）**
  - 逐 bin 增益平滑，降低频域突变带来的粗糙感。
  - Dry/Wet 交叉渐变（crossfade），覆盖 Print 进入与退出。
  - Dry 路径延迟对齐到 Wet 延迟基准，降低切换时序错位风险。

---

## 算法与音频链路

### 1) 总体处理流程

1. 输入音频进入每通道独立的 STFT 状态机。
2. 以 `N` 点窗口、`hop = N/4`（75% overlap）分帧。
3. 对每帧执行 FFT，得到复频谱（保留相位）。
4. 根据当前 Print 列与频率映射计算每个 bin 的目标增益。
5. 目标增益与历史增益做平滑后，作用于频谱幅度。
6. IFFT 回时域并进行 WOLA（窗函数重叠相加 + 归一化）。
7. 输出进入 wet FIFO，并与延迟对齐的 dry 信号做 crossfade。

### 2) 关键设计要点

- **相位保留**
  - 仅调整幅度，不直接篡改相位，降低重建伪影。

- **WOLA 归一化**
  - 输出叠加时同步累计窗平方能量，逐样本归一化，减少整体增益漂移。

- **预热机制（warmup）**
  - Print 刚开始时先填充 OLA 管线，预热期间不消费掩码列，避免“从中间开始绘制”的观感。

- **列游标单次推进**
  - 在 sample-major 逻辑中统一推进，避免多通道重复推进导致时间轴跳变。

- **切换时序控制（v1.1.0）**
  - `dryWetMix` 在进入/退出 Print 时向目标值平滑过渡（默认约 8ms）。
  - Dry 信号通过延迟环对齐到 `N - hop`，与 wet 共享时间基准，减少瞬态点击与错位。

---

## 主要代码结构

- [PluginProcessor.h](D:/SpectrumTag/PluginProcessor.h)
  - 处理器状态定义：参数、Print 状态、STFT 通道状态、crossfade 状态。

- [PluginProcessor.cpp](D:/SpectrumTag/PluginProcessor.cpp)
  - 核心 DSP 实现：
    - `prepareToPlay` / `rebuildStft`
    - `processBlock`
    - STFT 帧处理与 OLA 重建
    - Print 掩码列推进与增益映射

- [PluginEditor.h](D:/SpectrumTag/PluginEditor.h)
- [PluginEditor.cpp](D:/SpectrumTag/PluginEditor.cpp)
  - UI 交互：参数控件、频谱视图、图片框、Print 按钮与状态联动。

- [CMakeLists.txt](D:/SpectrumTag/CMakeLists.txt)
  - JUCE 工程配置与插件版本号源（当前 `1.1.0`）。

- [SpectrumTag_installer.iss](D:/SpectrumTag/SpectrumTag_installer.iss)
  - Windows 安装包脚本。

- [build_macos_installer.sh](D:/SpectrumTag/build_macos_installer.sh)
  - macOS `pkg/dmg` 打包脚本。

---

## 参数与行为说明（开发视角）

- **FFT Size**
  - 影响：频率分辨率、时间分辨率、算法延迟（`latency = N - hop`）。
  - 建议：处理中避免频繁切换；当前实现仅在非 Print 时允许重建，减少爆音风险。

- **Amplitude Ratio**
  - 通过内部映射转成频域目标增益（非线性/线性取决于实现）。
  - 与 `Invert` 组合决定掩码亮暗区域的“抑制/保留”方向。

- **Speed + Duration**
  - 决定 `printColPerSample`，间接决定图片从左到右“写入”频谱的速度。

---

## 构建与打包

### 本地构建（示例）

- 依赖：CMake 3.22+、支持 C++17 的编译器。
- JUCE：由 `FetchContent` 自动拉取（见 [CMakeLists.txt](D:/SpectrumTag/CMakeLists.txt)）。

常规流程（按你的本地 IDE/CMake 工作流执行）：
- 配置生成项目；
- 构建 `SpectrumTag` 目标；
- 在 `SpectrumTag_artefacts/Release` 下获取 VST3/AU/Standalone 产物。

### 安装包

- Windows：使用 [SpectrumTag_installer.iss](D:/SpectrumTag/SpectrumTag_installer.iss) 生成安装程序。
- macOS：使用 [build_macos_installer.sh](D:/SpectrumTag/build_macos_installer.sh) 生成 `.pkg` 与 `.dmg`。

---

## 后续开发建议（重要）

- **先看处理器链路再改 UI**
  - 涉及音频行为问题时，优先阅读 [PluginProcessor.cpp](D:/SpectrumTag/PluginProcessor.cpp) 的 `processBlock` 与 STFT/WOLA 相关实现。

- **改动切换逻辑时的最小回归清单**
  - Print 快速连点是否有点击声；
  - Print 开始/结束是否有波形错位；
  - 掩码绘制起点是否稳定（不“从中间开始”）；
  - `Amplitude Ratio=0` 时是否仍出现异常增益抬升。

- **性能注意**
  - 避免在音频线程中引入大对象频繁分配；
  - 频繁重建 FFT 状态应限制在安全时机（当前为非 Print）。

- **可扩展方向**
  - 更平滑的参数自动化（跨 block 插值）；
  - 多种窗函数/重建策略切换；
  - 更细粒度的频段映射与 psychoacoustic weighting；
  - 离线渲染模式（非实时）以获得更高质量印章效果。

---

## 版本信息

- 当前版本：`v1.1.0`
- 本版本重点：
  - 统一工程与安装脚本版本号；
  - 文档化 DSP 架构与开发规则；
  - dry/wet 切换与时序对齐策略说明完善。
