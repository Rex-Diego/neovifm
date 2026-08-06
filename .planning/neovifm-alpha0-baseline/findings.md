# Findings

- `tests/Makefile` 的 `vifm_obj` 会引用全部 Unix `src/neovifm/*.c`，但旧 `appsources` 只构建 `vifm` 与 core probe，导致干净目录缺少 session-only 对象。
- GCC 13 会把 oversized protocol fixture 的 assertion 写法报告为 `maybe-uninitialized`；显式分配失败分支比关闭 warning 更可靠。
- Vifm 上游把 classic local filter 从单个 `filter_t` 改为 `matchers_t`；adapter 应调用公开 filtering API，并保留未初始化 view 的 NULL 防护。
- `nv_fs_remove()`、`nv_fs_move()` 和 undo 文件操作当前只在 macOS 实现；Linux focused suite 不应运行 macOS-only undo 用例。
- Windows `Makefile.win` 已包含 core probe/session 构建目标，但这只能证明可编译，不能证明 persistence、平台 opener 或文件操作完成。
- Windows core 目标原有依赖清单不完整：probe/session 缺少 open resolver、UTF-8 宽度兼容和部分 filesystem 对象；classic `vifm.exe` 也需要显式链接 NeoVifm filesystem compatibility。
- Windows runner 的 checkout 会把文本转换为 CRLF。`tests/test-data/**` 是按字节断言的 fixture，必须以 `-text` 检出；否则文件大小测试会分别多出 3 和 9 字节。
- `commands/selection.c` 的 `*/a/**` 会误匹配 GitHub Windows runner 根目录 `D:/a/...`；测试必须用当前 sandbox 的完整路径表达目标，不能假设父目录不叫 `a`。
- TUI core-client 的旧测试夹具直接执行临时 `.sh`，Windows Bun 无法启动。测试期编译的本机 Bun fake core 可以在三平台保留相同的协议、超时、取消、stderr 和 session 断言。
- Windows checkout 由 Git for Windows 管理，最终 `git diff --check` 也必须使用同一 Git；改用 MSYS2 Git 会把正常 CRLF 误报成整仓尾随空白。
- 当前 GitHub 账号对导师仓库只有 READ 权限；required check 必须由导师设置。
