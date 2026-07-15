# NeoVifm M2 Findings

## 入口约束

- `docs/NEOVIFM_M2_ENTRY.md` 是本次范围依据：仅进程内 task event queue、可取消 text/directory preview 与 task drawer。
- M1 session v2 通过 stdin JSONL command 和 stdout workspace snapshots 工作；M2 需要独立 versioned record，而不能偷改 v2 terminal/command 语义。
- `background.c` 已有线程、取消和 job tracking，但会连接 status/UI/global state；M2 只能复用底层线程/取消模式，不能从 worker 调用它的 UI 路径。
- 预览 identity 使用原始 `path_bytes_hex`，不能依赖净化的 display 文本；path/cwd/pane/generation 必须进入 task context。

## 2026-07-14 Phase 1 盘点

- `background.c::bg_execute()` 会注册全局 job、更新 job bar/status，不能作为 headless M2 worker；M2 queue 必须自有 mutex/condition/event list，worker 只做受限文件 I/O。
- M1 的 `neovifm-core-session` 已具备 stdin/kqueue 主循环和不可变 snapshot 发布；task queue 应只向主循环交付 completed event，再由 session 决定协议 record 和 redraw，不从 worker 写 stdout。
- v3 应把 preview/task 作为独立 record type，不篡改 v2 workspace/command sequence；具体 schema 在 queue API TDD 后确定。

## 2026-07-14 Queue 首切片

- `preview_task.[ch]` 的 queue 是单 worker、mutex/condition 驱动：submit 立即发布 `queued`，worker 发布 `running`，终态为 `done`、`failed` 或 `cancelled`。event 含 task id、pane、generation、kind、cwd/path 原始 hex identity、content/error、OS error 与 truncation。
- text preview 仅允许 regular file，最多 64 KiB；directory preview 只枚举名称到同一上限。路径 hex 解码拒绝 NUL、奇数长度、非法 hex 和超限数据。
- 新 generation 会取消同 pane 的 queued 和 running older generation，取消终态由 main-drained event 明确发布，防止过时结果覆盖。
- `src/Makefile.in` 是本仓库跟踪的 generated build surface；`scripts/fix-timestamps` 只更新时间戳、不重生成内容。修改 `src/Makefile.am` 后必须同步对应 `.in`，再用 `./config.status src/Makefile` 更新本机构建文件。

## 待研究

- 可复用的 pthread/event primitive 与 Windows 等价实现。
- v3 record 顺序、批量合并和 TUI reducer 状态机。
- 文本读取的大小、超时、NUL/控制字符与目录预览上限。
