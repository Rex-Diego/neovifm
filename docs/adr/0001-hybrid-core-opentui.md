# ADR 0001：C Core 与 OpenTUI Client 混合架构
- 状态：Accepted
- 日期：2026-07-11
- 决策人：NeoVifm 项目

## 背景

NeoVifm 需要同时继承 Vifm 的成熟文件操作与 Vim 语义，并获得现代 TUI、任务/会话、异步预览和多客户端能力。

Vifm 当前是 C + ncurses 单体应用，核心模型、全局状态、事件循环和 renderer 耦合紧密。OpenCode 当前使用 TypeScript/Bun monorepo、OpenTUI/Solid 客户端、逻辑 client/server 边界、事件流和 session/task 模型。完整改写为 TypeScript 会丢失 Vifm 的成熟内核；继续把所有新界面能力塞进 C 单体则会扩大现有耦合。

## 决策

NeoVifm 采用演进式混合架构：

```text
neovifm-core (C)
  file model / watcher / operations / undo / Vim semantics / Lua compatibility
                         |
                versioned local protocol
                         |
neovifm-tui (TypeScript + Bun + OpenTUI/Solid)
  layout / rendering / command palette / task sessions / preview orchestration
```

具体约束：

1. Vifm C 内核继续是文件系统事实来源。
2. 新客户端不能直接访问 `view_t` 或其他 C 内部结构。
3. 跨边界数据使用不可变、版本化 DTO；第一份 DTO 是 `PaneSnapshot v0`。
4. M0 使用 JSONL over stdio，stdout 仅传协议，stderr 仅传诊断。
5. M0 不引入 daemon、socket、认证、网络或二进制 framing。
6. TUI 使用精确锁定版本的 Bun、TypeScript、OpenTUI 和 SolidJS。
7. 经典 ncurses 客户端在新客户端达到功能等价前继续存在。
8. Lua 兼容层留在 C core；新 TypeScript 插件 SDK 在协议稳定后另行设计。

## M0 组件

### Core Probe

- 独立 headless 可执行文件，不初始化 ncurses，不加载用户 vifmrc。
- 复用 Vifm 的 `compat`、filesystem 和 JSON 工具。
- 接收一个目录参数，输出 `hello` 后输出 `snapshot` 或 `error`。
- 只读，不执行 shell，不进行网络访问。

### TUI Prototype

- 启动并管理 core probe 子进程。
- 校验每条协议记录后更新响应式状态。
- 显示目录、条目和错误；支持宽/窄终端布局。
- 不执行文件操作，不保存 session。

## 协议原则

- Schema 位于 `protocol/neovifm-core-v0.schema.json`，是跨语言契约源。
- major version 不匹配时立即失败；同版本必须忽略未知字段。
- 每行一个完整 UTF-8 JSON object；禁止跨行 JSON。
- 64 位文件大小、inode 和时间使用十进制字符串，避免 JavaScript 精度损失。
- 文件名同时携带可显示 UTF-8 字符串和原始字节十六进制值，避免 Unix 非 UTF-8 文件名丢失。
- stderr 内容不得被客户端当作协议解析。

## 结果

### 优点

- 保留 Vifm 的成熟代码和测试资产。
- TUI 可以使用响应式组件、布局和类型系统快速迭代。
- 协议边界为未来桌面、远程控制和测试 harness 提供入口。
- core 崩溃与客户端崩溃可以独立诊断。

### 代价

- 需要维护 C 与 TypeScript 两套工具链。
- 需要处理协议版本、进程生命周期和类型漂移。
- 初期存在经典客户端与新客户端的双实现成本。
- headless core 的形成需要逐步拆除 `view_t`、全局状态和 ncurses 耦合。

## 被拒绝的方案

### 全量 TypeScript/Bun 重写

拒绝原因：文件操作、undo、权限、符号链接、平台差异和现有测试资产的迁移风险过高。

### 全量 Rust 重写

拒绝原因：虽然适合新文件管理器，但不符合“以 Vifm 成熟内核为基础”的当前策略，且会延迟首个可用版本。

### 继续扩展 C/ncurses 单体

拒绝原因：会继续扩大 model、任务、布局和 UI 的耦合，不利于会话、插件和多客户端。

### 直接嵌入 Bun 到 Vifm 进程

拒绝原因：生命周期、崩溃隔离、打包和调试复杂度高于本地协议方案。

## 复审条件

出现以下任一情况时复审本 ADR：

- stdio/JSONL 成为可测量的性能瓶颈；
- core 需要多个并发客户端；
- 需要长生命周期 daemon 或远程访问；
- OpenTUI/Bun 无法满足支持平台或分发要求；
- C core 无法在合理成本内形成稳定 headless API。
