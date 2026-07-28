# NeoVifm TUI 可用性 Findings

## 2026-07-15 恢复现场

- M2 已完成但全部改动仍在 dirty worktree；本轮必须在其上做定向 UI 修复，不能清理或覆盖 M1/M2 变更。
- 用户明确否定宽屏第三个 preview pane；preview/task 数据可以保留，但不能常驻占据文件 pane 横向空间。
- 架构基线要求双栏优先、可降级显示、字段宽度稳定，并明确吸收 lsd 的图标/颜色/权限/大小/时间表达。
- Total Commander 功能键中的真实 copy/move/delete 尚不在当前 v3 command 能力内；UI 应显示能力状态，不能伪装成已实现操作。

## 待确认的代码事实

- OpenTUI `useKeyboard` 对 Tab 的 `key.name` 实际值与测试模拟是否一致。
- 当前 pane 标题、文件字段和底栏在 60/80/100/120 列的实际文本帧。
- snapshot 已有字段能否支持权限、大小、mtime 和类型色彩，无需改协议。

## 2026-07-15 键盘问题

- 当前 `App` 直接比较 `key.name`，只测了 `mockInput.pressTab()`；没有覆盖真实字母输入、repeat/release、Shift-Tab 或 Vifm 成组键序列。
- OpenTUI `KeyEvent` 同时提供 `name`、`sequence`、`raw`、`eventType` 和 `source`。字母键应以规范化 `sequence || name` 解析，并只处理 `press/repeat`，避免 Kitty keyboard 的 release 事件重复执行。
- 当前代码名义上写了 h/j/k/l，但测试没有证明这些键经过 renderer -> callback -> v3 session；用户实际运行已证明现有实现不可用，必须把真实 mock key 输入和 session 更新纳入验收。
- 用户要求快捷键与 Vifm 一致；本轮至少覆盖 `h/j/k/l`、方向键、Tab/Space pane focus、`r` refresh、`q` quit，并为 `gg`/`G` 预留需要 core command 支持的边界。
- OpenTUI raw parser 已确认：普通字母 `name` 为小写字母，Tab 为 `tab`，大写字母会设置 `shift`；`useKeyboard` 默认只订阅 press（repeat 包含在内）。因此 h/j/k/l 失效不是名字假设导致，下一步要查真实 renderer -> `session.send()` -> C v3 acknowledgement 链路。
- `mockInput.pressTab()` 仅证明 callback mapping，不能证明 pane state 更新；需要真实 C session 驱动的键盘集成测试，等待 workspace cursor/active pane 发生变化。

## 2026-07-15 真实键盘链路

- 新增真实 C v3 session + OpenTUI test renderer 集成测试后，`j/k/Tab/l/h` 在测试环境能依次移动游标、切换右 pane、进入目录和返回父目录。这排除了 C command parser 与基本 raw key name 的系统性故障。
- 用户现场仍不可用，最可能的剩余差异是终端/Kitty keyboard 事件字段或焦点组件默认行为；实现应同时使用 `name` 与 `sequence` 归一化，并对已处理键调用 `preventDefault()/stopPropagation()`，避免 Tab 和字母被内部组件截获。
- “与 Vifm 一致”不应只停留在 h/j/k/l；需要加入 `gg`/`G`（首尾）、方向键 fallback，且由 core snapshot 更新证明，不做 UI 本地假移动。

## 2026-07-15 Vifm 源码键位基线

