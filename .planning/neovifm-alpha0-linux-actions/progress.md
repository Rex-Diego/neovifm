# Progress

## 2026-08-08

- 将 PR #2 `fix: establish Windows session baseline` 标记为 Ready for review。
- 从 `codex/neovifm-alpha0-windows-baseline@b3712239` 创建并推送 stacked 分支 `codex/neovifm-alpha0-linux-actions` 到个人 fork。
- `fix: enable Linux safe file actions`：Linux session 启用 action queue、undo bridge 和 `file-actions-v1`；Linux move 使用 `renameat2(RENAME_NOREPLACE)`，delete 默认使用 `gio trash`。
- `test: run Linux file action integration`：解除 Linux action/undo 测试的平台误隔离，加入 Trash 失败恢复测试，并让 Linux real-core undo integration 执行。
- 修复 Linux snapshot、filesystem 和 undo bridge 的 `ctime` 精度不一致；undo 成功后刷新来源和目标 pane/tab，避免 macOS watcher 掩盖 Linux stale workspace。
- 临时 WSL clone 验证：focused C `97 tests / 9880 checks`；TUI integration `21 pass / 4 skip`；TUI `145 pass`、函数/行覆盖率 `92.09%/96.85%`；serial `make check`、typecheck、audit 通过。
- 当前远端分支已推送到 `aumi314/neovifm`；B1 合并前不创建重复 PR。
