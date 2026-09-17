# SpectrumTag 项目总览（开发者文档）

面向维护者的工程说明。用户文档见 [README.md](README.md)。

---

## 1. 项目定位

SpectrumTag 把**图片轮廓**转为**频域掩码**，再通过 STFT / OLA 把它"印章"到音频频谱上。

项目同时提供两个形态，**产品重心是独立程序（Standalone）**：

| | 独立程序（主） | VST3 / AU 插件（补充） |
| --- | --- | --- |
| CMake target | `SpectrumTagStandalone`（`juce_add_gui_app`） | `SpectrumTag`（`juce_add_plugin`） |
| 入口 | `Standalone/StandaloneMain.cpp` | `PluginProcessor.cpp` / `PluginEditor.cpp` |
| 音频来源 | 用户导入的音频文件（整段进内存） | 宿主实时音频流 |
| Print 触发 | UI 按钮（一次性离线渲染） | UI 按钮 / `Print Trigger` 自动化 / MIDI Learn |
| 渲染路径 | `Standalone/OfflineRenderer.cpp`（阻塞式整段渲染 + 后台线程） | `PluginProcessor.cpp` 的 `processBlock`（逐 block 实时） |
| 状态持久化 | 无（一次性任务） | DAW 工程（`getStateInformation`，含图片路径与图片框位置） |

设计含义：**独立程序是"离线批处理工具"，插件是"实时效果器"**。两者共用视觉规范与核心算法思想，但**算法实现各自一份**（见 §7.9），改动时需同步。

---

## 2. 目录结构

```
SpectrumTag/
├── CMakeLists.txt                 # 两个 target + 聚合入口 + 版本号源
├── PluginProcessor.h/.cpp         # 插件 DSP：STFT/OLA、Print 状态机、自动化触发
├── PluginEditor.h/.cpp            # 插件 UI：频谱视图、图片框、参数控件、Print 按钮
├── Standalone/
│   ├── StandaloneMain.cpp         # gui_app 入口：主窗口、更新检查、遥测
│   ├── StandaloneWindow.h/.cpp    # 主组件：拖放、参数、Print 调度、静态频谱视图
│   ├── OfflineRenderer.h/.cpp     # 离线 STFT/OLA 印章合成（与插件数学一致）
│   └── SharedUI.h/.cpp            # 深色主题 LookAndFeel、Print 按钮、图片框组件、频率映射
├── source/
│   ├── network/                   # Version(SemVer) / UpdateChecker / 版本常量
│   └── ui/                        # UpdateDialog（独立原生窗口更新弹窗）
├── shared/IisaacTelemetry.h       # 遥测（header-only）
├── otf/                           # 标题字体，编译进 BinaryData
├── build_installer.bat            # Windows 打包（Inno Setup 6）
├── SpectrumTag_installer.iss      # Windows 安装脚本（VST3 + Standalone 组件）
└── build_macos_installer.sh       # macOS pkg/dmg 打包（VST3 + AU + Standalone.app）
```

---

## 3. 构建系统

### 3.1 target 关系

```
juce_add_plugin(SpectrumTag)  ──▶ SpectrumTag_VST3 / SpectrumTag_AU
                              └──▶ SpectrumTag_All  ◀── add_dependencies ──┐
                                                                          │
juce_add_gui_app(SpectrumTagStandalone) ──────────────────────────────────┘
```

- `juce_add_plugin` 的 `FORMATS` 只有 `VST3 AU`，**不生成插件式 Standalone wrapper**（避免与独立程序出现同名 exe）。
- `SpectrumTag_All` 默认只聚合插件格式，因此末尾用 `add_dependencies(SpectrumTag_All SpectrumTagStandalone)` 把独立程序挂进去：**构建 `SpectrumTag_All` 就同时产出插件与 exe**。

### 3.2 依赖与开关

