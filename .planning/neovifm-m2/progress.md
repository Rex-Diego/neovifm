# NeoVifm M2 Progress

## 2026-07-14

- 初始化 M2 持久计划并将 `.planning/.active_plan` 切换为 `neovifm-m2`。
- M1 收尾改动仍在工作树且未提交；M2 将在其上继续，避免清理或覆盖已有改动。
- 当前进入 Phase 1：先建立 v3 任务/预览 DTO 与 TDD 边界，再实现 worker。
- 已盘点 M1 session 与 `background.c`：复用 pthread/取消模式但隔离其 status/UI 全局路径。下一步先写独立 queue 的 C 失败测试，覆盖 done、failed、cancelled 与 generation replacement。
- 已完成 queue 的 RED/GREEN 首轮：先新增 `tests/neovifm_snapshot/preview_task.c`，确认缺少接口导致编译失败；随后实现 `src/neovifm/preview_task.[ch]` 的单 worker、有界 text/directory preview、hex 原始路径 identity 和 lifecycle event。
- queue 现在在同 pane 新 generation 入队时取消 queued 与 running 的旧任务，并从 worker 以 immutable event 交给主线程；worker 不接触 ncurses、`view_t` 或 stdout。
- focused `make -C tests neovifm_snapshot` 通过：30 tests / 8545 checks。构建元数据已同步到 Unix/Windows Makefile 表面，且本机 configure 基线已恢复。
- 下一步：为 session 定义并实现 v3 JSONL task/preview records、10--16ms drain，并将 queue 接入 active cursor 的显式 preview 请求。
- session 现已切换到 v3：启动和 cursor command 自动提交带 pane/cwd/raw path/generation 的 preview，主线程从 queue drain `task` 与终态 `preview` JSONL；macOS kqueue 等待窗口设为 12ms。TUI parser/reducer/client 和宽终端 preview/task drawer 已接通，窄屏保留活动 pane。
- 新增真实 C v3 session integration，验证任务 lifecycle 与 text preview；待补足 timeout/目录 preview/cancellation 的端到端覆盖，再跑全量验收。
- 补齐 queue 的 directory、queued-generation cancellation、missing-path failure 与 expired deadline timeout 测试；文本 preview 在 C/JSON 边界将非安全字节替换为 ASCII `?`，避免 stdout 协议注入无效 UTF-8/control bytes。
- 完成 serial `make check`（含 33 个 NeoVifm focused C tests），TUI unit/coverage/typecheck 与 C probe/session integration 均已通过。`bun pm scan` 因项目未配置 Bun scanner 无法运行，`npm audit` 因只有 `bun.lock` 没有 npm lockfile 无法运行；未为审计工具引入无关 lockfile。
- text worker 以 `O_NONBLOCK` 打开候选路径并在 `fstat()` 后只接受 regular file，新增 FIFO 测试确保恶意/过时 path 不会卡住唯一 worker；focused C suite 现为 34 tests / 8596 checks。
