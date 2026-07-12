# NeoVifm Hybrid M0 开发计划

## 目标

验证 NeoVifm 的混合架构：保留 Vifm C 内核，通过版本化本地协议向 TypeScript/Bun/OpenTUI 客户端发布不可变 pane snapshot，并证明客户端能够启动、渲染和响应终端尺寸变化。

## 当前阶段

Phase 2 - C Core Probe

## 阶段

### Phase 1：架构与协议

- [x] 写 ADR，记录采用与拒绝的方案
- [x] 定义 `PaneSnapshot v0` JSON/JSONL schema
- [x] 明确 framing、版本、错误和兼容规则
- **状态：** complete

### Phase 2：C Core Probe

- [ ] 先写失败测试覆盖空目录、基础条目和特殊文件名
- [ ] 实现 headless snapshot probe
- [ ] 输出协议版本、pane 元数据和结构化 entries
- [ ] 不初始化 ncurses、不读取用户 vifmrc
- **状态：** in_progress

### Phase 3：OpenTUI Client

- [ ] 建立 Bun workspace 和 TypeScript 严格配置
- [ ] 先写 schema/parser 和状态 reducer 测试
- [ ] 实现 OpenTUI 列表、状态区和 resize 布局
- [ ] 客户端不得执行文件操作
- **状态：** pending

### Phase 4：联调

- [ ] 客户端启动/管理 C probe
- [ ] 验证 JSONL framing、错误和进程退出
- [ ] 验证窄终端、宽终端和无图标降级
- **状态：** pending

### Phase 5：产品化决策

- [ ] 串行 `make check`、Bun tests、typecheck、`git diff --check`
- [ ] 代码与安全审查
- [ ] 决定 probe 如何演进为长期 core service
- **状态：** pending

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

## 错误记录

| 错误 | 尝试 | 处理 |
|---|---|---|