- `src/modes/normal.c` 的权威默认语义：`h`/Left 返回父目录，`j`/Down 下移，`k`/Up 上移，`l`/Right 打开或进入；`gg` 到首项，`G` 到末项。
- Vifm 默认 `Space` 是切换 pane，不是 toggle selection；当前 OpenTUI 把 Space 映射为 selection，属于明确的兼容 bug。Tab 对应 Ctrl-I，也有 pane/history forward 语义，可在当前双 pane slice 中作为切 pane处理。
- Vifm 还支持 Ctrl-N/Ctrl-P 作为 j/k，Ctrl-W w/p 切 pane；本轮可以在不扩张文件操作的前提下补齐这些导航别名。
- Total Commander F3/F4 等按键不是 Vifm 默认操作语义；底栏可以作为兼容提示层，但真实导航优先遵循 Vifm，未实现动作必须标记 disabled。
- 二次逐项核对 `builtin_cmds[]` 后确认：普通 `q` 是 `q:`/`q/`/`q?` 等序列前缀，不是退出；普通 `r` 只参与 `rl`，不是刷新；Vifm 重绘是 Ctrl-L，退出是 `ZZ`/`ZQ`。旧 TUI 的 `q` quit 与 `r` refresh 必须移除。
- KeyEvent 归一化会把大写字母转成小写 name，因此必须显式检查 `shift`；否则 `H/L` 会错误触发 `h/l`，Ctrl-W H/L 也会错误退化为定向 focus。

## 2026-07-15 OpenTUI 视觉能力

- `<text>` 支持 `fg`、`bg`、`attributes`，`<span>` 也支持独立前景/背景色，因此 Starship 风格状态段可以用纯 OpenTUI 实现，不需要引入组件或 powerline 字体。
- 为保证无 Nerd Font 可用，状态段使用 ASCII 分隔与背景色块；文件类型默认使用稳定单宽 ASCII glyph，颜色和字段层级负责主要美化。
- 当前 snapshot 已提供 `kind`、`size_bytes`、`mtime_unix_ms`、`mode_octal`、`selected`、`hidden`，足以实现 lsd 风格的类型、权限、大小、时间和名称列，无需改协议。

## 2026-07-15 Tab 根因

- 扩展真实集成后精确复现：Space 从 left 切到 right 成功，紧接的 Tab 未切回 left。说明 `useKeyboard` 持有的 `props.workspace.active_pane` 是挂载时的陈旧值，Tab 总是按初始 left 计算并发送 `focus right`。
- 修复必须让 C core 成为 pane 切换的唯一真相：新增无参数 `focus-next` command，由 `nv_workspace_session_apply()` 原子切换 active pane。Space、Tab、Ctrl-W w/p 都发送它，不再从 UI snapshot 推导目标 pane。
- Ctrl-W h/l 是定向切 left/right，与 toggle 不同，应保留显式 pane command。
- `focus-next` 落地后，键盘映射不再需要接收 active pane 参数；移除该参数可从类型层防止再次在 UI 中推导 toggle 目标。
- `app.tsx` 中遗留的总 entry/selection 统计 helper 已不参与新版底栏，移除以保持渲染层只计算实际展示状态。
- `neovifm-core-session` 与已有 `neovifm-core-probe` 同为构建产物，但此前未列入 `.gitignore`；补齐忽略规则，避免把本地二进制误纳入后续提交。

## 2026-07-15 ViATc 参考仓库

- 项目统一参考源码目录已经存在于 `/Users/rex/soft/_refs/neovifm`，其中 btop、lsd 等均采用 shallow clone；ViATc 应沿用该组织方式，不能嵌套进 NeoVifm Git 工作树。
- GitHub 搜索确认目标为 `linxinhong/ViATc`（Vim Mode At Total Commander）。已 shallow clone 到 `/Users/rex/soft/_refs/neovifm/viatc`，origin 为 `https://github.com/linxinhong/ViATc.git`，master HEAD 为 `755cead6477dfc8968009f41dc7d41252fc1ac4b`。
- ViATc 是 AutoHotkey 快捷键/动作映射平台，可参考多键序列、计数、Total Commander command action 与 pane/tab 切换；其实现语言和 Windows 注入模型不进入 NeoVifm runtime。

## 2026-07-15 真实终端返工

