# NeoVifm Hybrid M0 开发计划

## 目标

验证 NeoVifm 的混合架构：保留 Vifm C 内核，通过版本化本地协议向 TypeScript/Bun/OpenTUI 客户端发布不可变 pane snapshot，并证明客户端能够启动、渲染和响应终端尺寸变化。

## 当前阶段

Complete

## 阶段

### Phase 1：架构与协议

- [x] 写 ADR，记录采用与拒绝的方案
- [x] 定义 `PaneSnapshot v0` JSON/JSONL schema
- [x] 明确 framing、版本、错误和兼容规则
- **状态：** complete

### Phase 2：C Core Probe

- [x] 先写失败测试覆盖空目录、基础条目和特殊文件名
- [x] 实现 headless snapshot probe
- [x] 输出协议版本、pane 元数据和结构化 entries
- [x] 不初始化 ncurses、不读取用户 vifmrc
- [x] 用 `compat/os.h` 目录 API 和 Windows 构建目标替换 POSIX-only 依赖
- [x] 在 producer 端同时限制字段、条目和 JSON record 字节数；Windows 保留 no-follow symlink 身份
- **状态：** complete（Windows 实机编译/悬挂链接验证留给 CI）

### Phase 3：OpenTUI Client

- [x] 建立 Bun 客户端包和 TypeScript 严格配置
- [x] 先写 schema/parser 和状态 reducer 测试
- [x] 实现 OpenTUI 列表、状态区和 resize 布局
- [x] 客户端不得执行文件操作
- [x] 为运行时不可变 DTO、严格 JSONL/输入限额和 reducer 写失败测试
- **状态：** complete

### Phase 4：联调

- [x] 客户端启动/管理 C probe
- [x] 验证 JSONL framing、错误和进程退出
- [x] 验证窄终端、宽终端和无图标降级
- [x] 验证启动取消、结构化错误和非零 CLI 退出码
- [x] 验证 producer 在未超过 entry 上限但超过 JSON byte budget 时返回 `snapshot-too-large`
- **状态：** complete

### Phase 5：产品化决策

- [x] 串行 `make check`、Bun tests、typecheck、`git diff --check`
- [x] 代码与安全审查
- [x] 决定 probe 如何演进为长期 core service
- **状态：** complete

## 原型边界

- 原型只读，不实现 copy/move/delete。
- C probe 必须复用现有 compat/filesystem helpers，不能复制第三方实现。
- 协议先用 JSONL over stdio，避免在 M0 引入常驻 daemon、socket 和认证。
- 客户端使用 OpenTUI/Solid，但不得复制 OpenCode 源码。
- Lua 兼容、持久 session、preview 和插件 SDK 不属于 M0。

## 成功标准

1. `neovifm-core-probe <path>` 输出符合 schema 的 snapshot。
2. Bun 客户端能启动 probe、校验输入并显示目录。
3. core 崩溃、协议错误和无权限目录显示明确错误。
4. 默认 Vifm 构建和测试零回归。
5. 原型代码有测试且无硬编码秘密或网络依赖。

## 风险

| 风险 | 缓解 |
|---|---|
| OpenTUI API 快速变化 | 锁定精确版本，封装在单一 adapter |
| C/TS 类型漂移 | JSON Schema 为唯一协议源，双方有 fixture tests |
| Bun 增加分发复杂度 | M0 仅验证，不替换经典客户端 |
| Vifm core 与 ncurses 耦合 | M0 先做独立 headless probe，随后再逐步接入真实 PaneModel |
| JSONL 无背压/二进制能力 | M0 只发送小型控制消息，后续 ADR 再评估 MessagePack/socket |

## M0 完成决策

- M0 保持短命、单目录、只读 probe，不引入 daemon、socket 或文件操作。
- M1 按 `docs/NEOVIFM_ARCHITECTURE.md` 的既有路线，将独立扫描替换为可测试的真实 `PaneModel` adapter，随后增加导航、双 pane、selection、watcher 与增量 snapshot/event。
- 只有出现 ADR 0001 的复审条件（可测性能瓶颈、多客户端、长生命周期或远程访问）时，才评估常驻 core service 或其他 transport。

## 错误记录

| 错误 | 尝试 | 处理 |
|---|---|---|
| 在 `/tmp` 对当前已配置源码做 out-of-tree sanitizer configure | 1 | Autoconf 拒绝已配置 source directory；不运行 `make distclean`，改用隔离的 detached git worktree 作为干净 source。 |
| macOS AddressSanitizer 运行时启用 `detect_leaks=1` | 2 | 本平台 ASan 不支持 LeakSanitizer；改为保留 ASan/UBSan 的 `halt_on_error=1`，不声称完成 leak 检查。 |
| Windows probe dry-run build | 1 | `Makefile.win` 无 `neovifm-core-probe.exe` 目标；已写入 RED 验证，待添加跨平台目标。 |
| TUI strict typecheck 在新错误呈现/union DTO 测试处失败 | 1 | 显式将错误详情数组标为 `string[]`，并在测试中按 `record.type` 收窄 union；修复后 typecheck 通过。 |
| `git diff --check` 报告自动生成的 `src/Makefile.in` 尾随空白 | 1 | 由 Automake 新增 probe 规则产生；交付前通过受控补丁去除该行尾随空白并重新验证。 |
| 为提升 `index.tsx` 覆盖率重构 renderer 注入时出现 TSX 对象属性语法错误 | 1 | 将 JSX 式 `onCancel={...}` 更正为对象属性 `onCancel: ...`；索引测试与 typecheck 随后通过。 |
