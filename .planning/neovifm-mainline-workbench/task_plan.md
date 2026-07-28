# NeoVifm 主线能力继承与任务工作台计划

## 目标

把当前已经可用的 OpenTUI 双栏界面推进为 NeoVifm 的正式主线：优先继承 Vifm/ViATc 的 Vim 快捷键与成熟文件管理语义，在同一主线中加入压缩包/SSH 目录化、F3 富媒体预览，以及不会阻塞界面的文件任务队列与可见历史。

用户已授权执行。Phase 0 已完成验收并独立提交；当前继续按验收门槛推进 Phase 1 快捷键兼容，并把双栏快速预览、目录优先排序、信息层视觉优化和 Vifm 文件打开关联作为紧随其后的主线切片。未完成阶段仍保持 pending。

## 主线定义

- **默认产品入口：** OpenTUI。新用户默认进入 TypeScript/Bun/OpenTUI/SolidJS 界面。
- **主线架构：** Hybrid。这里不是“实验客户端”的意思，而是同一个正式产品由两部分组成：Vifm C core 继续拥有文件系统、Vim 语义、文件操作、undo、后台任务、挂载和扩展能力；OpenTUI client 负责默认交互与渲染；两者通过版本化、不可变 DTO/事件边界协作。
- **经典 ncurses：** 保留为兼容入口、回归基准和成熟能力来源，不再作为默认新界面，也不得被机械删除。
- **实现原则：** 优先桥接 Vifm 已有 `engine/keys`、`ops`、`undo`、`background`、`fops_*`、`filetype/fileviewer`、`quickview/vcache` 与 FUSE mount；不在 TypeScript 或 `src/neovifm/` 平行重写同一套语义。
- **参考代码：** ViATc、Total Commander、Yazi 和本地 preview repo 用于建立行为矩阵与交互参考。除非完成许可证和来源审查，不直接复制第三方实现。
- **暂缓事项：** 安装器、系统包管理、公开 release、远程原生协议、插件 SDK、agent session、全仓品牌重命名。

## 优先级

1. 完成 Vifm/ViATc 快捷键兼容矩阵的当前切片。
2. 落地 Space 对面 pane 快速预览、目录优先排序、底栏/滚动条/tab/header/权限与时间信息层优化。
3. 复用 Vifm `filetype/filextype/fileviewer` 建立统一打开 resolver，macOS 无显式关联时交给系统 `open`。
4. 复用 Vifm 文件操作/undo/background，继续补齐后台任务队列和任务中心弹窗。
5. 先让 ZIP/常见 archive 像目录一样打开，再接 SSH/sshfs 目录。
6. 以 F3 全屏查看和 Space 对面 pane 快速查看两种入口，共享图片、PDF、Markdown、代码和媒体 preview pipeline。
7. 最后做跨能力联调、回归和各阶段独立提交；安装发布继续后置。

## 阶段

### Phase 0：现有工作树验收与独立提交

- [x] 冻结当前 tab、鼠标选择、路径切换/复制、protocol v3 additive 字段的范围，不混入本计划的新功能。
- [x] 重新审阅完整 diff，确认无意外生成物、无硬编码秘密、无第三方代码混入。
- [x] 重新运行 focused C、TUI unit/coverage/typecheck/audit、真实 C integration、正式 PTY、串行 `make check`、schema 校验和 `git diff --check`。
- [x] 重点复验 60/80/100/160 列、ASCII fallback、`gt/gT`/计数、tab 新建/激活/关闭、鼠标批选、路径复制与 action 期间切 tab。
- [x] 验收全部通过后，只为这批现有改动创建一个独立 conventional commit；提交前不夹带 Phase 1 之后的代码。
- **状态：** completed，已创建独立提交。

### Phase 1：Vifm/ViATc 快捷键与动作兼容矩阵