- JUCE 8.0.12 由 `FetchContent` 自动拉取。
- `NEEDS_CURL TRUE`；**Windows 上额外强制 `JUCE_USE_CURL=1`**（JUCE 默认 WinHTTP 后端实测会静默失败，表现为"永远不弹更新"）。
- 字体通过 `juce_add_binary_data(SpectrumTagBinaryData ...)` 编入二进制，两个 target 都链接它。
- 独立程序额外定义 `JUCE_WEB_BROWSER=0 / JUCE_DISPLAY_SPLASH_SCREEN=0 / JUCE_REPORT_APP_USAGE=0`。

### 3.3 构建命令

```bash
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release --config Release --target SpectrumTag_All
```

产物：

- `cmake-build-release/SpectrumTagStandalone_artefacts/Release/SpectrumTag.exe`（macOS：`SpectrumTag.app`）
- `cmake-build-release/SpectrumTag_artefacts/Release/VST3/SpectrumTag.vst3`

> **注意**：`CMakeLists.txt` 中 `ICON_BIG "UI.png"` 要求仓库根目录存在 `UI.png`（JUCE 在 configure 时转换出 `icon.ico`，供 `SpectrumTagStandalone` 的 exe 使用）。该文件已在版本控制中，当前设计沿用 Print 按钮视觉：深色圆角卡片 + 居中的白色圆点 + 圆内黑色 `Spectrum` / `Tag` 两行字（字体 BasementGrotesque Black，与 UI 一致）。换图标时直接替换根目录 `UI.png` 并**删除 build 目录重新 configure** 即可；仅重新编译不会刷新 Explorer 里已缓存的旧图标。

### 3.4 打包

- Windows：`build_installer.bat` 自动探测 VST3 bundle 与 `SpectrumTag.exe`，通过 `-DSTANDALONE_EXE=` 传给 `SpectrumTag_installer.iss`；有 exe 时安装包出现「VST3 plug-in / standalone application」两个组件，独立程序装到 `{autopf}\iisaacbeats.cn\SpectrumTag\`。未找到 exe 只告警并退化为纯插件包。
- macOS：`build_macos_installer.sh` 打 VST3 + AU，若 `SpectrumTagStandalone_artefacts/Release/SpectrumTag.app` 存在则一并打进 `/Applications`（缺失则跳过）。

---

## 4. 架构与数据流

### 4.1 独立程序（主路径）

```
[拖入音频文件]
   │  AudioFormatManager → AudioBuffer<float>（全曲驻留内存）
   ▼
StandaloneAudioSpectrogramView::setAudio()
   │  一次性烘焙整段时频图 → juce::Image（宽 = 帧数，高 = 内容区高度）
   │  每列：加窗 → FFT → 幅度 → dB → heatmap 颜色
   ▼
[频谱显示]  Speed → 横向缩放；鼠标拖动 → viewOffsetPx 平移
   │
[拖入图片] → ImageBoxComponent：缩放至 ≤2048px → 二值化 → 预览
   │  图片框 normRect（x=时间，y=频率，顶部=高频）
   ▼
[点 Print]
   │  ① getImageBoxTimeRange()      → startSec / endSec（含 Speed 与 viewOffset 反变换）
   │  ② normRect 纵向 → FreqMap     → freqLowNorm / freqHighNorm
   │  ③ generateMask(fftSize/2+1, cols) → mask（cols = 图片框像素宽度，clamp[4,4096]）
   ▼
RenderJob（后台 juce::Thread）
   │  OfflineRenderer::render()：整段 STFT/OLA，区间外 unity，端点 20ms crossfade
   │  进度回调 → renderProgress（主线程 10Hz 绘制）
   ▼
