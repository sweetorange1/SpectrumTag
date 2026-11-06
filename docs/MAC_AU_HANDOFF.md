# Mac / AU 后续交接

本 PR 交付 Windows 已有成果及完整源代码。Mac VST3 / AU / ARA / 独立程序由上游继续编译和处理兼容问题；本轮没有生成、签名或实测 Mac 成品。以下是源码入口和已知待办，不是完成声明。

## 目标与身份

- 实时：`SpectrumTag_VST3` / `SpectrumTag_AU`，版本 1.4.18，`Iisa/SpTg`，bundle ID `cn.iisaacbeats.SpectrumTag`。
- ARA：开启 `SPECTRUMTAG_BUILD_ARA` 后构建 `SpectrumTagARA_VST3` / `SpectrumTagARA_AU`，版本 1.4.18，`Iisa/SpAr`，独立 bundle/factory/archive ID，AU export prefix `SpectrumTagARA`。
- 独立程序：`SpectrumTagStandalone`，版本 1.4.19，使用 `ModernStandalone.cpp`；旧 Standalone 实现已不作为当前入口。
- CMake 源码保留 `arm64;x86_64`、macOS 11.0 默认值。请核实实际产物架构，不能仅由配置推断 Universal 2 已成功。

## 优先处理

1. **独立版编码器封装**：当前嵌入资源使用 Windows PE / Win32 API。非 Windows 的 `resolveEncoder()` 除显式 `SPECTRUMTAG_FFMPEG` 覆盖外会返回无内置编码器错误。需要 macOS app bundle 中的 helper 查找、执行权限、架构、签名与相关许可处理；仅编译 app 并不足以得到可用的媒体导入导出。Windows FFmpeg EXE 不能带到 Mac 使用。
2. **平台路径**：媒体工作目录在非 Windows 分支使用用户应用数据目录，但窗口文件选择器、测试与其他默认路径仍应逐项检查。不要把 Windows D: 路径照搬到 macOS，也不要依赖可写 app bundle。检查 `AudioFileSession.h`、`ModernStandalone.cpp`、`EmbeddedCodec.cpp`、共享录音缓存和测试目录参数。
3. **测试可移植性**：Windows harness 存在 `_putenv_s` 等 Windows 写法；为 Apple Clang 处理环境变量、控制台/UI 初始化与链接差异。不要为让测试编译而改变产品的时间轴/回放语义。
4. **安装器**：现有 `build_macos_installer.sh` 是上游旧入口，未为现代独立版与单独 ARA 交付重新验收。更新目标清单、版本、bundle 资源、rpath/dependencies、签名与 notarization 后再发布。
5. **扫描与身份**：分别验证实时 AU / VST3 与 ARA AU / VST3 身份、注册缓存、归档恢复。OnlyARA 分类不保证每个宿主会隐藏普通插入入口；不能只凭 moduleinfo 得出 UI 隐藏成功的结论。

## 字体与窗口

`shared/PlatformFonts.h` 已按平台选择中文系统字体：macOS 优先 PingFang SC，其次 Hiragino Sans GB / Heiti SC / Songti SC，Windows 优先 Microsoft YaHei；最后回退系统无衬线字体。英文标题使用原嵌入字体。保持 UTF-8，不打包 Windows 系统字体，不在音频回调枚举字体。

ARA 不再提供独立的 75–250% 内部缩放菜单或内层角标。宿主实际分配的框架尺寸是布局依据；图片/控件等比例缩放，频谱利用余下空间。保留有上限的挂接同步，不通过无限 setSize 或定时重置窗口掩盖宿主尺寸问题。检查首次打开、关闭重开、停靠/浮动、Retina、多显示器、宿主缩放和中文控件裁切。

## ARA / AU 音频生命周期

不要把实时 VST3 的 Record/内部试听位置搬进 ARA。ARA Load 读取宿主实际提供的 region sequence，使用绝对工程位置、源偏移和采样率；点击非 Print 区域通过 HostPlaybackController 请求宿主定位。单击 Print 只选择/恢复参数。

同轨多片段应复用同一加载视图，保留空隙和重叠；宿主未提供的事件无法读取。关闭编辑器不终止文档拥有的 Print 处理。验证无 UI 离线渲染、source 读失败后重试、事件移动/裁剪/删除、Clear、不同采样率、共享 modification、保存重开、filtered archive restore 及宿主原生 Undo。

源音频与 Print 掩模/参数由宿主对象和归档管理，不依赖 Windows 录音缓存。跨系统后原图片路径可能失效；已有 Print 掩模仍可播放，重新做黑白转换需要重新关联原图。

## 验收顺序建议

先 Apple Clang 编译并核实各目标 Info.plist/架构和依赖，再做命令行/模型音频回归、`auval` 或相应插件验证，最后在对应宿主检查加载、窗口、播放、保存重开和离线导出。Apple Silicon 原生与需要支持的 Rosetta 场景分开验证。不要把禁用 SIP 当作兼容性修复。

Windows 回归范围见 WINDOWS_HANDOFF.md。测试通过后由上游决定如何发布 Mac 成品；此次贡献者只请求审阅合并。
