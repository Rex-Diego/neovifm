# NeoVifm Hybrid M0 Findings

## 已确认

- OpenCode 当前核心为 TypeScript/Bun monorepo，TUI 使用 OpenTUI + SolidJS。
- 值得吸收的是逻辑 client/server、事件流、session/task 和版本化 SDK。
- NeoVifm 的文件系统与操作内核继续使用 C，OpenTUI 作为新客户端验证。
- 原先的 `PaneSnapshot` 仍保留，但升级为跨进程 DTO。

## 待确认

- OpenTUI 当前最小启动 API和测试方式。
- C probe 最合适的复用层：`compat`、`utils/fs` 或现有 filelist。
- JSON Schema 到 TypeScript/C 的生成与校验是否值得在 M0 引入工具。

## 当前决策

- 本机已有 Bun 1.3.14，可直接建立官方 OpenTUI 0.4.3 原型。
- C 端已有 Parson、`os_opendir/os_readdir/os_lstat` 和 FileType 转换能力。
- M0 使用 `hello -> snapshot|error -> EOF` 的 JSONL 流。
- stdout 只传协议，stderr 只传诊断。
- 64 位数值用十进制字符串；路径同时传 display UTF-8 与 raw bytes hex。
