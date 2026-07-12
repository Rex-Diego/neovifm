# NeoVifm Hybrid M0 Progress

## 2026-07-11

- 用户批准采用 C core + TypeScript/Bun/OpenTUI client 的混合架构。
- 暂停原单体 C renderer 计划并保留历史记录。
- 初始化 Hybrid M0 计划。
- 新增 ADR 0001，确认 C core + OpenTUI client。
- 定义 `PaneSnapshot v0` JSON Schema 与 JSONL framing。
- 写入 C snapshot/protocol 测试和 TypeScript JSONL/layout 测试。
- RED 已确认：C 缺少 `pane_snapshot.h`；TUI 缺少 `protocol.js` 与 `app.js`。

## 测试结果

| 命令 | 结果 |
|---|---|
| `make -C tests neovifm_snapshot` | RED：预期的缺失实现编译失败 |
| `bun test` | RED：预期的缺失模块失败 |
