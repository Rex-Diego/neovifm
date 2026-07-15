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
