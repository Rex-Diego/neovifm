# NeoVifm Linux File Actions Baseline

## 目标

在 B1 Windows session 分支之上，让 Linux session 发布并真实执行 `file-actions-v1`，保持 Workbench Alpha 0 (unreleased) 和 protocol v3 不变。

## 范围

- [x] Linux action queue、undo bridge 和 capability 启用。
- [x] Linux copy/mkdir 复用 parent-FD-relative、no-follow、no-overwrite 实现。
- [x] Linux move 使用 `renameat2(RENAME_NOREPLACE)`，拒绝跨文件系统 fallback。
- [x] Linux delete 使用同目录隔离目录和 `/usr/bin/gio trash`，失败无覆盖恢复。
- [x] Linux identity 统一使用 `st_ctim` 纳秒值。
- [x] focused C 和真实 TUI/core integration 覆盖动作、undo、竞态和恢复。
- [ ] B2a 远端 CI 通过并在 B1 合并后整理为独立 PR。

## 不在范围

- Windows file actions；它属于后续 B2b。
- Linux watcher、Windows watcher/default opener、安装器、release 或 protocol/schema/public API 变化。

## 验收

- focused C：97 tests / 9880 checks 通过。
- Linux TUI/core integration：21 pass / 4 platform skips。
- TUI：145 pass，函数覆盖率 92.09%，行覆盖率 96.85%。
- serial `env -u VIFM -u MYVIFMRC make check`、typecheck 和 audit 通过。
- 远端最终证据以 B1 合并后的新 CI run 为准。
