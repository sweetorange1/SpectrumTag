# SpectrumTag 项目总览与开发过程

维护日期：2026-11-09。本文件是后续维护的统一入口：记录当前架构、需求演变、开发过程、问题诊断、验证边界与平台交接。面向用户的操作说明见 [README](README.md)。

## 当前完成状态：1.4.28 安装包、公开 Release 与增量交接（2026-11-09）

继承此前 Windows 独立版交付、Stable 指定和已完成的新 Plan B。本次补齐两个 Windows VST3、Mac 双架构 VST3/ARA/AU/独立版及两平台安装包；未重复独立版构建，未访问或刷新冻结备份。Windows 实装、安装后媒体/四种画幅/UI 检查，以及 Mac 原生和 Universal PKG 实装均通过。构建源码 `1bd36269f90bd3e99551620511ec12014d83a3c4`；详细哈希和范围见 [安装包验证](docs/INSTALLERS_1.4.28.md)。

用户明确确认“验证完成后公开发布到 Release”，随后发布 [1.4.28 Stable](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/releases/tag/v1.4.28)。桌面 `C:\Users\86189\Desktop\SpectrumTag-1.4.28-Installers` 为真实四文件交付：Win Setup、Mac PKG、`安装说明.txt` 和 `Installation-Guide.txt`，与 D 盘成品逐一哈希一致。GitHub 会自动改写中文附件名，因此另附完整下载 ZIP 保留中文文件名；安装包内部与本地文件均已使用中文名。