- [x] 以 `src/modes/normal.c`、`src/engine/keys.c`、Vifm 文档、`/Users/rex/soft/_refs/neovifm/viatc` 为来源，建立完整行为矩阵：按键序列、计数、模式、目标动作、当前支持度、冲突、验收用例和来源。
- [ ] 第一批固定主线交互层：`F3--F10` + `hjkl`、`gg/G`、计数、`Ctrl-W`、`gt/gT`、搜索/next/previous、marks、registers、selection/visual、历史、pane 切换、刷新、打开/返回和退出语义。
- [ ] 第二批覆盖 Total Commander/ViATc 高频动作：同扩展名选择、反选、目录历史/常用目录、复制名称/完整路径、左右 pane 同步/交换、在另一 pane 或新 tab 打开。
- [ ] 第三批单列高级动作：批量重命名、目录比较/同步、内容比较、快速搜索和命令浏览；逐项判断复用 Vifm、实现 adapter 或明确 defer。
- [ ] 将 Vifm 的 key engine/command engine 作为语义来源；OpenTUI 只做按键事件标准化和 UI-local overlay 操作，不维护第二套独立 Vim 状态机。
- [ ] 为每个已承诺按键先写 RED 单测，再补 C/协议/客户端最小实现；真实 PTY 必须覆盖普通键、组合键、多键前缀、计数、Esc、窄终端和宿主抢占 Fn 键时的鼠标替代入口。
- [ ] 矩阵中的每一项必须标为 supported、mapped、conflict、deferred 或 not-applicable，不允许静默缺失。
- **阶段验收：** 日常快捷键闭环在 unit、integration 和正式 PTY 中一致；经典 Vifm 行为不回归；形成独立提交。
- **状态：** in_progress。

### Phase 1A：双栏快速预览、排序与信息层优化

- [x] 先写交互契约和 RED tests，将 `Space` 从当前与 `Tab` 相同的 `focus-next` 映射中拆出；`Tab` 只负责切换活动 pane，`Space` 切换“在对面 pane 快速预览当前项”。
- [x] Space 快速预览不得修改对面 pane 的 cwd、tab、selection、history 或 snapshot：源 pane 保持 active，移动源 cursor 时用 generation cancellation 更新预览；再次按 Space 或 Esc 恢复对面 pane 原视图，Tab 切 pane 前先关闭临时预览。
- [ ] 双栏宽度不足或退化为单 pane 时，Space 使用与 F3 相同的全屏 viewer fallback；目录显示只读目录摘要/列表，普通文件走统一 preview resolver，archive/remote 不因预览而触发 mount/enter。
- [x] 将目录优先设为所有排序字段的稳定一级分组：parent entry（若存在）固定最前，真实目录在文件之前，升/降序只改变各组内部顺序；symlink 不在客户端追踪，只有 core 发布 enterable-directory capability 时才进入目录组。
- [x] 把底部固定为恰好三行：status line、单行 divider、`F3--F10` function bar；删除空白 spacer，保证 60/80/100/160 列都不遮挡状态和任务入口。
- [x] 将滚动条占宽限制为一个 terminal cell，缩短/变细 thumb；Nerd/Unicode 模式用圆角端帽字形，ASCII/低能力模式使用直线 glyph。若 OpenTUI 原生 scrollbar 不能稳定满足，封装局部 scrollbar adapter，不修改列表滚动语义。
- [x] 将 active pane 标记移到 pane header 右上角并预留固定宽度；Nerd/Unicode 显示圆点，ASCII 显示 `*`，切换 active 不得造成 tab/header 抖动。
- [x] 为当前排序字段增加低对比度语义背景和固定宽度端帽；Nerd/Unicode 模式以字形模拟圆角，ASCII 模式使用括号/平直背景，不能因激活态改变列宽。
- [x] 强化新建 tab 的 `+`：保留固定点击区域、提高前景/背景对比、提供 normal/hover/pressed/disabled 状态；Unicode 与 ASCII 均必须一眼可辨且不挤压 tab 标题。
- [x] 参考 lsd 的语义而非复制实现，为权限字符分别提供 read/write/execute/sticky/no-access 等主题 token；selected/cursor 状态优先级最高，低色彩模式合并为可辨识的有限色板。
- [x] 在版本化 snapshot 中以 additive capability 提供 bounded/sanitized owner 与 group display（解析失败使用 uid/gid）；解析与缓存属于 C core，不在 TypeScript 中 shell-out，并避免 NSS/远程解析阻塞 UI 主循环。
- [x] 按 pane 可用宽度响应式显示 owner/group；窄 pane 保留名称、大小、时间和权限。时间采用 hour-old/day-old/older 三档语义色，近期修改项更亮，但不得覆盖 cursor、selection、错误和 executable 等更高优先级状态。
- [ ] 为 keymap、排序 comparator、协议边界、响应式 column、主题降级和 preview 生命周期分别补 unit/integration；正式 PTY 覆盖 Space/Tab 分离、快速移动 cursor、双 pane 状态不变、鼠标滚动以及 60/80/100/160 列。
- **阶段验收：** 上述九项视觉/交互需求在 Unicode、ASCII、低色彩和窄宽终端均有稳定行为；经典 Vifm 排序和 OpenTUI 既有 tab/pane 操作无回归；形成一个独立提交。
- **状态：** in_progress（Space/Tab、source-pane/target-pane preview intent、target-lane generation cancellation、目录优先 comparator、三行底栏、滚动条/active marker/sort/plus/权限时间/owner-group 信息层和 TaskCenter 取消/详情入口已落地；unit、真实 core session、PTY、ASCII/compact 基线已复验，完整低色彩与 60/80/100/160 列矩阵和阶段独立提交仍待补齐）。

