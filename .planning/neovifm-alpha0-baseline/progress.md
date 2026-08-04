# Progress

## 2026-08-04

- 从 `master@fb8600150` 创建 `codex/neovifm-alpha0-baseline`。
- 将横纵分析 Markdown/PDF 搬到仓库外保存；未触碰既有 `research/total-commander-vifm/` 资料。
- 添加只读 upstream，确认执行时 `upstream/master=6083f5297`，以独立 merge commit 同步 44 个上游提交。
- 干净 Ubuntu/GCC developer 构建复现 `maybe-uninitialized`、缺失 core session 对象、matcher API 变化和 Linux/macOS 能力边界问题。
- 以最小改动修复后，干净 Linux focused suite 通过：`9462 checks / 80 tests`。
- 新增 Linux、macOS、Windows GitHub Actions 和最终 `CI / gate`；远端结果以 Draft PR 为准。
