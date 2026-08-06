# Progress

## 2026-08-04

- 从 `master@fb8600150` 创建 `codex/neovifm-alpha0-baseline`。
- 将横纵分析 Markdown/PDF 搬到仓库外保存；未触碰既有 `research/total-commander-vifm/` 资料。
- 添加只读 upstream，确认执行时 `upstream/master=6083f5297`，以独立 merge commit 同步 44 个上游提交。
- 干净 Ubuntu/GCC developer 构建复现 `maybe-uninitialized`、缺失 core session 对象、matcher API 变化和 Linux/macOS 能力边界问题。
- 以最小改动修复后，干净 Linux focused suite 通过：`9462 checks / 80 tests`。
- 新增 Linux、macOS、Windows GitHub Actions 和最终 `CI / gate`；远端结果以 Draft PR 为准。

## 2026-08-05

- Windows 干净构建真实生成并检查 `vifm.exe`、`neovifm-core-probe.exe` 和 `neovifm-core-session.exe`，现有 C tests 全部通过。
- 将 TUI core-client 的 POSIX shell 假进程替换为测试期编译的本机 Bun 可执行文件；Linux 和 Windows 均为 `19 tests / 34 checks` 通过，未跳过 Windows 用例。
- TUI 完整结果为 `145 tests / 551 checks`，函数覆盖率 `92.09%`、行覆盖率 `96.85%`；typecheck 和 `bun audit` 通过。
- 远端排错依次处理 Windows open flags、core 链接依赖、UTF-8 shim、源文件隔离、fixture CRLF、runner 路径假设、假 core 可移植性和 Git 实现差异；失败 run 为 `30925030634`、`30925382314`、`30925789215`、`30926171101`、`30926568774`、`30928833584`、`30963725547`。
- `aumi314/neovifm` 的 run `30964147637` 在 `10315e1e7` 上通过 Linux、macOS、Windows 和 `CI / gate`；Draft PR 为 `Rex-Diego/neovifm#1`。