### Phase 1B：Vifm 文件打开关联与 macOS 默认 opener

- [ ] 以 `src/filetype.c`、`src/running.c`、`data/vifmrc` 和 Vifm 文档建立 resolver 行为矩阵，记录 pattern/MIME 匹配、候选程序选择、宏展开、terminal/graphical/viewer 区分及无匹配 fallback。
- [ ] 保持 Vifm 配置为唯一来源：用户继续通过 `:filetype`、`:filextype`、`:fileviewer`（以及现有 Lua/config 入口）管理关联；OpenTUI 只发送结构化 intent 并呈现解析结果，不维护按后缀硬编码的第二张表。
- [ ] 明确三个互不混淆的 intent：`preview` 供 F3/Space 使用 `fileviewer` pipeline，`edit` 供 F4 使用编辑器语义，`open` 供 Enter/显式打开普通文件使用 `filetype/filextype` 与平台 fallback。
- [ ] 解析优先级固定为：用户显式 association -> Vifm 内建/兼容规则 -> 平台默认 opener -> 结构化 unsupported；目录、archive 和 remote 的 enterable-resource 判断必须先于普通文件 opener，不能把 ZIP 错交给桌面应用。
- [ ] macOS 在没有显式 association 时，以 argv 方式调用绝对路径 `/usr/bin/open` 并传入规范化的绝对目标路径；Linux/Windows fallback 单独 capability 化，不把 shell command 拼接进客户端或协议。
- [x] 建立 core-owned `open-v1` intent/result 边界：显式 structured association argv 优先，macOS/Linux/BSD 平台 opener 作为 fallback，v3 session 发布可校验的 resolved `open` record；当前不复制 Vifm 后缀表。
- [x] 为 `open` command 携带 source pane、cwd/snapshot/entry stat identity；core 在 resolver 前拒绝 stale target，目录继续要求走 enter/resource path。
- [x] 增加 bounded caller-supplied `filetype/filextype/fileviewer` rule resolver：有序 glob、argv tokenizer、`%f/%c/%%` 宏和 shell-syntax 拒绝；并在无显式 association 时有界读取 `MYVIFMRC`，显式 argv 仍优先。
- [ ] 复用 Vifm running/background 的 cwd、前后台、终端暂停/恢复、退出码和错误处理语义；GUI opener 不阻塞 UI，terminal program 必须有显式 suspend/restore 生命周期，失败在状态栏/任务事件中可见。
- [ ] 所有目标、宏和外部程序在 core 边界校验：open target identity、NUL、记录大小、结构化 argv 和有界 `MYVIFMRC` 读取已完成；仍需补齐完整 MIME/宏集合、配置语义、终端生命周期和敏感远程参数审查。
- [x] 先写 resolver/argv RED tests，覆盖显式规则优先、macOS fallback、未知文件、空格/引号、shell syntax、宏边界、open target stale identity 和真实 core session；archive 优先进入、缺失 opener 和 helper 生命周期仍待补齐。
- **阶段验收：** 同一份 Vifm 配置在 classic 与 OpenTUI 中得到可解释的一致关联；macOS 普通未知文件默认交给系统应用，F3/Space/F4/目录/archive 不被误路由；形成一个独立提交。
- **状态：** in_progress（已完成结构化平台 opener、bounded rule resolver、`MYVIFMRC` 有界加载、App `l`/Enter 注入和 stale target identity；完整 MIME/宏/候选语义、running/background 生命周期、archive/remote capability 和完整阶段验收仍待完成）。

