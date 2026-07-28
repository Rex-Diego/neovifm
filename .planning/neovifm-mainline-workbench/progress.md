# NeoVifm 主线能力继承与任务工作台 Progress

## 2026-07-27

- 使用 `planning-with-files` 恢复当前活动计划与 dirty worktree，确认现有 tab/鼠标/路径改动已经记录完整验收但尚未提交。
- 阅读 Vifm 本地源码/文档与本地参考仓库，确认已有 key engine、后台文件操作、`:jobs`、FUSE archive/sshfs mount、fileviewer/quickview/vcache 和图形 preview 扩展面。
- 确认本地参考包含 `viatc`、`vifm-sixel-preview`、`vifmimg` 和 `yazi`；ViATc 动作表包含打开压缩包、后台传输管理器、快速查看、pane/tabs、selection、history、sync/compare 等候选语义。
- 新建 `neovifm-mainline-workbench` 活动计划，明确 OpenTUI 是默认产品入口，Hybrid 是正式主线架构而非实验客户端，classic ncurses 保留为兼容基准和能力来源。
- 规划阶段顺序：现有改动验收/独立提交 -> 快捷键矩阵 -> Vifm 文件操作/undo -> 后台任务中心 -> archive 目录 -> SSH mount -> F3 富媒体 preview -> 主线联调。
- 按用户反馈将安装/发布后置；所有源码阶段保持 pending，等待用户明确说“可以开始执行”。
- 本轮只修改 `.planning/.active_plan` 与新计划目录，没有修改产品源码、测试、依赖或 Git 历史。
- 完成只读快捷键复核：确认 ViATc 只作为语义/动作目录；将 `F3--F10 + Vifm navigation` 固定为主线按键骨架，并把原生 `e` explore-contents、普通文件 `l -> F3`、enterable resource `l/Enter` 的边界写入计划。
- Phase 0 验证首轮：focused C、TUI unit/coverage/typecheck/audit 全通过；正式 PTY 初次使用 `right-file` 和带方括号的 tab 文本作为同步条件，分别受到初始帧复用、ANSI 增量重绘和 Tcl glob 语义影响。已改为按 core command 顺序发送 `h`/`j`，等待稳定路径/复制结果，并移除脆弱的 tab 文本断言。
- Phase 0 integration 修复后：`bun run test:integration` 通过，7 tests / 51 expects；下一步运行 schema、focused C、TUI 全套和串行 `make check`，再审阅 diff 并独立提交。
- Phase 0 全套验收完成：focused C 9082 checks / 55 tests；TUI unit 106 tests / 377 expects，coverage 85.97% functions / 97.73% lines，typecheck 和 audit 通过；真实 C/PTY integration 7/7；串行 `env -u VIFM -u MYVIFMRC make check` 通过。首次全量回归的 `selection_multi_run` 仅因 macOS shell/error-pipe 回收超过 500ms 测试窗口失败，已将测试 helper 等待上限调整为 5s 并复跑通过；`git diff --check` 通过。
- Phase 0 当前已完成验收并准备独立提交；Phase 1--7 新功能保持未实现、pending。
- Phase 0 独立提交已创建；工作树保持干净，未 push。下一步可从 Phase 1 快捷键/动作兼容矩阵开始。
- Phase 1 已开始：先以现有 `src/modes/normal.c`、`src/engine/keys.c`、ViATc 动作表和当前 `clients/tui/src/keymap.ts` 建立 supported/mapped/conflict/deferred 矩阵，再选择不破坏现有协议的第一批补齐项。
- Phase 1 首个实现切片：新增 `docs/NEOVIFM_KEYMAP_MATRIX.md`，明确 Vifm/ViATc 来源、当前冲突和 archive/remote/高级编辑等 deferred 边界；为 `p/P/d/D` 增加复用 F5/F6/F8 dispatcher 的客户端别名，并先写 RED 单测再实现。搜索、marks、registers、visual、history 等仍等待 core capability，不在本切片伪造客户端语义。