- 用户确认已能看到新版双行底栏，但真实终端中的 `h/j/k/l` 与 Tab 都没有效果；这证明 renderer mock + C session 集成没有覆盖正式入口的真实输入订阅或终端事件形态。
- 当前 `FunctionKey` 只是 `<box>/<text>` 视觉组件，没有任何 mouse handler；按钮点了无反应是确定性实现缺失，不是环境问题。
- 当前状态栏仅由矩形背景块和 ASCII `|` 拼接，缺少 Starship 的连续箭头边界、主次层级、右侧上下文以及一致的 palette；必须按真实文本帧和支持 Nerd Font/ASCII 两种能力重新设计。
- 每个 pane 需要 btop 风格的列标题。为同时满足 Vifm 语义与用户新要求，字母 `h/l` 继续负责父目录/进入，独立的 Left/Right 方向键改为在 `name/size/mtime/mode` 排序字段间移动。
- 排序不能只在 Solid 组件内对 entries 建派生数组，否则 core cursor index、selection 和预览 identity 会与画面错位；排序字段和方向必须通过版本化 command 交给 C session，再发布新的不可变 snapshot。
- 正式入口确实通过 `render(() => <App ...>)` 启动，`useKeyboard` 在 `onMount` 后订阅全局 `renderer.keyInput`；真实键无效仍需 PTY 事件日志确认，不能继续由 test renderer 推断。
- OpenTUI renderable 原生支持 `onMouseDown`，test renderer 也暴露 mock mouse；当前 FunctionKey 完全没有 handler，因此可以先用鼠标 RED 测试稳定复现。
- 用户明确拒绝 `d/x/-` 作为主视觉。新的默认应是 Nerd Font/lsd 图标，ASCII 只作为 capability fallback；同时压缩 cursor、selection 与 icon 之间的固定空白。
- 功能键条的产品目的已澄清：当 VSCode/终端宿主抢占 Fn 键时，用户仍能鼠标点击直达同一功能。因此键盘与鼠标必须复用一个 dispatcher，F5--F8 不能停留在 disabled 文案。
- 本机 `lsd 1.2.0 --icon always` 的真实输出使用 Nerd Font 文件类型图标：目录 `/󱧼`、TS ``、TSX ``，图标紧贴名称且只留一个 separator；这应成为默认 fancy 模式的视觉基线。
- 用户自己的 Starship 配置是 Catppuccin Mocha powerline：`` 起始、连续 `` 过渡、`` 收尾，palette 依次 red/peach/yellow/green/sapphire/lavender，文字使用 crust。状态栏应直接遵循这套本机审美，而不是泛化的彩色矩形加 `|`。
- btop `normal.png` 与帮助文本显示：panel title 嵌在边框，列标题颜色区分，当前 sort 在标题处带 `▼/▲`；Left/Right 选择前后排序列，reverse 独立切换。这与用户要求一致，可映射为 pane header 上的 active sort column + arrow indicator。
- Snapshot 已有 `sort_key`/`sort_descending`，现有枚举含 name/extension/size/mtime/type/other，但没有 permissions/mode；Phase 6 需增加 `mode` 并保持 JSON/schema/TS/C 一致。
- Headless snapshot 当前只在 build 末尾按 name qsort；workspace session 没有 sort command。需要新增 cursor-identity-preserving sort API，并让 refresh/enter 后继承当前 pane 的 sort key/direction。
- 为避免再次由 UI 陈旧状态推导，键盘用 core-owned `sort-cycle(delta)`；点击列头用 `sort-by(pane,key)`，core 对相同 key 切换升降、不同 key 切换到升序并激活被点击 pane。
- 已用正式命令 `bun run dev ../.. /tmp` 在真实 PTY 复现：界面完成加载后发送普通 `j` 与 Tab 均没有任何重绘；同一 PTY 发送 F10 序列 `ESC [ 21 ~` 能正常退出。由此排除 stdin 整体断开，范围已缩到普通字符/Tab 的真实 KeyEvent 解析或归一化。
- 首次在应用加载前发送的 `j` 会落在 OpenTUI 终端能力握手窗口，不能作为键盘验收；新的 PTY 测试必须等待 ready frame，再发送按键。
- OpenTUI 0.4.3 的 `StdinParser` 对单字节 ASCII 会立即生成 key event，`parseKeypress()` 明确将 `j` 解析为 name/sequence `j`、Tab 解析为 `tab`；源码层并不存在仅支持功能键的设计。
- `useKeyboard` 注册的是全局 `renderer.keyInput` listener，且全局 listener 优先于 renderable；scrollbox 焦点不应在 callback 之前吞掉按键。下一步用 `OTUI_DEBUG/OTUI_DUMP_CAPTURES` 确认字节是否到达 renderer，再检查 App handler 与 session.send。
- 原因已精确定位：真实 PTY 中 renderer 收到 `j`/Tab，App keymap 也分别产生 `{action:"move"}` / `focus-next`，但 `session.send()` 返回 `false`。
- `@opentui/solid` 的 `render()` 只负责创建 renderer 和挂载 Solid root，挂载完成后 Promise 立即 resolve，并不等待 renderer destroy。`main()` 错把它当应用生命周期 Promise，随即执行 `session.close()`；因此画面仍在、F10 本地退出有效，但所有 core command 都被拒绝。这也解释了为何直接持有 session 的 integration test 全绿而正式入口完全失效。
- 正确生命周期应通过 renderer config 的 `onDestroy` Promise 等待应用退出；必须给 `main()` 增加正式入口生命周期 RED 测试，防止 mount resolve 后提前关闭 session。
- `render()` 支持 `onDestroy` config，但其返回值只表示 mount 完成；可封装 `renderUntilDestroyed()`，以 onDestroy resolve 的独立 Promise 作为 `main()` 生命周期。
- `h/l` 的键位映射本身正确；问题在于 core `enter` 只接受目录，文件返回 `not-directory`。客户端必须把文件上的 `l` 统一为 F3 preview，目录仍发 core `enter`。
- btop 式字段轮换缺少创建时间：snapshot 已有 `ctime_unix_ns`，但 `nv_pane_sort_key_t`、协议 schema、runtime parser 和 UI 标题都没有 `ctime`，因此 Right/Left 不能依次访问 Created。
- 四个 metadata 列在双 pane 终端不足 160 列时会压缩文件名；完整列只在足够宽时显示，中等宽度显示当前 sort 字段并保持可用的 Left/Right 轮换。
- OpenTUI test renderer 提供 `mockMouse.click(x,y)`，renderable 支持稳定 `id` 和 `x/y`；功能键与列标题测试应按 renderable id 定位，避免硬编码屏幕坐标。
- 修复后的正式 PTY 已直接观察到最小重绘：`j` 更新 cursor marker，Tab 更新两 pane border/title 与状态栏 active pane。键盘主故障已由实际入口验证关闭。