### Phase 2：文件操作语义回归 Vifm core

- [ ] 盘点当前 `src/neovifm/action_task` 与 Vifm `ops`、`undo`、`fops_cpmv_bg`、`fops_put`、`background` 的重叠，形成保留/适配/移除清单。
- [x] 建立受限 `mkdir -> classic undo` bridge：只记录成功 mkdir，使用 parent/child no-follow identity，并在 core 主线程执行 `u`。
- [x] 让 OpenTUI 的 `u` 进入 core-owned undo command；当前对 copy/move/delete 明确返回 `undo-empty`，不宣称尚未实现的 undo 语义。
- [ ] 让 OpenTUI 的 copy/move/delete/mkdir 请求完整进入 Vifm 成熟的操作与 undo 语义；不得继续扩大平行 `nv_fs_*` 文件操作实现。
- [ ] 保留现有 immutable snapshot identity、no-follow/no-overwrite、防 stale target 和明确 partial 结果等安全边界。
- [x] 明确 mkdir undo 的队列忙碌/主线程边界、stale/replaced target 拒绝和 source pane/tab 精确刷新契约。
- [ ] 明确 copy/move/delete 的 undo、取消、冲突处理、跨文件系统、部分完成和双 pane/tab 刷新契约。
- [ ] 在 undo 尚未连通前，不把相关 destructive action 标记为稳定完成。
- **阶段验收：** 与经典 Vifm 对照验证 copy/move/delete、取消、失败、partial、undo 和 selection；形成独立提交。
- **状态：** in_progress（mkdir 首切片已完成；copy/move/delete undo、取消和 Vifm background facade 仍待补齐）。

### Phase 3：后台任务队列与任务中心弹窗

- [ ] 复用 Vifm `background.c`、`bg_job_t/bg_op_t`、`:jobs` 与后台文件操作能力，在其上增加可排队、可观察的稳定 task facade；不再新建互不相通的 executor。
- [x] copy/move 默认异步入队；当前 action lane 为有界 FIFO 串行执行，preview 使用独立 lane，避免预览挤占复制/移动。
- [ ] 队列模型至少发布：id、kind、source、destination、queued/running/succeeded/failed/cancelled/partial、items/bytes progress、当前文件、错误、开始/结束时间和 undo availability。
- [x] 当前会话内的 queued/running 与 completed/failed/cancelled action 历史由 v3 reducer 保留，并在任务中心覆盖层中可滚动查看；跨重启持久化另设明确的数据格式、隐私和 retention 决策，不偷偷引入 daemon。
- [x] 在右下角状态栏加入稳定尺寸、可点击的 `Tasks` 入口和 running/queued badge；窄终端仍保留短标签与数字，不遮挡 F3--F10。
- [x] 点击入口打开覆盖式弹窗；Queue/History 两个 tab 支持查看终态详情、取消 pending/running、清理当前视图历史和关闭弹窗。
- [x] 为 failed/cancelled 保留经验证的 source/destination/target identity 并实现 core-owned 安全 retry；重试复用原始不可变 action，不从 UI display path 重建请求，并以 64 条历史上限控制内存。
- [ ] 主界面在大目录 copy/move 期间必须继续响应导航、pane/tab 切换、F3、任务弹窗和退出请求。
- [ ] 退出时若有任务，必须给出继续等待/协作取消/返回应用的明确选择；本阶段不让任务脱离应用成为常驻 daemon。
- [ ] 将 ViATc/Total Commander 的“后台传输管理器”和 Vifm `:jobs` 语义统一到同一 task center，而不是提供两个互相矛盾的入口。
- **阶段验收：** 用受控大文件/大目录真实复制验证输入无卡顿、队列顺序、进度、取消、失败、history、undo 状态与鼠标入口；形成独立提交。
- **状态：** in_progress（FIFO、历史可见、Queue/History tab、终态详情、当前会话历史清理、Tasks 入口、取消和 core-owned 安全 retry 已完成；完整 source/destination/items/bytes DTO、undo availability、退出协作选择与 Vifm background facade 仍待补齐；mkdir undo 已在 Phase 2 单独落地）。