WavAudioFormat 写盘 → <原名>_tagged.wav（同采样率/位深/声道；重名自动加 _1 _2）
```

### 4.2 插件路径

```
宿主音频流 → processBlock
   ├─ STFT 分帧（N，hop=N/4）→ FFT → 保留相位、按 mask 列调制幅度 → IFFT
   ├─ WOLA 重建（窗平方能量归一化）→ Wet FIFO
   ├─ Dry 延迟环（N - hop）对齐 → dry/wet crossfade
   └─ Print 状态机：
        · 实时：Editor 30Hz timer 消费 automationPrintRequest
        · 离线（无 Editor）：音频线程在 processBlock 入口自行消费触发
   └─ 五层防误触：边缘触发 / 状态守门 / 资源守门 / 自动复位 / 加载抑制(sample-clock)
```

---

## 5. 核心模块

| 文件 | 职责 |
| --- | --- |
| `Standalone/StandaloneWindow.cpp` | 主组件：拖放分发、参数联动、Print 调度与 WAV 导出、静态频谱视图实现 |
| `Standalone/OfflineRenderer.cpp` | 离线 STFT/OLA 合成；`computeBinGainsForCol` 负责把 mask 行映射到 bin 增益 |
| `Standalone/SharedUI.cpp` | `ImageBoxComponent`（拖动/等比缩放/二值化/生成 mask）、`StandaloneLookAndFeel`、圆形 Print 按钮、频率映射 `FreqMap` |
| `PluginProcessor.cpp` | 实时 DSP、Print 状态机、自动化触发、工程持久化 |
| `PluginEditor.cpp` | 插件 UI（与独立程序视觉一致） |
| `source/network/UpdateChecker.cpp` | 异步 HTTPS 更新检查（5s 超时、失败静默、本地 SemVer 复核） |
| `source/ui/UpdateDialog.cpp` | 更新弹窗（独立原生窗口、置顶、可拖拽、Download / Remind Me Later） |
| `shared/IisaacTelemetry.h` | 遥测会话（插件与独立程序分别调用入口） |

---

## 6. 参数语义（两侧一致）

| 参数 | 范围 | 实现要点 |
| --- | --- | --- |
| `FFT Size` | 1024/2048/4096/8192 | 决定 mask 行数（`N/2+1`）与 STFT 分辨率；独立程序侧切换后调用 `rebuildSpectrogram()` 重新烘焙 |
| `FFT scale` | linear / mel | 频谱显示刻度，且决定图片框纵向 → 频率的映射（`FreqMap` 的 linear/log 版本） |
| `Speed` | 0.1–4.0 | 独立程序：时频图横向缩放，**并通过 `getImageBoxTimeRange()` 改变图片框覆盖的音频时长**；插件：掩码列推进速度 |
| `Amplitude Ratio` | 0.0–1.5 | `gain = jmap(mask, 1.0f, ratioGain)`；0 = 轮廓被抹除，1 = 无变化，>1 = 轮廓被提升 |
| `Invert` | bool | 交换映射方向：`jmap(m, ratioGain, 1.0f)` |

---

## 7. 关键实现细节 / 易踩的坑

1. **插件四字符码必须唯一**：`PLUGIN_MANUFACTURER_CODE Iisa` + `PLUGIN_CODE SpTg`。曾误用另一款插件（Pupon）的码，导致两个插件 UID 相同、宿主加载 SpectrumTag 实际打开的是 Pupon。改码后需清理宿主缓存与已安装的旧 bundle。
2. **`FFT scale` 切换必须重新烘焙时频图**：时频图是预先烘焙的 `juce::Image`，频率→像素行映射在烘焙时固化。只 `repaint()` 不重烘焙，会出现"切换后画面不变、重载音频才生效"。`setScaleMode()` 已内置重烘焙。
3. **`Speed` 不只是显示参数**：`getImageBoxTimeRange()` 用 `nativeX = (viewX - viewOffsetPx) / stretch` 反变换，因此 Speed 直接影响输出 WAV 中印章覆盖的时间长度。
4. **mask 维度约定**：`rows = fftSize/2 + 1`，`cols = 图片框像素宽度`（clamp 到 `[4, 4096]`），渲染时按 `rel * maskCols` 取列。
5. **坐标方向**：图片框 `normRect.y = 0` 是**顶部 = 高频**。频率区间由 `fTop / fBot` 取 min/max 得到。
6. **Windows 必须 `JUCE_USE_CURL=1`**，否则更新检查静默失败。
7. **图片二值化会自动切换模式**：透明像素占比 > 5% 时用 Alpha 通道做阈值，否则用平均亮度做阈值。
8. **导出文件命名**：`<原名>_tagged.wav`，同目录重名则追加 `_1`、`_2`…；位深 clamp 到 16–32。
9. **算法双份实现**：`OfflineRenderer` 与 `PluginProcessor` 的 STFT/OLA 数学必须保持一致（加窗、平滑系数、WOLA 归一化、`ratioToGain`）。任一侧改动都应同步另一侧并对比导出结果。
10. **Standalone 无 `AudioProcessor`**：`SharedUI.*` 被刻意写成不依赖 `juce::AudioProcessor`，以便在 `juce_add_gui_app` 目标中复用。

---

## 8. 网络、更新与遥测

- 更新检查：进程启动后延迟 5 秒触发一次，进程级去重（插件在 Processor 构造后，独立程序在 `StandaloneMain.cpp` 的 `initialise`）。
- 请求带 `product / version / platform(<os>-<arch>)`；超时 5 秒；失败静默；本地再用 SemVer 复核一次，避免服务端异常数据误弹窗。
- 有更新时弹 `UpdateDialog`（Download 打开下载页 / Remind Me Later 关闭）；`force_update` 时不显示 Remind 按钮。
- 遥测：`shared/IisaacTelemetry.h`，插件与独立程序各自调用（独立程序用 `forStandalone`）。

---

## 9. 版本号与发布流程

版本号散落在这些位置，升级时需同步：

- `CMakeLists.txt`：`project(SpectrumTag VERSION x.y.z)` 与 `juce_add_plugin(VERSION x.y.z)`、`juce_add_gui_app(VERSION x.y.z)`
- `SpectrumTag_installer.iss`：`MyAppVersion`、`OutputBaseFilename`
- `build_installer.bat`：`APP_VERSION` 与输出文件名
- `build_macos_installer.sh`：默认从 `CMakeLists.txt` 解析，也可用 `--version` 覆盖

发布步骤：

1. 同步版本号 → Release 构建 `SpectrumTag_All`；
2. Windows 运行 `build_installer.bat`（需 Inno Setup 6），macOS 运行 `build_macos_installer.sh`；
3. 更新服务端最新版本信息与下载页；
4. 打 tag 并附 `dist/` 产物。

---

## 10. 改动后的回归清单

**独立程序**

- [ ] 拖入音频后时频图正确；切换 `FFT Size` / `FFT scale` 立即重算；
- [ ] 拖动 / 缩放图片框后，导出结果的时间与频率落点符合预期；
- [ ] `Amplitude Ratio = 0 / 1 / 1.5` 与 `Invert` 组合行为正确；
- [ ] `Speed` 改变后导出印章的时长随之变化；
- [ ] 输出 WAV 的采样率、位深、声道数与原文件一致；重名文件不覆盖；
- [ ] 导出期间再次点 Print 被忽略；失败路径有弹窗。

**插件**

- [ ] Print 快速连点无爆音、无波形错位；
- [ ] 自动化曲线 `0→1` 上升沿只触发一次，结束后参数自动复位；
- [ ] 工程加载瞬间不误触发；图片丢失时不阻塞；
- [ ] 离线 bounce 输出中印章正确写入。

---

## 11. 已知问题与后续方向

- 算法在插件与独立程序各实现一份，长期看应抽取为共享模块并加一致性测试。
- 后续方向：跨 block 参数插值、可选窗函数、更细粒度的频段映射与心理声学加权、工程迁移时按文件名在预设目录做图片 fallback 查找。

---

## 12. 许可证

AGPL-3.0，详见 [LICENSE](LICENSE) 与 [README.md](README.md) 的许可证章节。商业化授权需联系作者协商双许可证。