## 2026-07-15 Phase 6 落地结果

- 正式入口生命周期已由 renderer `onDestroy` 控制；core session 不再在 mount 后被提前关闭。真实 PTY 中 Tab、Left/Right sort 与 F10 均产生预期重绘/退出。
- 两个 pane 的 border title 明确显示 `LEFT/RIGHT`、active 状态和 cwd；列标题嵌入 pane 内容首行，当前 sort 使用 `▲/▼`。
- 宽屏同时展示 Name/Permissions/Size/Modified；中等宽度展示一个可轮换 metadata 列，Left/Right 在 size/mtime/mode/name 间切换，避免选中字段在画面中不可见。
- 文件行默认使用 lsd 风格 Nerd Font 映射，cursor/selection 固定为单宽 `>`/`*`，图标与名称只留一个 separator；`NEOVIFM_ICONS=ascii` 提供明确降级。
- 状态栏采用本机 Starship 的 Catppuccin Mocha 与 `//` 连续 powerline 结构；ASCII 模式不输出 powerline glyph。
- F3--F10 的键盘与鼠标都进入同一个 dispatcher。F4 以 argv 方式启动 `$VISUAL/$EDITOR`，不经过 shell；F5/F6 操作当前项或 selection 到另一 pane；F7 使用输入框；F8 强制确认。
- headless 文件动作通过 `compat/neovifm_fs` 实现 no-overwrite copy/move、mkdir 与递归 remove；所有路径来自 snapshot identity，目录名拒绝空值、`.`、`..`、斜杠、反斜杠与 NUL。
- sort 由 C snapshot API 执行；qsort 后按 path identity 恢复 cursor，refresh/enter/parent 继承 pane 当前 sort key/direction。
- 当前文件动作仍为同步 core command；若后续处理大目录，需要按架构路线迁移到带取消与进度事件的 action task queue，但本轮不再保留假按钮或 disabled 占位。