### Phase 4：压缩包作为目录打开

- [ ] 优先桥接 Vifm 已有 `FUSE_MOUNT`/`FUSE_MOUNT3`、`fuse-zip`、`archivemount`、AVFS 与 file association 生命周期，不先实现完整 VFS/provider 框架。
- [ ] 第一纵向切片支持 ZIP：光标位于 `.zip` 时，Vifm 原生 `e`（explore contents）作为明确入口，`l`/Enter 按统一 enterable-resource 规则进入，`h` 返回；tab、history、selection、F3 和另 pane copy-out 正常。
- [ ] ZIP 首切片默认只读浏览和 copy/extract out；写回 archive、archive 内 move/delete 和原地重打包在安全事务模型确定前保持 disabled。
- [ ] 后续扩展 tar/tgz/7z，并通过 capability 检测报告缺少 mount helper、格式不支持、挂载失败和损坏 archive。
- [ ] mount 请求、进度、错误、取消和 unmount/cleanup 必须出现在任务中心；正常返回、关 tab、退出和异常恢复都不得泄漏 mount。
- [ ] 协议只发布资源/mount capability 与不可变 pane snapshot，不向 TypeScript 暴露 `view_t` 或任意 mount shell command。
- **阶段验收：** 正常/空/损坏/大 ZIP，文件名编码、符号链接、嵌套目录、helper 缺失、取消和清理均有测试；形成独立提交。
- **状态：** pending。

### Phase 5：SSH 挂载目录

- [ ] 复用 Vifm `FUSE_MOUNT2` + `sshfs` 模式，先支持连接描述/配置项进入远程目录；不把网络协议实现在 OpenTUI 中。
- [ ] 连接、认证等待、断线、重连、取消和 unmount 通过任务中心可见；口令、私钥内容和敏感参数不得进入协议、日志或历史。
- [ ] 第一切片支持浏览、进入/返回、F3、下载到本地和从本地复制；远端 move/delete/undo 只有在 provider capability 与失败语义明确后才启用。
- [ ] slow filesystem 不得阻塞 UI；snapshot、preview 和文件动作均显式携带 pane/tab/cwd/provider/cancellation context。
- [ ] 使用可注入假 sshfs/mounter 做自动化测试，真实 SSH 作为 opt-in E2E，不让 CI 依赖外网或个人凭据。
- **阶段验收：** mount、浏览、copy in/out、断线、超时、取消、清理和 secret redaction 全部通过；形成独立提交。
- **状态：** pending。

### Phase 6：F3 统一富媒体预览

- [ ] 以 Vifm `previewprg -> fileviewer -> builtin fallback` 的优先级作为 viewer resolver，桥接 `quickview/vcache/background`、`%px/%py/%pw/%ph`、`%pc` 清理和 `%pd` pass-through 语义。
- [ ] F3 继续打开全工作区 viewer，Space 使用 Phase 1A 定义的对面 pane 临时 viewer；两者共享 resolver/cache/cancellation，不恢复常驻第三 pane，也不修改目标 pane 的目录模型。
- [ ] 明确主线语义：F3 是全屏“查看”，Space 是双栏快速查看；`l` 对普通文件进入查看路径，对目录/archive/remote 则执行 enter；不得把 classic quickview 的第三栏布局与 OpenTUI 临时渲染层混为一套 UI。
- [x] 第一批切片：纯文本、Markdown 终端渲染、PDF 首页文本抽取；当前仍未接入 Vifm fileviewer 优先级。
- [ ] 第一批剩余：代码高亮、PDF 图形/首页渲染、图片。
- [ ] 第二批：视频首帧、音频 metadata/封面、archive listing、二进制/hex fallback。
- [ ] 图片能力顺序由终端 capability 决定：可用协议渲染、`chafa` 降级、纯文本 metadata 最终降级；无 Nerd Font/低色彩仍可读。
- [ ] 复用本地 `/Users/rex/soft/_refs/neovifm/vifm-sixel-preview`、`vifmimg` 和 Yazi 作为行为参考，先做许可证/来源审查，不直接复制代码。
- [ ] 所有外部 viewer 使用结构化 argv、明确 cwd、超时、输出上限、取消和清理，不执行 display path，不拼接未验证 shell 输入。
- **阶段验收：** 图片/PDF/Markdown/代码/视频/音频在宽窄终端、支持/不支持图形协议、快速移动 cursor、helper 缺失和损坏文件下行为稳定；形成独立提交。
- **状态：** pending。

