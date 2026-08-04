# Findings

- `tests/Makefile` 的 `vifm_obj` 会引用全部 Unix `src/neovifm/*.c`，但旧 `appsources` 只构建 `vifm` 与 core probe，导致干净目录缺少 session-only 对象。
- GCC 13 会把 oversized protocol fixture 的 assertion 写法报告为 `maybe-uninitialized`；显式分配失败分支比关闭 warning 更可靠。
- Vifm 上游把 classic local filter 从单个 `filter_t` 改为 `matchers_t`；adapter 应调用公开 filtering API，并保留未初始化 view 的 NULL 防护。
- `nv_fs_remove()`、`nv_fs_move()` 和 undo 文件操作当前只在 macOS 实现；Linux focused suite 不应运行 macOS-only undo 用例。
- Windows `Makefile.win` 已包含 core probe/session 构建目标，但这只能证明可编译，不能证明 persistence、平台 opener 或文件操作完成。
- 当前 GitHub 账号对导师仓库只有 READ 权限；required check 必须由导师设置。