## 2026-07-15 用户范围纠正：Yazi 仅用于多媒体与美化

- 用户澄清原意：以 Vifm 原有实现为产品底座，只借鉴 Yazi 的多媒体支持与视觉美化；此前把 Yazi 的异步任务、VFS、并发插件等扩展成 Hybrid/OpenTUI 主架构，属于范围扩大。
- 上游 Vifm 原本就是单体 C/ncurses 双 pane：主 `event_loop` 在等待输入的时间片内轮询目录变化、IPC、background callback、viewer cache 和延迟 redraw，不需要 headless core 或 JSONL 客户端才能工作。
- Vifm 已有跨平台 `fswatch`：Linux 使用非阻塞 inotify；无 inotify 的 Unix（包括当前 macOS 路径）退化为 file metadata polling；Windows 使用 change notification。Linux 支持不要求另建 kqueue session 架构。
- Vifm 已有成熟预览扩展面：`:fileviewer`/`previewprg` 选择外部 viewer，`quickview`/Miller view 提供区域，`vcache` 通过 `background` 异步启动外部命令、非阻塞读取、缓存输出、超时和取消，并在数据到达时触发 redraw。
- 图形预览已有三种 viewer kind：textual、graphical、pass-through；`%px/%py/%pw/%ph` 提供区域，`%pd` 支持直接终端序列（如 sixel），`%pc` 定义清理命令，`%pu` 禁用缓存。因此多媒体增强应优先复用这些机制。
- Vifm 原生美化面已经包括 colorscheme/highlight、按文件类型或名称装饰的 `classify`、图标配置、`viewcolumns`、可定制 `statusline`、`fillchars`、Miller view 和多种 pane highlight。
- 当前不执行代码动作。Hybrid/OpenTUI 现有成果待 Phase 6 agent 交接后再由用户决定是主线保留、转实验分支还是选择性移除。

## 2026-07-15 Phase 6 文件动作安全收敛

- 首轮最终审查发现 destructive action 不能只读取执行时 cursor：确认对话期间 cursor/pane 变化会造成“确认 A、操作 B”。修复后 dispatcher 在点击/按键当时冻结 `pane`、`cwd_bytes_hex`、另一 pane 的 `destination_cwd_bytes_hex` 与最多 64 个 `paths_bytes_hex`；core 只接受仍属于对应 snapshot 的精确 identity。
- v3 hello 新增显式 `workspace-sort-v1` 与 `file-actions-v1` capability；TUI 仅在 capability 存在时启用 F5--F8，schema 为 copy/move/delete/mkdir 分别声明必填 identity 字段，core parser 同时实施数量、长度和重复项边界。
- POSIX 文件动作改为 parent-fd-relative `openat/fstatat/readlinkat/symlinkat/unlinkat`，目录与普通文件均 no-follow；destination 使用 `O_EXCL`/`mkdirat`，目录 ancestry 通过 fd 的 dev/inode 向上检查，阻止复制到自身子树。
- 递归 copy 使用 parent-fd-relative no-follow/no-overwrite 与自身子树拒绝；copy 中断或失败可能保留已复制部分，terminal event 必须标记 `partial`，而不是危险地递归清理可能被并发修改的 destination。
- macOS move 使用原子 no-replace rename，跨文件系统 move 显式拒绝，绝不 copy-delete；delete 先移动到同目录私有隔离目录、保留原 basename 后调用 `/usr/bin/trash`，同名替换不会被误删。其他平台不发布 `file-actions-v1`。
- F4 不再使用 `path_display` 执行编辑器；只在 `path_bytes_hex` 可严格 UTF-8 解码时传入 raw identity，并在 editor argv 前加入 `--`，非 UTF-8 identity 显式拒绝。
- 正式 PTY 回归使用 `/usr/bin/expect` 获取真实 pty：等待 ready frame 后发送 `j`、Tab，再用 SGR mouse 点击 F7 创建目录、点击 F10 退出。该测试直接覆盖 VSCode/宿主抢占 Fn 键时依赖鼠标入口的产品目标。

