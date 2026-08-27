# Findings

## Linux filesystem boundary

- 复制和建目录原本已经使用 parent-FD-relative、`AT_*_NOFOLLOW` 和 identity recheck；Linux 缺的是 move/delete 的平台实现与 session capability 入口。
- Linux move 不能用普通 `rename()` 模拟 no-overwrite。当前实现通过 `renameat2` 的 `RENAME_NOREPLACE` 保持原子性；不支持该系统调用时返回 `ENOTSUP`，不退化为 copy-delete。
- Linux delete 与 macOS 同样先进入同目录私有隔离目录，再调用 Trash backend；helper 失败时只恢复原对象，不覆盖并发替换。

## Identity

- `pane_snapshot.c`、`neovifm_fs.c` 和 `undo_bridge.c` 必须使用同一套 Linux `st_ctim` 纳秒 identity；原先 workspace/fs 只到秒，undo bridge 使用纳秒，导致合法 undo 被错误拒绝。
- move/delete 的原子 hook 后增加一次 identity recheck，用于发现同名并发替换。

## Session refresh

- macOS kqueue 会自动掩盖部分 pane refresh 缺口。undo location 现在记录来源和目标 pane/tab，undo 后由 core 显式刷新两侧，不依赖 watcher。

## Environment

- Windows 工作区通过 WSL 构建会被 WSL Git 误报大量行尾变化，因此 Linux 验证使用独立临时 clone；Windows 工作区只保留 Git 真实改动。
- 由于 Linux GCC 不接受 macOS 的 `-Wno-error=gnu-folding-constant`，Linux configure 不带该 flag。