### Phase 7：主线联调与计划收口

- [ ] 串联快捷键、task center、archive、SSH 和 F3：archive/remote 内的 copy/preview/mount 必须共享同一任务可观察面。
- [ ] 更新 README、架构文档和协议文档，明确 OpenTUI 默认入口、Hybrid 主线含义、classic compatibility 和 capability 降级。
- [ ] 完成安全审查、许可证审查、代码审查、全量回归和真实 PTY 走查。
- [ ] 每个阶段只提交自身通过验收的范围；最终确认工作树、提交历史和 planning 文档没有混入未验收功能。
- [ ] 安装/发布继续留在后续独立计划，不作为本计划完成门槛。
- **状态：** pending。

## 通用验收门槛

每个实现阶段至少完成：

1. TDD RED -> GREEN -> refactor 证据；新增/修改模块覆盖率不低于 80%。
2. focused C tests 与相关 integration tests。
3. `clients/tui` unit、coverage、typecheck、dependency audit。
4. 真实 C session 与正式 PTY；不能只依赖 test renderer。
5. 60/80/100/160 列、ASCII/无 Nerd Font、低色彩和鼠标/纯键盘两种路径。
6. `env -u VIFM -u MYVIFMRC make check` 串行全量回归。
7. `git diff --check`、安全审查、第三方许可证/来源检查。
8. 验收通过后独立 conventional commit；未经明确要求不 push、不改写历史。

## 主要风险

- 把“继承快捷键”实现成 TypeScript 中另一套 Vim engine，造成与 Vifm 长期漂移。
- 同时保留 `nv_fs_*`、Vifm ops 和新 task executor 三套文件操作路径。
- archive/SSH 挂载在异常退出、tab 关闭或 helper 崩溃后泄漏。
- remote/archive 写操作缺少本地文件系统等价的原子性和 undo，却被 UI 错误启用。
- 图形 preview 的终端序列清理失败，残留覆盖 TUI；旧 generation 反向覆盖新 selection。
- Space 快速预览如果直接替换对面 pane snapshot，会污染 cwd/tab/history；必须保持临时 render state 与 pane model 分离。
- owner/group 名称解析可能经 NSS 或远程 provider 变慢；无缓存地放进 snapshot/UI 主循环会重新引入卡顿。
- 在 TypeScript 中维护扩展名到命令的映射，会与 Vifm association 配置漂移，并扩大 shell 注入与平台差异风险。
- 直接复制 ViATc/Yazi/preview repo 实现，带来语言不适配和许可证问题。

## 错误记录

| 错误 | 尝试 | 处理 |
|---|---|---|
| 正式 PTY 在 `h` 后用初始帧中已存在的 `right-file` 提前匹配 | Phase 0 integration 2 | 改为在 `h`/`j` 后等待稳定的路径与复制结果，并用延时让 core command 顺序完成；不再用含 ANSI 增量重绘的 tab 文本作为同步条件 |
| macOS `misc/running.c:selection_multi_run` 在 500ms 内未完成后台 shell/error-pipe 回收 | 串行 `make check` 首次执行 | 将 `wait_for_all_bg` 测试等待上限放宽到 5s；产品后台代码未改，`make -C tests misc` 与完整串行 `make check` 复跑通过 |
