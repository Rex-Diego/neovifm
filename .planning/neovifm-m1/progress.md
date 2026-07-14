# NeoVifm M1 Progress

## 2026-07-14

- planning-with-files session catchup 已确认上一轮 watcher 交付与验证已写入工作树；`git diff --stat` 显示的是既有 M0/M1 未提交基线及 watcher 变更，未发现额外外部改动。当前继续把两个已加载 classic `view_t` 原子转换为 workspace snapshot 的独立适配层，仍不让 headless session 持有 view 指针。
- 新增 classic workspace adapter 与原子失败回归：它深拷贝左右已加载 view，并在右 pane 无效时保留上一次完整 workspace。focused `neovifm_snapshot` 通过（26 tests/8472 checks）；用户随后要求提交并推送，将把现有 M0/M1、协议、TUI、测试和计划上下文一并提交。
- 已发布 `9aad055ea feat: establish neovifm hybrid m1` 至 `origin/master`。生成的 `src/neovifm-core-session` 二进制未纳入版本控制，保留在工作树供本地 integration 使用。
- 已按 planning-with-files 恢复活动计划；session catchup 无待同步上下文，重读 M1 计划、发现记录、架构文档和 HACKING.md。当前继续 Phase 4 的 macOS kqueue watcher 实现前核查。
- TDD：新增真实 session integration，先在 left 目录创建文件并要求无需 `refresh` command 即收到保持同一 command sequence 的 workspace；旧 session 在 5 秒内超时，确认 watcher 缺失。新增 v2 trigger/parser/reducer/schema 测试后，旧实现按预期失败（缺少 `trigger`，reducer 拒绝相同 command sequence，schema 未声明 trigger）。
- 构建命令首次从 `clients/tui` 目录错误指向不存在的 `src/`（`make -C src neovifm-core-session`）；将改用仓库根 `make -C src neovifm-core-session`，不重复该错误路径。
- macOS kqueue watcher 已实现：stdin 和 left/right cwd 在同一 kqueue；watcher refresh 以完整 v2 workspace 和 `trigger: watch` 发布，保留最后确认 command sequence；导航后重开对应 cwd FD，单 pane watch 失败仅写 stderr 并停用该 watcher。schema、C serializer、TypeScript parser/reducer 与协议文档同步为 `initial|command|watch` trigger。
- 验证中 `bun test` 会把 integration 目录作为普通测试运行，但没有 `NEOVIFM_CORE_PROBE`/`NEOVIFM_CORE_SESSION` 环境变量而失败；这是命令选择错误，不是实现回归。后续使用 package scripts：`bun run test`（unit）与 `bun run test:integration`（注入两个 built core 路径），不再直接运行无环境的全目录 `bun test`。
- TUI unit（52 tests/113 assertions）和 coverage 通过（88.31% functions、93.62% lines）；随后 typecheck 因 schema 测试直接读取 `unknown` 的 `properties.trigger` 报 TS2571。已将该测试收窄为 `objectValue` 后读取，继续执行剩余验证。
- 修正后 TUI typecheck、真实 probe/session integration（4/4）和 `bun audit` 均通过。最后一个 `make check` 被错误地从 `clients/tui` 运行，因该目录没有 C Makefile 而报 `No rule to make target 'check'`；改从仓库根串行运行，避免重复该路径。
- watcher 实现最终验证通过：focused `neovifm_snapshot` 为 25 tests/8461 checks；TUI typecheck、真实 probe/session integration 4/4 与 `git diff --check` 通过。串行 `env -u VIFM -u MYVIFMRC make check` 通过（src runtests 1/1）。Phase 4 的 macOS watcher 子项已完成；真实 Vifm pane/filelist adapter、排序/过滤 snapshot 与 classic 默认 renderer 回归仍待后续阶段处理。
- 已完成 security-review：受限 JSONL command 的大小、版本、action、sequence 和 path 边界均受 C/TypeScript 双侧校验；kqueue 只刷新 snapshot，不执行 shell/网络操作；client 对流量有资源上限，`bun audit` clean。未发现 HIGH/CRITICAL 问题。Phase 4 仅余真实 classic pane/filelist 接入与排序/过滤 DTO，Phase 5 验证与审查子项已完成。
- 根据用户对 Vifm/Total Commander 双窗口体系的明确要求，重新激活 M1 并将其改写为 Hybrid 双 pane 工作台计划。
- 已核实 M0 仅完成单目录 snapshot 原型；M1 将从双 pane DTO、C core、OpenTUI 默认双列表、独立导航和真实 pane adapter 顺序推进。
- 当前工作树含未提交的 M0 变更，必须作为 M1 基线保留；不得清理、重置或将单 pane M0 误报为产品完成。
- 已完成 C/TUI 双侧架构审查：M1a 将先以 v1 原子 workspace DTO 交付真实双列表默认界面；M1b 再进入受 TUI 持有的只读交互 session。直接复用 `view_t` 已被排除，因为其 ncurses、watcher 和进程级 cwd 耦合违反 headless core 约束。
- M1a 已完成：单目录 `neovifm-core-probe <path>` 继续发布 v0；双目录 `neovifm-core-probe <left> <right>` 发布 `workspace-v1` 和原子 `workspace-snapshot`。TUI 宽屏为左右列表，窄屏为 active pane，并支持 Tab 切换本地 focus。
- M1a 验证已完成：focused C suite 22 tests/8408 checks；TUI unit 49 tests/107 assertions、coverage 89.44% funcs/98.38% lines、typecheck、integration 3/3、audit 和 `git diff --check` 均通过；串行 `env -u VIFM -u MYVIFMRC make check` 通过。
- 当前进入 M1b：必须新增由 TUI 持有的 stdio session 和独立 pane 状态，才能让 Tab、cursor、进入目录、返回上级、selection 均由 C core 的不可变 workspace 事实源驱动。不得把 M1a 的静态 Tab 视觉焦点误报为完整 Vifm 双 pane 导航。
- M1b C 基础已开始：新增 `workspace_session`，拥有两个 snapshot 和 active pane，已覆盖独立 cursor/selection/焦点；新增 `neovifm-core-session` v2，hello 后发布初始 workspace，并能从 stdin 接受 focus/move/enter/parent/toggle-selection 命令后发出新的完整 workspace。
- 构建过程中，测试链接缺少 `workspace_session.o`；原因是新的 C module 尚未列入 `src/Makefile.am`。已加入 core probe 源闭包并运行 `automake`，focused suite 恢复通过。下一步是 v2 TypeScript session client/reducer/UI，不能让现有 v0/v1 one-shot client 接管这个长连接。
- 后续 focused suite 首次链接因 `tests/Makefile` 的 wildcard 将新的 executable `core_session.c` 也作为测试库源，产生 duplicate `main`；已按既有 `core_probe.c` 规则过滤该入口文件，正重新验证。
- 已重新验证：focused C suite 为 23 tests/8435 checks，v2 session 的 initial + focus 命令 JSONL 手工端到端校验通过；`automake` 生成的 `Makefile.in` 有两行尾空格，已清除，`git diff --check` 通过。
- M1b TypeScript 已开始接入：protocol parser 支持 v2 session snapshot/command-error，session reducer 保留上一份 workspace 并只接受递增 command sequence；OpenTUI 不再本地伪造 Tab 状态，而是发送 core command、等待新 DTO；入口默认启动 `neovifm-core-session`。unit/typecheck 和 focused C suite 均通过（TUI 50 tests/106 assertions）。
- 尚未完成 M1b 验收：需要补 v2 schema/README、真实 Bun stdin/stdout session client integration、Windows target、watcher/真实 classic pane adapter 和完整回归；这些未完成项保留在 Phase 4/5。
- 已补 v2 schema、协议 README 和 `Makefile.win` session target；Windows target dry-run、focused C suite、TUI unit/typecheck 与 `git diff --check` 均通过。下一步应增加真实 session client integration，再处理 watcher/adapter 的架构边界。
- 已增加真实 Bun/C session integration：启动 built `neovifm-core-session`，验证初始左右 workspace、focus right、move cursor 后 left pane 完全不变；`bun run test:integration` 现同时提供 probe 与 session 环境变量并通过 4/4。M1b session 子项已完成，Phase 4 余下 watcher/真实 adapter 仍未完成。
- 已为 v2 schema 增加 TypeScript 契约测试，验证 session workspace 需要 command acknowledgement；typecheck 与 diff check 通过。对 `view_t`/`filelist.c` 再次核查表明其仍混合 ncurses、watcher、全局 cwd 与可变 entries，不能直接导入 headless session；下一步必须是独立 read-only classic pane adapter，而不是复用 `change_directory()`。
- session client 现并发 drain stderr（64 KiB 上限），避免 core 诊断大量输出时阻塞 stdout/命令循环；真实 session integration 和 typecheck 仍通过。
- 已定位 classic adapter 的可测入口和严格边界：只读 `view_t` 复制可行，复用 `filelist.c` 加载/导航不可行；后续实现将用 test-support 的 `view_setup/populate_dir_list` 验证而不调用 ncurses 或进程 chdir。
- 已实现 `classic_pane_adapter`：把已加载的 `view_t` 深拷贝成 owned snapshot，复制 cwd、cursor、entry metadata、type、selection、隐藏状态和原始身份；不保留任何 view/entry 指针，也不加载目录或改变 cwd。focused suite 的 adapter deep-copy 测试通过（24 tests/8443 checks）。尚未把这个 classic-only adapter 接入 headless session，避免把 ncurses view 状态跨 C/TS 边界泄漏。
- 为 watcher 前的可验证刷新语义新增受限 `refresh` command（TUI `r`）：C core 只重建 active pane snapshot，仍以完整 v2 workspace 发布；真实 integration 在外部创建文件后验证 refresh 更新 left pane。它不是 watcher，Phase 4 的后台监测/增量事件仍待实现。
- 当前完整回归通过：串行 `make check` 通过；TUI coverage 88.30% funcs/93.62% lines（51 tests/108 assertions）、integration 4/4、typecheck、audit 与 `git diff --check` 均通过。由于 watcher/真实 adapter 仍未接入，Phase 5 不能整体完成。
- 修复 refresh 的状态保持：重建 active pane 时按原始 name identity 保留 selection 和 cursor；focused C suite 仍通过（24 tests/8443 checks）。
- 用户已明确 M1 先以 macOS 为交付目标；Windows/其他平台 watcher 延后。下一步可使用 macOS FSEvents 或 kqueue，但仍须保持 session stdout 协议、独立 pane 上下文和无 UI-thread 写入约束。

## 2026-07-11

- 初始化 M1 持久计划。
- 当前进入 Phase 1：数据契约与测试。
- 阅读 `view_t`、`dir_entry_t`、经典绘制入口和测试框架。
- 初步确定 snapshot 采用独立最小结构，不复制内部缓存和所有权标志。

## 测试结果

| 命令 | 结果 |
|---|---|
