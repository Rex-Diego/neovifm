# NeoVifm Hybrid M1：双 Pane 文件工作台

## 目标

在不改变经典 Vifm 默认行为、文件操作、配置目录和 Lua API 的前提下，把 Hybrid M0 的单目录只读验证升级为真正的 Vifm/Total Commander 双 pane 浏览基础：两个独立目录、明确 active pane、各自的 cursor/selection，并由 C core 通过版本化不可变 DTO 发布给 OpenTUI。

M0 的单 pane probe 已完成协议与生命周期验证，但不能被当作产品目标完成。M1 是该缺口的直接后继；旧版“C renderer 骨架”M1 计划在 2026-07-11 被 Hybrid 架构取代，本文件现按用户明确的双 pane 目标重开。

## 当前阶段

Phase 4：M1b 交互会话、真实 Pane 接入与增量刷新

## 阶段

### Phase 1：双 Pane 契约与兼容迁移

- [x] 盘点 M0 `PaneSnapshot v0`、core probe CLI、OpenTUI renderer 与经典 `view_t` 的可复用边界
- [x] 定义 versioned workspace DTO：`left`、`right`、`active_pane`、每 pane 的 cwd/cursor/selection 与 error 语义
- [x] 决定并记录 v0→v1 迁移：旧 client 的失败方式、client dual-version 支持范围和 capability 协商
- [x] 先写 schema/parser/reducer 的失败测试，覆盖 pane identity、顺序、独立 cursor/selection、record/byte limits
- **状态：** completed

### Phase 2：M1a C 双 Pane Core（原子只读工作区）

- [x] 为两份独立 pane snapshot 建立 owned workspace model 与单一释放路径
- [x] 保留单目录 v0 CLI；为两个目录参数发布 v1 workspace，明确零/一/两参数的行为和结构化 usage error
- [x] 从可测试的 pane adapter 构建 workspace，禁止跨边界暴露 `view_t` 或可变条目指针
- [x] 输出 workspace 协议、active pane 与结构化错误；producer 按整个 workspace 实施字段、entry、record 和 stream 限额
- [x] 扩展 Autotools 与 Windows 构建目标；写 C 单元/协议测试
- **状态：** completed

### Phase 3：M1a OpenTUI 双 Pane 默认工作台

- [x] 支持 workspace DTO、双 pane reducer、逐记录状态与 core invocation
- [x] 默认宽终端左右双列表；窄终端降级为 active pane 单栏而不是详情替代第二 pane
- [x] 显示 active pane、各自 cwd/cursor/selection；active pane 由 v1 workspace DTO 明确给出
- [x] 写 OpenTUI 宽/窄/ASCII、双目录和 protocol-error E2E 测试
- **状态：** completed

### Phase 4：M1b 交互会话、真实 Pane 接入与增量刷新

- 当前优先级：先交付 macOS；Windows/其他平台 session watcher 暂不作为 M1 完成前置。
- [x] 新增由 TUI 持有的 `neovifm-core-session` stdio 子进程；保留 M0/M1a probe 的终结记录语义，加入 Tab、移动、进入、返回和 selection，不引入 daemon、socket 或网络协议
- [ ] 用经典 Vifm 可测试 pane/filelist 能力替代 M0 的孤立扫描路径，记录无法复用的耦合
- [ ] 为 selection、排序、过滤和双 pane 刷新建立 immutable snapshot adapter
- [x] 在 macOS 接入 watcher/刷新事件，显式携带 pane、cwd、selection 与取消上下文；后台不触碰 TUI 状态
- [x] 保持 classic ncurses renderer 为默认兼容基线，并验证不回归
- **状态：** in_progress

### Phase 5：验收与 M2 入口

- [x] C focused tests、串行 `make check`、TUI tests/coverage/typecheck/audit、真实双 pane integration 和 `git diff --check`
- [x] 安全与 C/TypeScript 审查，修复 HIGH/CRITICAL 问题
- [ ] 评估 M2 的异步预览/任务中心入口；不在未写 ADR 时引入 daemon、socket、Rust/C++ 或网络协议
- **状态：** pending

## 架构约束

- 双 pane 是默认的宽终端模型；详情仅是辅助信息，不能取代右 pane。
- 每个 pane 都有独立 cwd、cursor、selection、排序、过滤和导航上下文；不能用进程级 `chdir()` 传递状态。
- C/TypeScript 边界只传版本化、不可变 DTO；TUI 不得持有 `view_t`、`dir_entry_t` 或其他内部可变地址。
- M1 保持只读。文件操作、undo、Lua API 和经典 renderer 不改动。
- 协议升级必须明确旧 client 的失败方式；同一版本不得悄然改变已有字段语义。
- 无 Nerd Font、低色彩和窄终端时，双 pane 信息必须可操作地降级。

## 验收标准

1. `neovifm-core-probe <left> <right>` 发布两个独立 pane 和明确 active pane；单目录调用继续是 v0 兼容 probe。
2. 新 TUI 宽终端默认显示左右文件列表，而不是“列表 + 详情”。
3. M1b 的 Tab 和导航只改变 active pane 的上下文；另一个 pane 的 cwd/cursor/selection 保持不变。
4. 协议、取消、超时、限额和错误不会让一个 pane 的失败污染另一个 pane 或经典 vifm。
5. classic Vifm 行为和串行 C 回归零回归；新 C/TypeScript 模块覆盖率不低于 80%。

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| v0 单 pane payload 无法无损表达双 pane | 通过显式版本升级与 capability 协商迁移，不在 v0 偷换字段语义 |
| `view_t`/filelist 仍与 ncurses、全局 cwd 耦合 | 先建立最小可测试 adapter，逐字段移入，不让 TUI 读取内部结构 |
| 双 pane workspace 超过 M0 4 MiB record | C producer 对整个 workspace 计量并在序列化前结构化失败 |
| 宽/窄布局语义漂移 | 文本快照测试覆盖双列、单列降级、ASCII 和低色彩 |
| 为导航误引入文件操作 | TDD 明确只读边界，禁止 copy/move/delete 代码路径 |

## 错误记录

| 错误 | 尝试 | 处理 |
|---|---|---|
