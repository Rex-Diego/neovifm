# Progress

## 2026-08-08

- 本地 `master` 快进到导师 `58f26509`，创建 `codex/neovifm-alpha0-windows-baseline`。
- fetch 并以 `chore: sync vifm upstream` 独立 merge commit 同步 `upstream/master@f5d60eaa`；上游提交是当前分支祖先，upstream push URL 为 `DISABLED`。
- 完成 Windows UTF-16 core 参数、状态路径、原子替换、Unicode 状态读取、stdin pipe 轮询和 `.exe` 默认 core 路径。
- Windows focused C 逐 fixture 真实运行；macOS-only action/undo 和 POSIX-only helper 用例按平台边界隔离。
- 新增 Windows real-core integration，覆盖 Unicode probe/session、空闲初始 preview、搜索、pane、tabs、排序、refresh、默认状态创建、二次替换和跨进程恢复；hello 不包含 `file-actions-v1`。
- GitHub Actions run `31243077034` 通过 Linux、macOS、Windows 和 `CI / gate`。Windows real-core integration 为 `5 pass / 2 files`；TUI 为 `145 pass / 11 files`，函数覆盖率 `94.44%`，行覆盖率 `98.33%`。
- 已知限制：Windows/Linux 文件操作、Windows watcher/default opener、安装器和公开 Alpha 发布留到后续阶段。