## 2026-07-15 Phase 7 原生基线盘点

- 经典 Vifm 已有 `event_loop` 在输入等待期间处理 redraw、background、watcher 和 UI 状态；`background` 已提供外部命令、内部任务、进度、取消和错误生命周期。
- `utils/fswatch_nix.c` 在 Linux 使用 inotify，并保留 polling fallback；`fswatch_win.c` 提供 Windows change notification。原生 watcher 不需要 Hybrid session 才能工作。
- `ui/quickview.c` + `vcache.c` 已把 previewprg、fileviewer、viewer geometry、缓存、后台读取和取消接入经典 UI；后续图片/视频/音频优先扩展这一条路径。
- 既有可配置的展示面是 colorscheme/highlight、`classify`、`viewcolumns`、`statusline`、`fillchars` 和 Miller/quickview。Phase 7 最小实现应只增强这些已有契约，不复制 Yazi runtime。

## 2026-07-27 鼠标排序、功能键与 pane tabs

- 用户明确新的鼠标契约：列标题左键只反转当前字段方向，右键才轮换 Size、Created、Modified、Permissions；现有“点击任意列直接选字段”的行为不符合要求。
- 顶部全局 `NeoVifm` 与终端尺寸行可以删除；省出的单行应成为左右 pane 各自的 tab bar，而不是继续占用文件区域。
- tab 必须是可点击切换的真实目录状态。客户端不得仅替换标题或本地缓存 entries；激活 tab 后仍需由 C core 发布不可变 workspace snapshot。
- 终端没有 CSS 圆角，Starship 风格圆角应使用 ``/`` 胶囊边界；ASCII 模式必须用清晰可读的方括号或圆括号降级，并显式设置文字前景色。
- F3--F10 “只有按钮没有文字”的直接原因不是主题遮挡：`BottomBars` 在终端宽度小于 90 时把 `compact=true`，`FunctionKey` 随即用 `<Show when={!compact}>` 完全删除 label；80 列正好稳定复现。七个短标签在 80 列仍可排下，应改为紧凑胶囊而非隐藏文字。
- 当前 `ColumnHeader` 的 `onMouseDown` 不区分 mouse button，所有点击都调用 `sort-by(pane,key)`；因此只能在某个列标题上选字段/反转，无法实现“左键方向、右键字段”的二元契约。
- 顶层 App 确实额外渲染一行 `NeoVifm` + `${width}x${height}`；这正是可删除并让位给 pane tab bar 的空间。
- 文件行当前没有 mouse handler。左键精确定位和右键累积多选不能靠客户端改颜色：需要单条 core command 同时设置 active pane、cursor index，并按右键意图 toggle selection，避免两条 command 之间被 watcher 或其他输入穿插。
- 右键批量选择可直接复用 snapshot 现有 `entries[].selected` 与 `selection_count`；已有 F5/F6/F8 的 target 收集逻辑会优先使用 selection，因此不需要另建客户端 Visual 状态。
- 经典 Vifm 已有真正的 pane-tabs 模式：`cfg.pane_tabs`、`tabs_setup_ptab()`、`tabs_goto()`、`tabs_current()`，并且 normal-mode 鼠标会把 tab-line x 坐标映射到 `tabs_goto()`。OpenTUI 的新交互应沿用“每 pane 独立 tab 集合”语义。
- 经典 tabs 直接持有 `view_t`，不能跨 Hybrid C/TS 边界复用；headless session 仍应只发布 tab DTO，并在 core 内保留每个 tab 的目录/排序状态，激活后重建不可变 pane snapshot。
- 当前 `EntryList` 每行已有稳定 `entry-${pane}-${index}` id，适合用 test renderer 与真实 SGR mouse 覆盖左键定位和右键 toggle selection，无需按屏幕文本猜坐标。
- OpenTUI 0.4.3 的真实 mouse contract 已确认：`MouseButton.LEFT = 0`、`MIDDLE = 1`、`RIGHT = 2`，`MouseEvent.button` 是 number；test renderer 的 `mockMouse.click(x,y,button)` 会发完整 down/up SGR 序列，因此左右键都能做稳定 RED。
- 经典 pane tab 的新建语义是“在当前 tab 后克隆当前 view 并激活”。Headless session 可保留 `left/right` 作为活动 snapshot，把非活动 tab 存为 owned snapshot；切换时交换 struct，即可保留 cursor、selection、sort 与 cwd 而不复制 entries。
- 当前 v3 workspace JSON 只含 `active_pane/left/right`。tab DTO 应作为 v3 additive 字段和 `pane-tabs-v1` capability 发布；旧 v0-v2 builder 与旧测试记录继续兼容，TypeScript 对缺失 tabs 回退为每 pane 一个当前 tab。