验证：[Windows 实装](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/actions/runs/42564174659)、[Mac 双架构与 PKG 实装](https://github.com/Ziqing-Gu/Spectrum-Tag-Private/actions/runs/42562703901)。本次只申请新的增量 PR，保留上游已添加的致谢和项目名，不自动合并。原作者的审阅入口见 [1.4.28 交接](docs/UPSTREAM_HANDOFF_1.4.28.md)。下文的“生成中”等文字是执行时的历史状态，以本节完成记录为准。

## 当前状态：1.4.28 Stable，Plan B 后继续安装包与上游交接（2026-11-09）

用户明确指定 1.4.28 为 Stable，要求先执行新的 Plan B，再将当前源码同步到个人 Fork，生成 Windows Setup 与 macOS Universal PKG（含 AU/ARA/独立版），附中英文 TXT 安装说明，中文版文件名使用中文，最后提交给原作者申请合并。此前 [PR #3](https://github.com/sweetorange1/SpectrumTag/pull/3) 已于 2026-11-08 合并；本轮应另建仅包含后续变化的增量 PR。

1.4.28 Windows 独立版的编译、横竖画幅和界面测试及 D 盘覆盖记录直接复用。Stable 是用户对本版的指定，不代表尚未生成的 Mac/插件安装包已经验证。新的源码备份目标为 `D:\备份文件\Vibe Coding\SpectrumTag\源代码\SpectrumTag-1.4.28-Stable`，完成后冻结；后续只引用完成记录，安装说明或发布记录的后续变化不刷新该快照。上一完整跨平台 Stable 为 1.4.27，保留为回滚基线。详见 [Stable 记录](docs/STABLE_1.4.28.md)。

Plan B 已完成并冻结：源提交 `b762eb9ef12a5010d16cc9cd8107d6bad320db06`，479 个文件、74,914,433 字节、17 项关键哈希一致；只引用 `D:\Codex\Temp\SpectrumTag-1.4.28\PLAN_B_COMPLETION.md`，不再访问备份。本轮后续将中文安装文档改名为 `安装说明.txt`，Windows/Mac 安装资源和验证流程同步采用中文文件名；完整安装包结果见 [1.4.28 安装包记录](docs/INSTALLERS_1.4.28.md)。这些发布文档和打包调整不改变已交付独立版的二进制，也不触发新备份。

## 1.4.28：频谱视频横竖画幅（2026-11-09）

用户要求导出视频可选择 4:3、3:4、16:9 和 9:16。独立版“导出频谱视频”新增画面比例菜单，分辨率按短边 720、1080、1440、2160 四档计算并直接显示实际像素尺寸；切换比例保持当前质量档位。默认 16:9，记住上次选择；旧设置未保存比例时沿用原有 16:9，不改变格式、分辨率和帧率。新画幅继续使用既有 Tag 镜头适配，以保持图形比例、画面铺满及最右侧逐渐显现。

本轮先生成 Windows 独立版 1.4.28；Mac、插件和安装包不在本轮重建。1.4.27 Stable 与冻结 Plan B 保留原状。已提交的上游 [PR #3](https://github.com/sweetorange1/SpectrumTag/pull/3) 保持 1.4.27，本轮使用独立开发分支，未自动追加待验证功能。实现、验证与交付记录见 [1.4.28 Windows 记录](docs/WINDOWS_1.4.28.md)。

## 当前交接：获准向原作者提交 1.4.27（2026-11-08）

用户在确认本次 Mac 独立版打印已经显示后明确要求：“OK，下面可以PR给我朋友了”。此指令解除此前暂缓提交新 PR 的限制；目标为 `sweetorange1/SpectrumTag` 的 `main`，由原作者审阅和决定合并。旧 PR #2 保持关闭，本次以独立分支提出包含当前 1.4.27 的新请求，不自动合并、不覆盖上游分支。

Stable 状态、Plan B 和已经交付的 Windows/macOS 安装包沿用原记录；仅更新交接说明，不改变产品输入或重新执行构建。Mac 独立版本次实测正常；VST3/AU/ARA 的原生与自动化检查已通过，但不把这次授权写成所有宿主现场验收通过。面向上游的入口与边界见 [1.4.27 合并交接](docs/UPSTREAM_HANDOFF_1.4.27.md)。下文的暂停、未设 Stable、等待诊断等措辞保留为各阶段的历史记录，以本节和后续事实为当前状态。

## Mac 独立版现场诊断反馈（2026-11-08）

用户提供 `SpectrumTag-1.4.27-Diagnostics-20261108-041103.json`，随后明确确认：导出该报告时，隐藏原图后“已经能看到打印图形”。报告来自原生 arm64 Mac 独立版，非 VST3/AU/ARA。Print 已完成，音频与频谱探针均有变化，所请求、采用与绘制的频谱代次一致，原图已隐藏且无模拟预览覆盖。本次场景可记录为 **Mac 独立版 1.4.27 打印显示正常**。完整诊断摘要和证据边界见 [现场报告分析](docs/MAC_DIAGNOSTIC_1.4.27.md)。

这不是原先失败状态的日志，不能反推原始故障的唯一原因，也不能据此宣称 VST3/AU/ARA 全部通过现场验收。此次仅更新开发记录，不改变产品、不递增版本、不编译、不重装、不上传用户诊断文件；1.4.27 Stable 与已冻结 Plan B 保持原状，不访问或刷新备份。上游 PR 仍等待其余 Mac 格式的用户确认。

## 当前状态：1.4.27 Stable 与新 Plan B（2026-11-08）

用户明确要求“先设为stable 执行plan B”。将当前已交付的 **SpectrumTag 1.4.27** 记录为 Stable，涵盖现有 Windows Setup 与 macOS Universal PKG 中的实时、ARA 和独立版组件。现有成品、数字版本与构建验证直接继承，不重编、不重装、不重新打包。本次只更新本地状态记录和建立新的正式源码快照，不自动执行后续发布 Plan 或同步 GitHub。

源码：`D:\Codex\Workspaces\SpectrumTag-1.4.20-CrossPlatform`。新 Plan B 目标：`D:\备份文件\Vibe Coding\SpectrumTag\源代码\SpectrumTag-1.4.27-Stable`。备份复制完成情况、基本文件/字节与关键文件核对结果见 `D:\Codex\Temp\SpectrumTag-1.4.27\PLAN_B_COMPLETION.md`；完成报告发出后立即冻结，后续工作只引用记录，不再访问备份。以前的冻结备份不读取、不覆盖。版本身份、构建输入和回滚记录见 [1.4.27 Stable 记录](docs/STABLE_1.4.27.md)。

Stable 是用户指定的版本状态，不代表朋友的 Mac 现场问题已确认修复：完成 Print 后内部不显示、导出后 RX 可见的问题仍等待诊断 JSON；正常立体声自动化检查不能代替该工程实测。安装包的独立版安装位置选择改进已由用户明确推迟，留待后续版本。上游旧 PR 保持关闭，仍须用户亲自确认 Mac 没有问题后再授权新 PR。前一版交付为 1.4.26；此前明确的 Stable 为 Windows 独立版 1.4.24、Windows 插件 1.4.18，保留各自实际组件范围和历史标识。

## 1.4.27 打印显示诊断与选中尺寸修正

用户报告 Mac VST3、AU、ARA 完成 Print 后内部没有显示图形，但导出音频在 RX 可以看到；首次安装，不预设为旧版覆盖问题。另报告选中后的原图黄框比实际打印白框窄。

新增真实图片加载→Print→隐藏源图→停止播放时检查最终像素的回归测试。已复现一种相符情况：反相立体声、Noise Fill 25% 时导出通道确实改变，但内部图形亮度约 0.00085，近乎全黑。显示原先先求 L+R 平均再 FFT，导致相位抵消。1.4.27 改为每声道 FFT 后平均能量，覆盖实时、录音、ARA 和独立版共用频谱，音频打印算法不变。用户 Mac 原始素材尚未取得，不能仅据这个合成复现断言所有现场原因均已排除。

老的派生缓存保留正确的渲染音频，只重算一次旧显示频谱并保存新版缓存；不重跑 Print。选中时按当前频谱区域高度重新求视图缩放，让真实时间/频率覆盖范围恢复图片比例，再对齐原图，不改变音频或打印坐标。原图超出视窗时交给父组件裁切，不单独缩小原图。

用户明确指出朋友的素材不是反相立体声；因此反相回归仅记录为独立缺陷，不作为这次 Mac 现场故障的已确认原因。朋友尚未测试切换刻度是否恢复显示。按用户要求增加所有格式共用的“导出诊断 / Diagnostics”按钮，直接采集故障实例的版本、宿主、架构/Rosetta、参数、音频与 FFT 聚合差异、视图范围、栅格代次及采用条件、paint 次数和最近约一分钟的状态。报告在打开文件对话框前生成，不主动 tick、重绘或重新渲染；保存为用户选定位置的 JSON，默认桌面。不收集音频/图像内容、截图、路径、工程、源标识或账户信息，不自动上传。简要状态仅在窗口打开期间保留内存中的 120 条，关闭即释放。音乐素材的真正故障仍须结合朋友回传报告判断。

新增正常立体声（独立左右声道音调及噪声）回归与诊断报告检查；报告验证音频已改变、FFT 已改变、显示源是否匹配，且不触发新的 Print、FFT 或栅格刷新。ARA 共用界面也验证入口、格式标识与文件名隐私。Mac 构建、实际安装及诊断采集流程的结果另行记录，不将本地测试代替现场验收。

2026-11-08：Windows 全套处理器/ARA、独立版媒体和视图检查及两个 VST3 验证通过。Mac Apple Silicon/Intel 的完整原生回归、AU/VST3、音视频和诊断 JSON 检查通过，Universal PKG 实际安装通过；Windows Setup 在临时 runner 实际安装通过，三个安装后产品哈希与本地构建一致。证据写入 `D:\Codex\Temp\SpectrumTag-1.4.27`，索引见 [1.4.27 验证记录](docs/INSTALLERS_1.4.27.md)。朋友原始素材下的问题仍未确认修复，下一步以现场 JSON 为依据继续调查；不指定 Stable，不执行新 Plan B，不新建上游 PR。

## 1.4.26 安装包完成，上游 PR 等待用户验收

2026-11-08：Windows Setup、macOS Universal PKG 和中英文 TXT 安装说明已生成。两个 Mac 架构的处理器、ARA、音视频、AU/VST3 检查均已通过；PKG 实际安装后的编码器、AU 和 VST3 签名检查通过。Windows Setup 实际安装及内置媒体检查通过，三个安装后文件 SHA-256 与本地最终构建逐一相同。最终交付目录为 `D:\Codex\Outputs\SpectrumTag-1.4.26-Installers`，只有根目录双语 TXT、Win 安装程序和 Mac PKG；验证日志保存在 `D:\Codex\Temp\SpectrumTag-1.4.26`。完整证据与哈希见 [安装包验证记录](docs/INSTALLERS_1.4.26.md)。本轮没有覆盖用户本机已安装产品，没有新增或访问冻结 Plan B，没有自动设为 Stable。

Intel AU 发现失败来自临时 runner 的组件注册列表未刷新。仅在隔离 CI 中刷新安装目录时间戳并以 SIGKILL 重启用户的 AudioComponentRegistrar 后，两个 AU 都通过 auval；没有删除 AU 缓存或放宽系统安全策略。补跑复用精确源码对应的构建缓存和 ARM 成品，不重复完整构建。PKG 的 `lipo -verify_arch` 参数顺序错误单独修正后，只重打包并实装验证，两个架构的原生产物继续复用。

用户最新指令：新 PR 必须等待用户本人验证 Mac 版并明确确认没有问题后再提交。自动构建、AU/VST3 检查和 PKG 安装通过不代替该确认。旧 PR #2 保持关闭，本轮只更新个人 Fork 并交付安装包供用户验证；不新建上游 PR。
1.4.25 Windows Setup 已通过临时 Windows runner 的真实安装及内置编码器/原格式音视频检查，三个安装后文件与本地构建 SHA-256 相同。测试发布保留历史身份。Mac arm64 的处理器、ARA、普通媒体、首次视图、AU 和 VST3 均通过；延长视频检查定位到新版 FFmpeg 不再接受 `-vsync`。本轮最终安装版递增为 1.4.26，导出补帧时查询实际编码器能力，优先 `-fps_mode:v:0 passthrough`，保留旧编码器的 `-vsync 0` 路径。继续通过帧数、逐帧像素和音画同步断言验证，不能仅以文件导出成功代替。1.4.26 未指定 Stable，也不新建 Plan B。

Fork 已按用户要求改名为 `Ziqing-Gu/Spectrum-Tag-Private`，README 标题为 Spectrum Tag（私人）；可见性仍为公开 Fork。改名使旧 CMake 路径缓存失效，工作流缓存现按仓库名称隔离。Windows CI 继续使用 contents: read；草稿不可读的问题通过明确的测试预发布解决，没有提升令牌写权限。

1.4.26 首轮 Mac 补黑视频、首次视图、缺失媒体和空白 Print 检查通过；纯频谱视频传输超过 180 秒。POSIX FIFO 写满后不再每个数据块固定 sleep 5 ms，改为 poll 等待可写（每次最多 20 ms，以保持取消响应）。补充导出阶段/进度诊断，后续原生结果决定是否修复成功，不通过放宽测试超时掩盖问题。

Mac arm64 与 x86_64 的首次构建均在 `shared/Recording.cpp` 的 `juce::jmax<size_t>` 处失败：Clang 在重载决议中实例化了未实现的 `SIMDNativeOps<unsigned long>`。改为标准库标量 `std::max<size_t>`，计算语义保持不变。由于 1.4.24 独立版已交付，按版本规则将本轮产品递增为 1.4.25，安装包各组件统一版本；1.4.24 Stable 和冻结备份保持不动，不自动指定新 Stable。失败记录：Actions 42383473939。

README 按用户要求整理为中英双语产品入口，集中展示功能、版本状态、安装说明与构建入口；旧版交接细节保留在本总览、CHANGELOG 和逐版文档中。上游 PR #2 已于 2026-11-07 关闭，待用户亲自验证 Mac 并明确确认后再新建 PR。

2026-11-07 用户恢复 GitHub 同步及跨平台交付，明确要求 Windows 安装程序和 Mac PKG（含 AU、ARA 和独立版），附中英文 TXT 安装说明。继承此前独立版 Plan A 和已完成 Plan B，不访问冻结备份。当前源码补建未交付的 Windows 1.4.24 插件并验证两个 Mac 架构，只有实际通过的结果才写入完成记录。

按用户指令关闭上游旧 PR #2，保留其分支和历史；Mac 自动检查通过后仍须等待用户本人验收确认，才能提交新 PR。现有 Fork 主分支历史保持，不进行 force push。打包版本从 CMake 读取，避免沿用旧的 1.4.20 文件名；新功能检查纳入 Mac 工作流。安装说明保留未签名/未公证、Logic ARA/Rosetta、扫描及媒体迁移等真实限制。

## 1.4.24 Stable 与新 Plan B（2026-11-07）

用户在验证已交付 EXE 后明确要求“执行 Plan B 设为 Stable”。本次将 **Windows 独立版 1.4.24** 记录为 Stable；已有成品、版本号和验证直接复用，不重编、不重装。正式备份路径为 `D:\备份文件\Vibe Coding\SpectrumTag\源代码\SpectrumTag-1.4.24-Stable`，完成后冻结；后续阶段只引用本记录，不再访问该备份。旧备份保持不动，Mac/AU 和 GitHub 发布仍暂停。完整状态、成品身份、构建输入与完成记录入口见 [Stable 记录](docs/STABLE_1.4.24.md)。

## 1.4.24 纯频谱视频与导出优化（2026-11-07）

用户要求导出视频只保留频谱。画面改为满屏，取消标题、Tag 编号、刻度、时间和播放线；新频谱从最右边出现并向左滚动，不保留右侧未来区域。自动跟随 Tag 的比例、频率刻度和重叠优先级继续保留。导出默认目录改为系统桌面，仍可自行选择；新增独立进度弹窗，显示百分比/阶段，支持取消和关闭后的安全收尾。

导出缓存不变频率视图下的 FFT 颜色列，直接传送平台原生 RGB/BGR 字节，减少重复计算和整帧复制；CPU 编码线程按机器能力调整，上限八个，编码画质和用户选择的分辨率/帧率不降低。1080p 90 帧测试中，画面生成由 1032.3 ms 降到 370.0 ms，约 2.79 倍；这是局部画面生成测试，不能当作整段视频导出提速承诺。RTX 3070 的内置编码器探测被驱动拒绝，暂未启用硬件编码。

删除残影在“删除后不播放、不等下一次定时器，直接绘制”的测试中捕获；删除时立即使旧栅格任务失效，遮蔽过期时间段（含图片频段外的 FFT 泄漏），新快照到达后恢复真实频谱。缩短后的时间范围不能再读到保留 FFT 边界块的旧数据。相关画面、四格式视频、取消保护、音画同步、会话恢复、源格式导出和超范围 Print 回归通过；见 [1.4.24 记录](docs/WINDOWS_1.4.24.md)。仅交付 Windows 独立 EXE，既有 VST3/ARA 及冻结备份不动。

## 1.4.23 独立版频谱视频（2026-11-07）

新增“导出频谱视频”，画面使用真实处理后的频谱，声音保持正常速度；按 Tag 的实际时间、频段、图片比例自动平滑调整视图，逐渐显现打印图形。重叠时优先较早开始的 Tag，不修改工程中的 Speed 或打印参数。可选择 MP4/MOV/MKV/WebM、720p/1080p/1440p/4K、30/60 fps，记住选择；默认 MP4 1080p30。原有保留原画面、原音轨格式的导出入口继续保留。

未导入音频时也可在空白时间轴 Print，时长由 Tag 的位置扩展。Noise Fill 大于零生成声音，为零则静音；无源文件默认导出 48 kHz、24-bit 双声道 WAV。空白工程支持缓存恢复、删除、撤销重做及清空。导出逐帧送入编码器，不落地原始视频帧序列；可取消，成功前不覆盖既有目标文件。

Windows 已通过：四种视频格式和四档分辨率、30/60 fps、真实音轨同步、重叠优先级、比例和渐变、取消保护；实际界面空白 Print/撤销/清空恢复；完整独立版原格式音视频回归与首次 Load/全览回归。详细边界见 [1.4.23 记录](docs/WINDOWS_1.4.23.md)。本轮交付独立 EXE，不替换已安装的 1.4.22 VST3/ARA；不自动设 Stable，不执行 Plan B 或恢复暂停的 Mac/AU/GitHub 发布。

## 1.4.22 Windows 改进（2026-11-07）

本轮修复独立版首次 Load 把整首音频自动全览、导致同一个 Speed 对应不同显示时长的问题。用户确认：首次 Load 按当前 Speed，手动点击才全览。全览显示独立倍率和时长；常规 Speed 范围、统一时间换算保持不变。有效的上次视图，包括主动全览，仍然恢复。

独立版允许在原始零秒之前、原文件结尾之后的空白处 Print。音频、频谱、全览、导出范围随打印扩展；删除对应 Print 会撤回延长及其补偿噪声，原始音频不改写。负时间播放定位、撤销重做和缓存恢复一并处理。此能力不改变实时 VST3 的录音覆盖或 ARA 的宿主片段边界。

视频超范围导出补黑帧，原音画同步平移；视频前补时按帧对齐，音频增加相同的不足一帧的静音余量。无须补帧的导出继续复制原视频码流；补帧需要按原编码重新编码，常见 H.264/HEVC/VP9 使用无损设置。找不到恢复所需的缓存时静默留空，用户可直接重新加载；失败的空白恢复不覆盖旧会话。细节、格式边界及验证入口见 [1.4.22 Windows 记录](docs/WINDOWS_1.4.22.md)。

1.4.22 尚未由用户指定 Stable；不执行或刷新 Plan B，不恢复暂停的跨平台发布任务。

## 1.4.21 Windows 改进（2026-11-07）

用户暂停跨平台打包后，授权先交付 Windows 的实时 VST3、ARA VST3 和独立版；macOS/AU 打包仍暂停。1.4.21 不自动设为 Stable，不改动已冻结 Plan B。

- 静音/弱能量区域的 Noise Fill 改为目标频谱幅度合成，加三轮相位重建校正，减少亮区随机空洞。计算按四秒分块并保留校正所需的边缘数据，支持取消旧任务。新增 `fillVersion`：旧工程保留原算法，修改旧 Print 后使用新算法；删除仍回到原始音频，补偿不进入未录制间隙或图片时间范围之外。
- 增加按实际原始音频、有效覆盖、FFT 和 Print 参数进行 SHA-256 校验的派生缓存，保存处理后 PCM、原始/处理后频谱及波形缩略图。ARA 源指针或 UUID 改变不影响内容相同的缓存；音源内容改变、参数改变或文件损坏会重新计算。使用临时文件原子提交，音频线程不执行磁盘操作。
- 录音文件升级为 SPT5，保存浮点 mask 与算法版本，继续读取 SPT1–4。ARA 归档中的 mask 和算法信息仍是权威编辑数据；外部缓存丢失不改变工程编辑内容，只影响首次恢复速度。
- 首次 Load：减少音频读取调用次数；未编辑 ARA 时间线不重复计算干湿副本；频谱按 tile 预混声道后分析，长音频最多四个后台分析线程。保持原始频谱分析数值与 DAW 绝对时间映射。
- 独立版正常关闭时原子保存上次会话，恢复本地音频缓存、原格式信息、Print 编辑和控件状态；Clear 后不会在下次启动重新恢复已清空的音频。该行为只用于独立版，未用于 VST3/ARA。参考 QQ DeBreath 独立版的会话隔离原则。常见 PCM/FLAC 首次加载无需启动视频编解码器。

本轮验证结果和 Windows 安装覆盖位置记录在交付报告中。旧版本创建的工程第一次升级打开需要建立派生缓存；完成后保存工程，再次打开才可直接复用。新算法不会自动重写已有 Print 的声音。

## 1.4.20 跨平台构建进展（此前工作）

用户追加要求把当前源码同步到自己的 Fork，并生成 Windows 和 macOS 安装包，因此不再仅把 Mac 编译交给上游。新一轮产品版本统一为 **1.4.20**；历史 Stable 和已冻结 Plan B 保留不动。

- 用户 Fork 的 `main` 已在保留原有 README 提交历史的前提下合入现代源码；新增 `cross-platform-1.4.20` 开发分支。朋友的 PR #2 不自动合并，也不把本轮 Mac 工作写成此前的交付结果。
- Windows 实时/ARA VST3 和独立程序均已编译为 1.4.20；完整处理器回归、ARA 模拟宿主回归、两个 pluginval strictness 5 验证，以及独立版音视频加载/原格式导出回归通过。
- Mac 使用独立的 Apple Silicon 和 Intel 原生构建，再用 lipo 合并 VST3、AU 与独立 App。独立版从 App 内的对应架构 Helpers 目录启动 FFmpeg；封装依赖闭包并改写加载路径，不要求用户安装 Homebrew。
- 文件选择器使用平台合适的目录。Windows 保留现有 D 盘习惯，无 D 盘的机器回退用户数据目录；Mac 不使用 Windows 盘符，不向 App bundle 写入缓存。
- 首轮 Apple 编译发现测试 harness 的 CoreFoundation 头文件与 JUCE 的 Point 名称冲突，已调整系统头文件的包含顺序。这个问题位于测试入口，不是 Print DSP 的算法变化。
- Mac 回归进一步发现真实图像格式问题：JUCE 在 macOS 上把请求的 Native RGB 图像创建为 ARGB，频谱和 Print 模拟预览却按三字节 PixelRGB 连续写入。两处改用显式 SoftwareImageType，保持真实 RGB 存储；频率映射测试检查所有列的 RGB 与不透明度，不能通过放宽颜色误差掩盖像素错位。
- Windows 新安装器延续上游 AppId，提供三个组件选项并携带按需安装的微软 x64 运行库；Mac 安装器规划五个组件。双语说明区分 Cubase ARA 扩展与 Logic 首插槽/Rosetta 的入口，说明签名、扫描、字体、缓存与媒体限制。
- Mac 自动验证和 PKG 交付结果仍待本轮构建完成后补充；不能据 Windows 测试通过推定 Mac 或真实 DAW 工程已通过。

后文中“当前 Windows Stable”“Mac 交给上游”等内容为 2026-11-06 的历史交接基线；本节记录追加工作的范围与进度。

本次记录依据活动源码、逐版开发/交付记录和已提交的 [PR #2](https://github.com/sweetorange1/SpectrumTag/pull/2) 整理，不将讨论过但取消的方案写成当前功能，也不将自动测试通过写成完整宿主验收。末尾保留原作者 1.4.3 基线文档的完整原文供追溯。

- [当前成果和历史说明的区别](#current)
- [当前产品、架构与操作约定](#architecture)
- [需求调整的最终结论](#decisions)
- [按版本记录开发过程](#history)
- [关键问题与排查经验](#lessons)
- [构建、回归与验证边界](#validation)
- [Mac / AU 接手事项](#mac)
- [后续记录方式](#maintenance)
- [原作者历史基线](#legacy)

<a id="current"></a>
## 当前成果和历史说明的区别

本轮基于上游 `ac7d32470d1e7d5ae447e00fb01ebb8ee12631e6`（1.4.3）开发。用户先要求改进实时 VST3，待该路径稳定后再增加 ARA，最后实现现代独立版和视频音轨处理。

| 产品 | 当前用户指定 Stable | 当前入口与状态 |
| --- | --- | --- |
| Windows 实时 VST3 | 1.4.18 | `PluginProcessor.*` / `PluginEditor.*`，已构建、自动验证并交付 |
| Windows ARA VST3 | 1.4.18 | 共享 Processor/Editor + `ARA/ARAIntegration.*`，已构建、自动验证并交付 |
| Windows 独立程序 | 1.4.24 | `Standalone/ModernStandalone.cpp`，已交付含编码器的单文件 EXE；2026-11-07 用户指定 Stable |
| 修改后的 Mac VST3 / AU / ARA / 独立程序 | 未完成本轮平台验收 | 由上游维护者继续编译、封装及宿主验证 |

1.4.19 只增加独立版单文件封装及其界面版本显示，已交付的两款插件保持 1.4.18。Stable 是用户的明确决定，不是由编译成功自动推定。此次文档整理不改产品版本，不重新构建或安装；已有正式源码备份已完成并冻结，后续文档更新不回写它。

原作者文档中以下描述仅适用于旧基线：

- 旧独立版入口是 `StandaloneMain.cpp`，当前入口是 `ModernStandalone.cpp`，使用共享 Processor/Editor。
- 旧插件与独立版各有一份离线/实时算法；当前独立版使用共享录音编辑与 Print 核心。旧实现保留但不作为新独立版目标的入口。
- 当前 JUCE 固定为 8.0.15，网络功能默认关闭；旧文档中的 8.0.12 和强制启用更新/遥测不代表本轮构建。
- 当前菜单无 8192、无 Super Vision；Amplitude Ratio 上限为 33%，不再是旧文档的 1.5。
- 原图当前始终保持原始长宽比，不再采用旧文档中原图任意 stretchToFit 的交互约定。
- 旧安装器和打包脚本尚未为现代独立版与单独 ARA 重新验收，不应直接作为本轮最终发布入口。

<a id="architecture"></a>
## 当前产品、架构与操作约定

### 三种音频来源、播放与定位

| 行为 | 实时 VST3 | ARA | 独立版 |
| --- | --- | --- | --- |
| 音频来源 | DAW 实时输入；Record 记录输入 | Load 宿主提供的同轨 PlaybackRegions | Load 文件选择器 / 外部拖入音频或视频音轨 |
| Record / Load | Record、Clear Record；无 Load Audio | Load、Clear；无 Record | Load、Clear；无 Record |
| 未缓存时 Print | 播放中的即时音效；停止时按下立即结束，不留待播触发 | 先成功加载源再编辑 | 先成功加载文件再编辑 |
| 缓存后 Print | 编辑录制的音频 | 编辑宿主源的 modification | 编辑载入文件的音频副本 |
| 点击非 Print 区域 | 内部试听，不向 DAW 发送 seek | 请求 HostPlaybackController 定位 | 立即移动本地播放指针 |
| 点击已打印图形 | 只选中并恢复其参数和正常显示比例，不 seek | 同左 | 同左 |
| Clear | 返回未录音的实时模式，清频谱及内部试听，可撤销 | 清理已加载轨道范围的 Print/Noise Fill，保留宿主事件 | 清当前音频/编辑视图，保留源文件 |
| 窗口尺寸 | 75–250% 菜单及等比例拖拽 | 宿主外框实际尺寸决定布局，无内部百分比菜单/角标 | 独立窗口手动调整，无 ARA 式宿主尺寸同步 |

实时录音使用 DAW 绝对 sample/time，不把分离片段串起来。保存有效覆盖位，区分“录到的静音”和“未录到的空白”。重录只替换相同位置，保留其余片段。正常跟随 DAW 时，录到的位置播放录制/渲染结果，未录到的位置透传当前宿主输入。

内部试听与 DAW 脱离后，进入未录空白必须静音，不能把 DAW 另一个时刻的声音冒充该位置的声音；停止、宿主跳转或循环回绕取消旧试听偏移。停止时点击已录区域可选择下一次试听起点，点击未录区域取消该起点。全览只显示首次到末次录制的位置，保留中间空白；指针可在全览外，以边缘提示显示实际位置。

ARA 的同轨范围由宿主 region sequence 决定。选中任一已暴露的同轨事件可共用该轨加载视图，不按文件名猜测归属；宿主没有提供的事件无法被插件凭空读取。工程开始时间、源偏移、裁切、采样率、事件长度和重叠均分别处理。

### 数据与线程

| 模块 | 职责 |
| --- | --- |
| `PluginProcessor.*` | 参数、实时 DSP、宿主时钟、模式切换、状态读写及音频输出 |
| `PluginEditor.*` | 图像操作、Print 选择和参数恢复、控件、视图/宿主尺寸布局 |
| `shared/Recording.*` | 原始音频页、有效覆盖、不可变快照、Print 编辑集合、后台渲染、Undo/Redo |
| `shared/ImageMask.h` | 统一黑白转换及掩模生成 |
| `shared/FrequencyScale.h` / `TimeScale.h` | 显示与打印使用同一频率/时间转换 |
| `shared/ViewRenderer.h` / `PreviewWorker.h` / `PrintPreview.h` | 后台频谱绘制、合并请求、几何/预期功率预览与版本匹配 |
| `shared/Presentation.h` / `FineSlider.h` / `PlatformFonts.h` | 配色、双语说明、微调手势、平台字体 |
| `ARA/ARAIntegration.*` | 宿主 source / modification / region、加载、轨道上下文、播放计划和归档 |
| `Standalone/AudioFileSession.h` / `EmbeddedCodec.*` | 文件解码/导出和 Windows 编码器资源/缓存 |

原始音频保持不变；Print 参数和掩模构成独立编辑，每次替换从原音频及当前编辑集合得到结果。重复同位置 Print 保留同一编辑身份/噪声种子，不累积加深。删除同步移除该 Print 的补偿噪声。没有其它编辑影响时可复用原始音频页、频谱 tile 和峰值；有重叠时仍需保留其它编辑，只重新合成相关部分。

UI 手势期间仅更新轻量预览，音频继续使用上一个完成版本；松手/短暂空闲后提交最终渲染。连续修改合并为最新请求，并在拷贝、FFT 帧、噪声/峰值页等阶段检查取消。结果带音频快照与请求版本；旧结果不能覆盖新参数，删除后的残影也不能重新出现。实时/录音频谱在后台绘制，峰值缓存避免每次指针重绘都扫描整段音频。

ARA 源读取发生在宿主允许的模型线程；音频回调和 DSP worker 只使用不可变副本，不跨线程调用 host reader。源读失败不发布半个 buffer。Print 任务归文档/modification 所有，关闭编辑器不取消任务。离线输出不依赖 UI 或 Playing=true；归档恢复按 persistent ID / filtered restore 处理，不把恢复通知重新上报为新编辑。

### 当前参数与交互

| 控件/手势 | 当前约定 |
| --- | --- |
| FFT Size | 可选 1024 / 2048 / 4096；保留旧自动化槽位兼容，旧 8192 槽映射至 4096 |
| FFT scale | Linear / Mel (RX) / Bark / Log / Extended Log / insight；新实例默认 Mel (RX) |
| Range | 默认 20 kHz；20 / 24 / 22.05 / 48 kHz；实际不超过音源 Nyquist |
| Speed | 0.1–4.0，共用 1x = 60 逻辑像素/秒；旁边显示实际视窗秒数，包含 Fit 和视图缩放 |
| insight | 按用户截图参考比例匹配 Speed，允许后续手动调整；不自动改变 Range，不宣称是 Insight 官方通用默认 |
| Amplitude Ratio | 0–33%，有数值显示且影响原图/选中 Print 预览；上限由用户要求按截图估算 |
| B/W amount | 默认 0.5；自动 Otsu、自适应 Sauvola、手动阈值、Alpha silhouette 共用转换路径 |
| Invert | 掩模和原图预览一起反转，不只改变音频 |
| Noise Fill | 仅已录制/加载模式，0–100%；限图片时间/频率范围内，受边缘淡化约束，不填未录到的时间空隙 |
| 参数微调 | Shift 拖动/控件滚轮约为普通灵敏度的 1/10；Alt+左键或双击恢复默认 |
| 频谱滚轮 | 普通滚轮平移时间，短缓动；Ctrl 或 Shift + 滚轮横向缩放视图，不重新渲染已选 Print，无纵向缩放 |
| Speed 拉杆 | 选中 Print 时改变其时长，需要渲染；手势期间先预览，松手后提交 |
| 原图 | 一直保持原比例；单击选择/移动，双击加载；新图载入后自动显示，记忆上次图片 |
| Print 选择 | 第一次点击即恢复保存参数、频率刻度和正常显示比例；再次点击可复原滚轮造成的视觉拉伸 |
| Print 移动/缩放 | 选中后拖动位置，右下角等比例缩放；只在松手时真正渲染，整个手势是一个撤销操作 |
| 边框/删除 | 仅选中时显示边框；Delete 删除；提示为“Delete 键删除” / “Press Delete to Remove” |
| Undo/Redo | Ctrl+Z / Ctrl+Shift+Z；工具栏改为上/下一个 Print，缺少对应图形则禁用，不移动播放指针 |
| 原图显示/全览 | 眼睛按钮或频谱右键显示/隐藏原图；频谱右上角独立 Fit 图标 |
| 语言/配色 | 8 个记忆圆形配色；中文/EN 按钮显示当前语言，控件文字和悬停说明一起切换 |

Noise Fill 的轻量预览使用原始频谱功率、掩模、反转、保留音量、补偿底限与淡化估算；它不是最终波形的精确 FFT。相位、窗泄漏、随机纹理和声道相关性会产生差异，最终始终以真实渲染频谱替换模拟图。

<a id="decisions"></a>
## 需求调整的最终结论

这些变更在开发过程中多次讨论，接手时以右列为准，不要恢复早期已取消的操作：

| 早期提议/实现 | 最终决定与原因 |
| --- | --- |
| B/W amount 默认 0.7 | 用户发现部分图太黑，恢复 0.5 |
| 8192 更高 FFT | 用户认为效果不佳，从可见菜单移除；旧参数兼容槽保留 |
| 7–9 个配色 | 实施 8 色并记忆选择 |
| Shift/Ctrl 分别平移/纵向缩放，多次对调 | 普通滚轮平移；两种修饰键都横向视图缩放；取消纵向缩放 |
| 所有视图变化都要求打印图保持原比例 | 原图永远原比例；已打印频谱允许视图拉伸；点击 Print 恢复；Speed 拉杆才改变渲染时长 |
| 右键全览、全览从工程零点开始 | 右键显示/隐藏原图；独立 Fit 图标按录制范围全览；指针可超出范围 |
| RePrint / Load Audio 放在实时版 | 移除 RePrint 和实时 Load Audio；同位置 Print 替换；ARA/独立版用各自 Load |
| Undo/Redo 按钮先改图标 | 最终只保留快捷键，工具栏改上/下一个 Print |
| Range 中的 22 kHz | 用户纠正为 24 kHz，保留原自动化索引，不改写旧 Print 物理频段 |
| Super Vision 与 insight 各自定义不同 1x 时间 | 改为统一时间单位；之后取消 Super Vision 可选项；insight 匹配写入同一 Speed 标尺 |
| 独立 RX 预置 | 不新增 RX 专名预置；提供通用刻度，Mel 后加 (RX)，并设为默认 |
| ARA 也提供 75–250% 内部缩放 | 宿主外框未跟随，最终移除菜单和内部缩放角标，外框尺寸主导 |
| Print 期间没有反馈 | 原图隐藏后，在实际 Print 位置显示模拟图、进度与阶段，完成后替换 |
| 删除每次完整重渲染 | 保存原音频及其频谱/峰值，独立编辑删除走复用路径；有重叠时保留必要局部合成 |
| 独立版需 EXE 旁运行库文件夹 | 1.4.19 改为 EXE 内嵌压缩资源，首次媒体操作释放并校验缓存 |

<a id="history"></a>
## 按版本记录开发过程

以下记录实际开发顺序。每版均保留真实版本身份，不把后来的规则倒填成当时已实现。所有本轮产品工作集中在 Windows；Mac/AU 的旧基线能力不等于本轮修改已完成平台验收。

### 1.4.4：先处理打印清晰度、黑白转换和实时停播

- 起因：白噪声下图形与预处理黑白图差距明显，停止 DAW 后频谱仍移动，Invert 不更新预览；先暂停 ARA 设想，优先实时版。
- 改动：Hann WOLA overlap 从 4 增至 16，去掉额外 10 ms 时间低通和三 bin 增益模糊，修正 Nyquist 实部位置、启动 hop/归一化与末列保留。提高重叠并不意味着独立时频分辨率简单变成四倍。
- 转换：统一感知亮度/白底透明合成、Otsu、Sauvola、手动阈值和显式 Alpha silhouette；Invert 同时作用于预览与掩模。Alpha silhouette 对完全不透明图会成为实心块，不能替代内容黑白转换。
- 验证：相同输入白噪声与旧基线音频对照、多采样率/变 block/启动/停止/Nyquist/反转回归，实际插件 validator。早期分析 WAV 曾因测试 writer 追加造成多段 RIFF 拼接，已用重新生成的 final 数据替换，不能用错误试验文件评价效果。

### 1.4.5：录音编辑基础与界面体验

- 新增按宿主绝对时间捕获的 Record、录音范围全览、内部试听、可编辑 Print 和原音频保留；多段音频不按先后读入顺序拼接。
- 实施 8 色选择/记忆、UI 比例菜单及等比例窗口拖拽、缓存上次图片、显示/隐藏与新图自动显示；B/W 默认 0.5、Amplitude Ratio 0–33% 数值与预览。
- 可见 FFT 菜单移除 8192，独立索引适配保证三项菜单不重排四槽宿主参数。加入中英文悬停说明。
- 此阶段出现过实时 Load Audio / RePrint 与早期滚轮方案，后续按用户要求调整，不能从本阶段说明恢复这些旧控件。

### 1.4.6：编辑事务、时间线与录音完整性

- 明确原图比例、单击/双击、选中边框、Delete 与全局 Undo/Redo；原图可在更大范围移动，选中 Print 恢复保存参数。
- Speed/Amplitude/B/W/Invert/Noise Fill 拖动先给模拟预览，结束后合并渲染；音频版本切换交叉淡化。重复同位置 Print 使用相同身份和噪声种子，不叠加处理。
- 引入 Clear Record 和可撤销的清理；离线冻结队列满时等待，避免丢掉后半段/尾包。录到的区域替换播放，其他区域遵循当前模式规则。
- 实时版移除 Load Audio 和 RePrint；眼睛图标替代文字 Show Image；中文切换同时覆盖按钮/标签。早期未录区域内部试听透传方案在下一版进一步改为静音规则。

### 1.4.7：视图缩放与真实编辑分开，增加渲染反馈

- Ctrl/Shift+滚轮只缩放视图，不修改已打印音频；再次点 Print 恢复其保存 Speed/正常比例。只有拖动 Speed 拉杆改变其时长。
- 支持拖动选中 Print 位置，手势期间模拟、释放一次替换，保持 ID、频段映射和噪声种子；选择边框与模拟图采用同一几何。
- Print 后自动隐藏原图，原位显示模拟图/动画/进度，等待匹配音频与频谱都完成后再替换；预计算频率到掩模行的映射，跳过无变化编辑。
- 全览最终采用录音首尾范围，保留内部空白；指针允许越界。停止时点已录位置可预约下一次内部试听；播放中内部跳转到未录空白静音，停止后恢复 DAW 对齐。

### 1.4.8：Clear、卡顿、删除与 0% 问题

- Clear 立即退出录音模式、停止捕获/Print/内部试听、清实时 FFT 和下方全览，独立失效序号阻止旧绘制数据回填。尾包和撤销状态由 Processor/worker 保持，关窗不丢事务。
- 频谱后台栅格化、峰值缓存、录音显示请求合并，放慢滚轮并短缓动；录音结束自动 Fit，普通停播不强行打断用户视图。
- 右键改为显示/隐藏原图，Fit 成为频谱右上角图标。显示上限默认 20 kHz；选中 Print 修改 Range/频率刻度会重新映射掩模并渲染。
- 复用原音频页/频谱 tile 加速删除；重叠编辑仍局部合成。增加旧任务取消点，进度区分 Queued / Preparing audio / Rendering / Spectrum，小于 1% 用 `<1%`，不伪造进度。
- 按用户要求研究 QQ DeBreath / QQ EasyTool 的 Load/Record/ARA/AU 历史经验，参考生命周期和线程问题，不复制其不同许可的实现。

### 1.4.9：增加频率刻度与参考匹配

- 提供 Linear、Mel (RX)、Bark、Log、Extended Log、insight，曾加入 Super Vision；使用截图频率标记拟合对应曲线，不声称读取了第三方插件内部算法。
- insight 参考约 840×528 的截图与约 18 秒视窗；Super Vision 参考约 1186×560、用户确认的 5 秒视窗。窗口比例改变仍需重新匹配。
- 选中 Print 切换刻度会映射实际频段/掩模并替换渲染；普通选择已有 Print 只回忆其保存值，不重新应用预置重置。

### 1.4.10：统一时间标尺、Shift 微调、停止时 Print

- 用户要求所有模式共用时间衡量。统一 1x 为每秒 60 逻辑像素；参考预置改成写入共同 Speed 数值，不再重定义 1x。显示真实视窗秒数，包含 Fit/局部缩放。
- 四个连续参数 Shift 微调约 1/10；拖动中按/放 Shift 不跳值，一次手势一个宿主 begin/end 和 Undo 操作。
- 未录音且已停止时 Print 是即时无操作，不排队到下次播放；暂停音频回调的宿主也通过 UI/状态清理复位。已录音仍可停止时离线编辑。
- 状态版本 6 与 SPT4 识别统一单位，对旧参考模式 Speed 做一次迁移；SPT1–3 可读，不改变原音频/掩模/物理时间。旧 1.4.9 不识别新 SPT4；自动化点不由插件重写。

### 1.4.11：移除 Super Vision，修复细调预览

- 移除 Super Vision 菜单和时间匹配，保留旧参数槽并映射到 Log，避免错解旧工程。新实例默认 Mel (RX)，旧工程保留已有选择。
- 区分两类“微调不变”：二值阈值在没有像素跨阈值时本来就不变；而旧 worker 连续丢弃所有落后于最新请求的已完成图，会导致拖动时完全不刷新。
- 修复为同源/同转换模式下按递增 generation 接收中间结果；最新结果仍决定最终渲染，不接收换图/换模式前的无关图像。

### 1.4.12：Noise Fill 的选中 Print 预览

- 原模拟图使用常量背景且没读取补偿量，导致调整已打印图形的 Noise Fill 看不到正确变化。
- 预览改为基于未打印频谱估算输出功率，使用与 DSP 对应的补偿底限 `-66 + 42 * amount` dB、掩模、Invert、Amplitude、向内频率淡化和时间淡化。
- 降低补偿从原音频估算，不残留旧补偿；保留空隙及其它重叠编辑。最终音频仍在手势结束后计算，模拟结果不宣称精确等于最终 STFT。

### 1.4.13：insight 保留 Range

- 用户要求选择 insight 不再改变显示上限。只保留频率刻度与共同 Speed 的参考匹配，Range 由用户保持。
- 选中 Print 仍按保留的 Range 重建频段映射；旧 Print 选择和工程恢复仍还原其保存 Range。
- 此版曾被用户指定 Stable 并完成独立源码备份，历史记录保留。

### 1.4.14：纠正 22 / 24 kHz

- 原菜单中的 22 kHz 是需求口误，改为 24 kHz。默认仍 20 kHz，原菜单槽位不移动，Nyquist 上限仍生效。
- 已保存的 22 kHz Print 物理范围/掩模不改写；回忆菜单时以最近选项表示历史显示上限，需要显式编辑才重新映射音频。
- 此版也曾被用户指定 Stable，后续 ARA 开发从实时稳定成果继续。

### 1.4.15：首次独立 ARA 实现

- 新增独立 `SpectrumTagARA` target、插件身份与 ARA factory/archive ID，不替换实时版。Load 读取宿主音频，点击非 Print 区域通过 HostPlaybackController 请求定位。
- 按 Source/Modification/PlaybackRegion 保存数据与处理结果，保留源偏移、工程位置、重叠和混合采样率；宿主允许的模型线程读取，文档拥有处理任务，归档包含打印掩模/参数。
- 避免固定 32 源淘汰；无 UI 离线输出、读失败重试、filtered archive restore 与 44 源/22 renderer 等模型测试进入回归。
- 提前加入跨平台字体和有限窗口挂接同步；Mac/AU 仅提供源码入口，没有因此宣布已通过平台测试。弹性时间拉伸尚不支持。

### 1.4.16：共享预览/缓存问题与 ARA 外框适配

- 选中 Print 调 B/W 时从完成的转换生成轻量掩模，修复拖动无预览；删除时清旧音频快照的 raster、孤立模拟图/选择，像素回归与新开窗口结果对比。
- 修复 Fit 隐藏缩放因子叠加到 Speed，使视窗变成数百/数千秒；未选中改 Speed 回到共同时间单位。增加大块拷贝阶段取消点，连续修改只完成最新请求。
- ARA 增加 Clear 及撤销；读取不等于清编辑，Clear 后不会因普通选择通知立即重载。移除内层缩放角标，用宿主实际尺寸布局。
- ARA 类别改为 OnlyARA、不带 Fx，但宿主普通插入菜单实际是否隐藏仍未验收；不能只凭扫描元数据声称成功。
- ARA 复用原音频/时间线页和频谱 tile，局部合成；源替换、采样率/事件位置变化使相关缓存失效，Delete 无编辑剩余时可直接复用原数据。

### 1.4.17：首次点击、同轨缓存与现代独立版

- 点击 Print 不再移动指针；参数回忆作为一个 UI 操作，避免 APVTS 通知回调里读到旧缓存导致要点两次才恢复频率刻度。
- 修复视图缩放锚点/先后顺序和完全脱离音频范围的恢复，保留用户有意平移与部分相交视图；全览外的 DAW 指针仍被允许。
- 撤销/重做工具栏换成上一/下一 Print，键盘快捷键保留。ARA 新 modification 明确默认 Mel，即使宿主重用 Linear wrapper；显式选择与归档参数按上下文回忆。
- ARA 按真实 region sequence 扩展同轨事件，切同轨事件复用快照，无需重新读取/组合；移除 75–250% 菜单，只有宿主框架尺寸决定布局。
- 独立版改为共享 Processor/Editor，加入 Load/外部音频拖入、Play/Pause/Stop、设备选择、局部定位和原格式另存导出；旧 Standalone 源码保留但退出当前入口。

### 1.4.18：选中 Print 等比例缩放与视频音轨

- 三个版本选中 Print 后可拖右下角等比例缩放，固定左上时间/频率锚点。手势开始准备模拟图，边框随模拟图变化，松手替换同一编辑，一个 Undo 恢复整个手势，不 seek。
- 独立版增加 MP4/M4V/MOV/MKV/WebM/AVI 音轨载入/拖入；处理第一条音轨，保留时间戳空隙/前导延迟，无音轨时拒绝并保留原会话。
- 导出可选保留原容器的视频或仅音频；视频/其他流尽可能 stream-copy，只重编码第一条编辑音轨，避免 shortest 截短视频。音频保持源编码族和适合的容器；不承诺原码率、全部 metadata 或压缩数据逐字节不变。
- 这不是视频画面播放器。源文件不覆盖，限制为单声道/立体声、15 分钟/64M sample 的载入范围；不支持的 encoder 明确报错而不悄悄换格式。

### 1.4.19：独立版单文件交付

- 用户不希望 EXE 旁依赖一个运行库目录。将既有 FFmpeg、GPL 文本及 notice 压成资源嵌入 Windows PE，首次媒体操作才解压至版本/hash 缓存。
- 缓存逐文件 SHA-256 验证，唯一临时文件写完验证后原子替换，进程间锁处理并发；有效缓存复用，缺失/损坏可修复；解压发生在文件 worker，不进音频回调，不在线安装。
- 隔离单 EXE、冷启动/热复用/损坏修复/并发测试与完整媒体回归通过。独立版 1.4.19 已交付，两款插件继续使用既有 1.4.18 成品。
- 当前 Windows 默认媒体/编码器缓存使用 D 盘；通用发行仍需处理无 D 盘机器的路径策略。非 Windows 内嵌编码器尚未实现，交由后续平台工作。

### Stable、备份与本次 Overview 补全

用户随后指定插件 1.4.18 + 独立版 1.4.19 为 Stable，完成一份包含源代码、资源、构建配置及内部历史的正式源码备份，完成后冻结。向上游发起 PR #2，首次代码提交为 `31d0ed8cef535f32676fcc3a41d6a3031e25ca0c`；仅请求原作者审阅合并并继续 Mac/AU，不自动合并。

按原作者“各项目都把开发过程写进 overview”的交接习惯，本次将原先分散的当前实现/迭代过程集中到此文件，并保留原作者历史原文。公开记录不上传用户素材、个人交付路径或内部项目日志；本次纯文档提交不升级产品版本，不重新构建、不刷新冻结备份。

<a id="lessons"></a>
## 关键问题与排查经验

| 现象 | 诊断/修正要点 | 后续必须守住的边界 |
| --- | --- | --- |
| 冻结后录音不全或频谱只显示部分 | 区分真实采样缺失与视图/频谱缓存缺失；离线队列等待和尾包保留；停止捕获强制最后一次发布/Fit | 不能因频谱看不到就推断音频丢失，更不能把后段样本提前拼接 |
| 解冻后 Record 仍亮着 | 覆盖 suspend、offline→realtime、prepare/release 和停播生命周期的 disarm | UI 按钮和捕获状态必须同源，不能只改按钮颜色 |
| Clear 后仍是录音模式、旧频谱回来 | 同时使音频模式、FFT 历史、raster 请求和内部试听失效，保留可撤销事务 | 音频/清理任务不能依赖 Editor 是否仍存在 |
| 修改参数卡顿或连续修改使进度停在 0% | UI 不做全长重算；合并最终请求、任务分阶段取消、进度区分准备/渲染/频谱 | 模拟图不是已完成音频；大图/长区域/重叠/Noise Fill 仍有真实计算成本 |
| Delete 后图还在却无法选中 | 编辑已经删除但旧 raster/ghost 仍被发布；以 snapshot/request 版本作接受条件 | 删除列表、选择、模拟图、真实频谱不能各自持有不一致的版本 |
| 删除依然很慢 | 原音频并非没保存；缺少原频谱/峰值复用也会重做显示计算 | 无重叠可走原始缓存快路径，有重叠不可抹掉其他 Print |
| 黑白强度微调原图不变 | 二值阈值本身可能没有像素跨界；另外存在中间转换结果被全丢弃的问题 | 明确算法离散性与刷新 bug 的区别，不用灰度淡化假装真正阈值改变 |
| 选中 Print 改 Noise Fill 没变化 | 原模拟图未依据原始功率/补偿量 | 使用原始谱估算，降低参数也能回退，不累积旧噪声 |
| 第一次点 Print 频率刻度不对 | 通知回调读取了 APVTS 尚未更新的缓存值 | 回忆参数作为完整操作；点击不应额外触发 seek |
| Mel / Speed 显示视窗特别长、缩放后空白 | 频率模式不应暗改时间单位；Fit 的额外 view zoom、锚点与视窗恢复必须分开 | Fit 长视窗可以合理；0.1x 也可能很长。不能简单把所有长时窗判为 bug |
| ARA 新加扩展不是默认 Mel | wrapper 状态可被宿主重用；新的 modification 与已有归档恢复须区别 | 新上下文默认 Mel，已保存的明确选择优先 |
| ARA 点同轨不同事件反复重载 | 编辑器选择与 renderer 归属混淆；缺少 track context 缓存 | 用显式选择/region sequence；成员变化使缓存失效，同轨未变复用 |
| ARA 内部缩放与外框不一致 | 宿主可能拒绝/延迟内部 resize 请求 | 以实际宿主 allocation 布局；不循环强推外框尺寸 |
| OnlyARA 仍出现在普通插入 | 元数据与宿主实际分类/扫描 UI 不等价 | 已移除 Fx 并保留身份，但真实隐藏须各宿主确认 |

其它插件历史研究中有几条反例已转为本项目约束：不要在 DSP worker 遍历 ARA host model；不要用固定 32-source 淘汰造成多轨静音；不要用 renderer 归属覆盖显式编辑器选择；不要让关闭窗口取消文档任务；Load/Reload/Clear 必须是不同事务；filtered archive restore 不得清掉无关源，宿主 Undo 恢复不能再次上报成新编辑。这些是行为经验借鉴，不代表复制其它项目源码或其许可。

<a id="validation"></a>
## 构建、回归与验证边界

### 当前构建入口

需要 C++17、CMake 3.22+，Windows 使用 Visual Studio 2022 C++ 工具。JUCE 8.0.15；启用 ARA 时获取固定 SDK commit `0005c532bb03eb210e7bb490329fc6d7ba1792c7` 的 ARA_API / ARA_Library，或指定本地 SDK。SDK/编码器二进制不放入本 PR。

```powershell
.\build-windows.ps1 -BuildDir D:\Build\SpectrumTag
.\build-ara-windows.ps1 -BuildDir D:\Build\SpectrumTagARA -AraSdkDir D:\Dependencies\ARA_SDK
.\build-standalone-windows.ps1 -BuildDir D:\Build\SpectrumTagStandalone -RuntimeDir D:\Dependencies\SpectrumTagRuntime
```

`RuntimeDir` 包含 `ffmpeg.exe`、`COPYING.GPLv3`、`FFmpeg-NOTICE.txt`。`SPECTRUMTAG_BUILD_TESTS` 构建 Verify 目标；`SPECTRUMTAG_BUILD_ARA` 开启独立 ARA；`SPECTRUMTAG_ENABLE_NETWORK` 默认 OFF。实际测试命令、目标路径与已有成品 SHA-256 见 [Windows handoff](docs/WINDOWS_HANDOFF.md)。

### 已有验证，不因写文档重复执行

| 范围 | 实际已有验证 |
| --- | --- |
| 实时 DSP | 44.1/48/96 kHz、变化 block、unity/fade、Nyquist、启动/停止、反转与延迟；同输入白噪声和旧版比较 |
| Record/Print/编辑 | 多段绝对时间、覆盖与空隙、同位置替换、噪声边界、Delete/重叠、Undo/Redo、参数细调与拖动预览、取消/缓存、选中/移动/缩放 |
| ARA | 44 源 / 22 renderer 等多源测试、源偏移/混合采样率/重叠、读失败、filtered archive、无 UI 输出、轨道成员变化和同轨快照复用、窗口 allocation 模拟 |
| 插件 validator | 两款 1.4.18 实际 VST3 均通过 pluginval 1.0.4 strictness 5，seed 20261106；GUI 项跳过 |
| 独立版媒体 | WAV/FLAC/MP3/OGG/M4A-AAC/AIFF/AAC/WMA-wmav2/Opus 九种音频，六种视频容器；画面 packet/frame/timestamp、额外音轨保留、延迟音频对齐、无音轨失败保留旧会话 |
| 单 EXE 资源 | 1.4.19 无邻接运行库、无编码器覆盖变量情况下，冷解压/热复用/损坏修复/并发；完整独立版回归 |

自动化入口保留在 `tests/Verify.cpp`、`tests/ARAVerify.cpp`、各 `*Checks.h` 及独立程序 `--self-test` / `--codec-check`。历史白噪声对照脚本在 `tests/make_baseline.py`、`make_fixture.py`、`analyse.py`；其中 Windows 字体/路径和历史 8192 对照用途不能当成当前 UI 功能。

已知局限：mono/stereo；8–384 kHz；每源最多 64M samples，加载组合视图最多 15 分钟/64M samples；不支持 ARA 弹性时间拉伸；长源首次读取仍可能等待。ALAC/PCM float 路径已实现但未分别完成上述媒体用例；原编码族导出不承诺原压缩位流/码率/全部标签。实际 Cubase 操作、音频设备听测、OnlyARA 隐藏、宿主原生 Undo 和 Mac/AU 验收不能由自动回归代替。

状态兼容需覆盖 SPT1–4 读入和统一时间标尺迁移，移除选项保留参数索引。当前实例 Undo 历史与已序列化工程状态是不同概念；内部 Undo 成功不等于宿主原生 Undo 分组已验收。

<a id="mac"></a>
## Mac / AU 接手事项

完整平台交接见 [MAC_AU_HANDOFF.md](docs/MAC_AU_HANDOFF.md)。以下事项在本轮没有完成：

1. **目标与身份**：实时 `SpectrumTag_VST3/AU` 使用 `Iisa/SpTg`；ARA `SpectrumTagARA_VST3/AU` 使用 `Iisa/SpAr`、独立 factory/archive ID 和 AU export prefix。不要为复用安装器把两个身份合并。配置中的 arm64+x86_64/macOS 11 不是实际 Universal 2 验证结果。
2. **编码器**：Windows PE 内嵌资源不可用于 Mac。非 Windows `resolveEncoder()` 当前除显式 SPECTRUMTAG_FFMPEG 覆盖外会报无内置编码器；需 app bundle helper、架构、执行权限、签名和 notice/对应许可来源处理。
3. **路径和测试**：检查媒体临时目录、选择器默认路径、录音缓存、测试 `_putenv_s` 和 UI 初始化/链接，改为对应平台的可写位置。不要假定 macOS 有 D:，也不要依赖 app bundle 可写。
4. **字体/窗口**：中文优先 PingFang SC，其次 Hiragino Sans GB/Heiti SC/Songti SC，保留 UTF-8 和原嵌入标题字体；不复制 Windows 系统字体。验证 Retina、初次/再次打开、停靠/浮动、宿主尺寸改变和中文裁切，保持有上限的尺寸同步。
5. **宿主行为**：分开验证 AU 扫描注册、ARA document/source 生命周期、同轨选择、源变更、播放/离线无 UI 输出、保存重开/filtered restore/native Undo，以及 Apple Silicon 原生和需支持的 Rosetta 情形。不要用禁用 SIP 代替兼容修复。
6. **安装发布**：旧 installer 未适配新目标；检查各 bundle 的实际版本、架构、动态依赖、签名/notarization 后再发布。Windows 结果不构成 Mac 成品完成证据。

<a id="maintenance"></a>
## 后续记录方式

继续开发时直接维护本文件：每个已交付数字版本写明“触发问题/需求、原因或决策、最终实现、验证和仍存边界”；被用户取消或被后续版本替代的行为标注历史，不删去真实过程。关键 bug 写清为何修而不只写“优化”。未知原因写未知，不猜测已根治。

README 面向使用者，CHANGELOG 提供版本摘要，本文件集中开发过程，平台 handoff 提供详细接手命令。产品输入发生变化再升数字版本；已有产品标记 Stable 或纯文档整理不触发重编。备份和发布记录继承已完成状态，冻结备份不因后续文档更新而重做。

<a id="legacy"></a>
## 原作者历史基线（1.4.3 及以前，原文保留）

下面内容是上游原始开发者文档，完整保留以便理解早期设计及已经发生的修复。它不覆盖本文件前面的当前约定；其中版本、网络开关、旧独立版入口、参数、图像比例和安装器说明均应按上方当前实现判断。

<details>
<summary>展开原作者历史文档原文</summary>

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
└── build_macos_installer.sh       # macOS pkg/dmg 打包（Standalone.app 主 + VST3/AU 可选组件）
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

**macOS 默认产出双架构 universal 二进制**（`CMakeLists.txt` 在 `project()` 前把 `CMAKE_OSX_ARCHITECTURES` 设为 `arm64;x86_64`，并把 `CMAKE_OSX_DEPLOYMENT_TARGET` 设为 11.0，arm64 的最低要求）。Standalone / VST3 / AU 三个产物都是 fat binary，Intel 与 Apple Silicon 均可加载。需要单架构时显式覆盖：

```bash
-DCMAKE_OSX_ARCHITECTURES=arm64     # 仅 Apple Silicon
-DCMAKE_OSX_ARCHITECTURES=x86_64    # 仅 Intel
```

> 用 `lipo -archs <binary>` 校验；`build_macos_installer.sh` 在 Step 2/7 会打印各产物架构，非双架构时告警。

产物：

- `cmake-build-release/SpectrumTagStandalone_artefacts/Release/SpectrumTag.exe`（macOS：`SpectrumTag.app`）
- `cmake-build-release/SpectrumTag_artefacts/Release/VST3/SpectrumTag.vst3`

> **注意**：`CMakeLists.txt` 中 `ICON_BIG "UI.png"` 要求仓库根目录存在 `UI.png`（JUCE 在 configure 时转换出 `icon.ico`，供 `SpectrumTagStandalone` 的 exe 使用）。该文件已在版本控制中，当前设计沿用 Print 按钮视觉：深色圆角卡片 + 居中的白色圆点 + 圆内黑色 `Spectrum` / `Tag` 两行字（字体 BasementGrotesque Black，与 UI 一致）。换图标时直接替换根目录 `UI.png` 并**删除 build 目录重新 configure** 即可；仅重新编译不会刷新 Explorer 里已缓存的旧图标。

### 3.4 打包

- Windows：`build_installer.bat` 自动探测 VST3 bundle 与 `SpectrumTag.exe`，通过 `-DSTANDALONE_EXE=` 传给 `SpectrumTag_installer.iss`；有 exe 时安装包出现「VST3 plug-in / standalone application」两个组件，独立程序装到 `{autopf}\iisaacbeats.cn\SpectrumTag\`。未找到 exe 只告警并退化为纯插件包。
- macOS：`build_macos_installer.sh` 打**组件式**安装包（`pkgbuild` 打 component → `productbuild` 合成带"自定义安装"页的 product pkg → 包成 dmg），形态与 Windows 一致：
  - `SpectrumTag (Standalone Application)`：**主组件，必装**（`enabled="false" selected="true"`，勾选框置灰）→ `/Applications/SpectrumTag.app`。产物缺失直接报错退出。
  - `SpectrumTag Plug-ins (VST3 + AU)`：**可选组件**，默认勾选，用户可取消 → 系统插件目录。VST3 / AU 产物缺失只告警并跳过该组件（两个都没有时自动退化为纯 Standalone 包）。
  - 常用参数：`--version` / `--no-sign` / `--identity "Developer ID Application: ..."` / `--skip-plugins` / `--keep-work`。
  - Standalone 组件附 `preinstall`（关闭正在运行的旧版本，对应 Windows 的 `CloseApplications=force`）与 `postinstall`（清 `com.apple.quarantine`，避免 ad-hoc 签名的 app 首次启动被 Gatekeeper 拦截）。

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
| `Standalone/StandaloneWindow.cpp` | 主组件：拖放分发、参数联动、Print 调度与 WAV 导出、静态频谱视图实现（含滚轮平移/缩放、底部时间轴、`ExportResultOverlay` 自定义导出弹窗） |
| `Standalone/OfflineRenderer.cpp` | 离线 STFT/OLA 合成；`computeBinGainsForCol` 负责把 mask 行映射到 bin 增益 |
| `Standalone/SharedUI.cpp` | `ImageBoxComponent`（拖动/缩放/二值化/生成 mask，界面显示与 mask 拉伸一致）、`StandaloneLookAndFeel`、圆形 Print 按钮、`FlatButton` 自绘按钮、频率映射 `FreqMap` |
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
11. **STFT/OLA 的真实群延迟是 `N - 1`（不是 `N - hop`）**：`processFrame` 需 `N/hop` 帧（共 N 个样本）预热后才放出首个输出，对应 `input[0]`，据此可得群延迟 `N - 1`。离线渲染的 dry 对齐、以及插件的 `setLatencySamples` / `dryDelaySamples` 都必须用 `N - 1`。此前误用 `N - hop`，导致独立程序导出整体延迟 0.07s（无宿主补偿）与印章区干/湿错位 `hop - 1` 样本；v1.4.0 已统一修正。
12. **界面图片显示必须与 mask 生成一致**：`generateMask` 把整张二值图**拉伸**映射到 `rows × cols` 网格；因此 `ImageBoxComponent::paint` 也应用 `RectanglePlacement::stretchToFit` 拉伸铺满，而非按宽高比居中留白，否则左右留白大的图片会出现「所见非所得」。

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


</details>