## 2026-07-27 tab 与状态路径交互定稿

- 用户确认 tab 键盘只先落地 Vifm 正常模式语义：`gt` 下一 tab、`gT` 上一 tab，并保留计数；本轮不为 `:tabnew`/`:tabclose` 新建半套命令行。
- tab 鼠标右键关闭鼠标指向的 tab，不要求先激活；仅剩一个 tab 时拒绝关闭。左键激活对应 tab 并展示其目录。
- pane chrome 不再显示 `LEFT`、`RIGHT` 或 `ACTIVE`；活动 pane 使用单宽小圆点表达，ASCII 能力下降时使用 `*`。
- 状态栏路径模式是 TUI 本地展示状态：默认绝对路径，左键在 `/...` 与 `~/...` 间切换。只有 HOME 本身或其目录边界内的路径才能缩写，不能误把 `/Users/rex-other` 当作 HOME 子目录。
- 右键复制的是当前展示模式下的完整路径文本，即使画面因宽度截断也复制未截断值。剪贴板属于 host 集成，不应加入 core 协议；实现必须直接 argv + stdin，禁止 shell 拼接。
- 仓库已有跨平台参考：macOS `pbcopy`、Linux `wl-copy`/`xclip`/`xsel`、Windows clipboard；TUI 当前没有剪贴板依赖，适合通过可注入 `onCopyText` 服务实现和测试。
- 当前 `app.test.tsx` 与 `production-pty.test.ts` 仍把 `LEFT ACTIVE`、`RIGHT`、`NeoVifm` 和终端尺寸当作成功标志；这些断言与最新视觉契约相反，必须在 RED 阶段改为活动圆点、tab label 和完整功能键文字。
- 当前 keymap 的 `g` 前缀在任何 shifted 第二键上直接返回 unhandled，因此 `gT` 确定不可用；它也没有 count 状态。实现需避免破坏既有 `gg`/`gh`/`gj`/`gk`/`gl`。
- 当前正式 PTY 使用固定底栏坐标点击 F7/F10；功能键改为响应式胶囊并在窄屏换行后，应优先保留 renderable 单测，PTY 则重新按最终 80 列文本帧校准坐标。

## 2026-07-27 最终审查结论

- 用户已明确将排序鼠标语义拆开：左键只反转当前字段，右键才轮换字段。因此不采纳“左键点击非当前标题应切换字段”的 reviewer 建议，避免恢复用户刚否定的旧行为。
- `gt/gT` count 状态不得吞掉下一次普通按键；当前仅为 tab 导航解释 count，其他后续键按原单键语义继续执行。
- core/schema 的 tab id 都从 1 开始；TypeScript runtime validator 也必须拒绝显式 id `0`。旧 v1/v2 snapshot 的本地兼容占位 id `0` 仅在字段缺失时内部生成，且 capability gate 禁止发送 tab command。
