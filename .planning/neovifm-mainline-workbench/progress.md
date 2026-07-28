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
- Phase 0 已完成验收并独立提交；Phase 1 已进入快捷键兼容切片，Phase 3 已进入后台任务队列首切片，其余能力仍按阶段推进。
- Phase 0 独立提交已创建且未 push；后续 Phase 1/3 改动分别保持独立提交，当前工作树状态以最新验收记录为准。
- Phase 1 已开始：先以现有 `src/modes/normal.c`、`src/engine/keys.c`、ViATc 动作表和当前 `clients/tui/src/keymap.ts` 建立 supported/mapped/conflict/deferred 矩阵，再选择不破坏现有协议的第一批补齐项。
- Phase 1 首个实现切片：新增 `docs/NEOVIFM_KEYMAP_MATRIX.md`，明确 Vifm/ViATc 来源、当前冲突和 archive/remote/高级编辑等 deferred 边界；为 `p/P/d/D` 增加复用 F5/F6/F8 dispatcher 的客户端别名，并先写 RED 单测再实现。搜索、marks、registers、visual、history 等仍等待 core capability，不在本切片伪造客户端语义。
- Phase 3 首个实现切片：`nv_action_queue` 改为 64 项有界 FIFO，保留 queued/running/terminal 生命周期并按 task id 维护多个 action refresh context；TUI 不再因已有 action 忙碌而禁用后续 copy/move，右下角 `Tasks` 入口打开可滚动 Queue/History 覆盖层。新增 C queue、TUI、真实 core session 回归，验证两个连续 copy 的 done/failed 历史和 destination-exists 语义。
- 本切片验收：`make -C tests neovifm_snapshot` 9085 checks / 55 tests；TUI 109 tests / 391 expects，coverage 85.95% functions / 97.65% lines，typecheck、audit、schema、`git diff --check` 通过；真实 integration 8 tests / 57 expects；串行 `env -u VIFM -u MYVIFMRC make check` 通过。Phase 2 的 native undo/background bridge 尚未宣称完成。
- 真实队列回归暴露 macOS `select()` + buffered `fgets()` 会把已写入管道的第二条命令藏在 stdio 缓冲区，导致 FIFO 测试偶发只处理一个 action；core session 现将 stdin 设为 `_IONBF`，连续命令和 full integration 复跑通过。

## 2026-07-28

- Phase 2 mkdir/undo 首切片先写 RED：新增 bridge 单测覆盖空 undo、成功删除和 replaced directory 拒绝；实现后 `9116 checks / 57 tests` 通过。
- `src/undo.c` 已通过窄兼容边界链接到 core session；bridge 只记录成功 mkdir，保存 parent/child no-follow identity，并在主线程执行 `OP_RMDIR`。copy/move/delete 的 `u` 请求返回明确 `undo-empty`，不伪装成已支持的 destructive undo。
- undo 记录携带 source pane/tab，`u` 只刷新原始 tab；真实键盘集成覆盖“在第二个 tab mkdir、切回第一个 tab、u、再切回第二个 tab 验证目录消失”，避免 inactive tab snapshot 陈旧。
- identity 测试改用与产品实现一致的 macOS/Linux 纳秒级 ctime；core-only `undo_compat.c` 改用 `nv_lstat`，普通测试对象图继续使用 Vifm 原有 utility 实现。
- 当前 slice 验收完成：core session build、focused C `9116 checks / 57 tests`、TUI unit `111 tests / 393 expects`、coverage `85.95% functions / 97.65% lines`、typecheck、audit、schema、真实 integration `8 tests / 60 expects`、串行 `env -u VIFM -u MYVIFMRC make check` 和 `git diff --check` 全部通过；待独立提交。
- 已知限制：undo 仍是单个 `core_session` 进程内的 classic 全局栈；仅支持 mkdir，记录失败只写 stderr，尚未向 task event 发布 undo availability；copy/move/delete、redo、取消/重试和 Vifm background facade 继续留在后续切片。
- 使用 `planning-with-files` 接收新一轮交互反馈，本轮仅更新计划文件，没有修改产品源码、测试、依赖或 Git 历史。
- 新增 Phase 1A：Space 从 Tab 语义中拆出，改为不污染目标 pane 状态的对面 pane 临时预览；同时规划目录优先排序、三行底栏、单 cell 滚动条、右上 active 标记、低对比 active sort、明显的新增 tab 按钮、彩色权限、owner/group 和近期修改色。
- 新增 Phase 1B：明确现有后缀/MIME 打开方式由 Vifm `filetype/filextype/fileviewer` 与 `running` 管理；OpenTUI 不新增平行映射，macOS 未显式配置时通过结构化 argv 交给 `/usr/bin/open`。
- 为两个新阶段分别写明 TDD、协议/安全边界、60/80/100/160 列与 ASCII/低色彩降级、真实 core/PTTY 验收和独立提交门槛；实现状态保持 pending。
- UI Phase 1A 首个实现切片已落在共享工作树：`clients/tui` 已分离 Space/Tab，加入临时对面 pane preview、三行底栏、单 cell scrollbar、右上 active 标记、低对比 sort header、明显新增 tab、权限语义色、近期修改色和可选 owner/group 列；相关 TUI tests/typecheck/audit/diff-check 通过。
- 该 UI 切片仍未宣称 Phase 1A 完成：core 尚未发布独立的 source-pane/target-pane preview intent，目录优先排序也仍需在 C comparator 完成；因此继续保持 pending。

## 2026-07-28 Phase 1A TUI slice

- TUI keymap now keeps Tab as `focus-next` and maps Space to a UI-local `quick-view` action; Space no longer aliases pane switching.
- Added ephemeral opposite-pane quick preview rendering. The source pane remains active and the destination pane's tabs/cwd/selection/snapshot are untouched; Tab or Esc closes the temporary render. Narrow layouts fall back to the existing full-workspace viewer.
- Added stable status/divider/function-bar bottom layout, removed the root spacer, configured one-cell OpenTUI scroll thumbs without arrows, moved active markers to the pane header right edge, softened active sort backgrounds with rounded side borders, and enlarged the new-tab plus target with an ASCII fallback.
- Added semantic permission tokens, hour/day/older mtime buckets, recent-name/mtime colors, additive bounded owner/group display fields, and responsive owner/group columns for wide detailed layouts.
- Added RED/GREEN coverage for Space/Tab, quick preview lifecycle, owner/group protocol fields, semantic permission tokens, mtime buckets, and wide owner/group rendering.
- TUI verification passed: `bun run typecheck`; `bun test ./test/` (115 tests, 409 expects); `git diff --check`.
- Remaining Phase 1A boundary: the C session currently auto-previews only the active pane. Space consumes the matching active preview as a safe temporary render, but a dedicated source-pane/target-pane preview intent and core generation routing remain pending for the full protocol slice.

## 2026-07-28 Phase 1B opening fallback slice

- Added `clients/tui/src/open-file.ts` as a structured argv boundary. macOS uses absolute `/usr/bin/open`; Linux and BSD-like platforms use `xdg-open`; Windows uses `explorer.exe`.
- Added optional `openCommandForAssociation()` for a future core-resolved Vifm association. The client appends the target as one argv item and rejects empty/NUL-containing arguments; it never invokes a shell or interprets an extension map.
- Wired `App`/`index` so `l`/Enter on regular files uses the injected opener when available; standalone renderer tests retain the F3 preview fallback when no opener is supplied. Directories still go through the core `enter` command.
- Added RED/GREEN tests for platform argv, association argv, invalid paths, opener spawn/exit behavior, and App Enter routing. `bun run typecheck` and `bun run test:coverage` pass: 121 tests / 419 expects, 85.30% functions / 97.42% lines.
- Phase 1B remains pending: Vifm `filetype/filextype/fileviewer` matching, macro expansion, archive/remote enter capability and core-owned association resolver are not yet wired into the session protocol.

## 2026-07-28 Phase 1A core/protocol and task-center slice

- Added RED/GREEN coverage for directory-first sorting. `nv_pane_snapshot_sort()` now compares a direction-invariant real-directory group before the selected Name/Size/Time/etc. key; descending order only reverses entries within each group. Symlinks remain non-directory until core publishes an enterable-directory capability.
- Extended preview task/request/event DTOs with additive `target_pane`. Existing callers that omit it normalize target to source; queue cancellation is keyed by render target so source-left -> target-right and source-right -> target-right generations supersede each other without cancelling source-lane previews.
- Added v3 `preview` command with explicit source pane, target pane, cwd/entry raw identities, snapshot revision and stat identities. Core validates the immutable source snapshot, acknowledges the command with an unchanged workspace snapshot, then queues a new generation without mutating target cwd/tab/selection/history.
- TUI protocol parser defaults missing target fields to source for compatibility and probe-state generation filtering uses target lanes. App Space quick-view now submits the command and re-submits when source snapshot/cursor identity changes; the opposite pane remains ephemeral and narrow layouts keep the full viewer fallback.
- Task Center queue/running rows now expose a mouse cancel target and send the core-owned `cancel-action` command; history rows remain non-cancelable.
- RED/GREEN verification: `make -C tests neovifm_snapshot` passed 9262 checks / 63 tests; `make -C src neovifm-core-session` passed; TUI focused app/protocol/probe/keymap tests passed 102 tests / 247 expects; full TUI coverage passed 123 tests / 429 expects at 86.56% functions and 97.43% lines; audit, typecheck, core-session integration (2 tests / 17 expects), serial `env -u VIFM -u MYVIFMRC make check`, and `git diff --check` passed. A previous watcher-only assertion was removed from the core-session fixture because piped stdin cannot register kqueue; explicit refresh still covers the same snapshot update path.

## 2026-07-28 Phase 1B core open resolver/session slice

- Added a C-owned `nv_open_resolve()` boundary with bounded structured argv. An explicit association prefix wins; an empty association selects macOS `/usr/bin/open` or the platform `xdg-open` fallback. The resolver intentionally does not duplicate Vifm extension/MIME tables.
- Added v3 `open-v1` hello capability, `open` resolved records, schema definitions, TypeScript validation/reducer support, and an integration test that sends a real session command and observes the structured association result.
- Added resolver and JSON protocol unit tests. Focused C verification passed `9263 checks / 63 tests`; focused TypeScript/protocol/reducer/open-file tests passed `60 tests / 146 expects`; real core-session integration passed `3 tests / 20 expects`.
- The shared UI edits that originally blocked `bun run typecheck` have since been reconciled; the current client typecheck passes. Current shared-worktree `git diff --check` passes.
- Remaining Phase 1B boundary: load and match Vifm `filetype/filextype/fileviewer` rules with macro expansion, validate snapshot/pane identity for open targets, launch/monitor external programs through the background lifecycle, and add archive/remote enterable-resource capabilities.

## 2026-07-28 Integration regression repair

- The first full TUI integration run exposed two post-Phase-1A mismatches: the compact function bar hid `F3 View`/`F10 Quit` after a resize, and the keyboard integration still asserted the old Space-as-pane-switch behavior.
- Function labels now derive their narrow form reactively after terminal resize; 60-column output keeps `F3 View` and `F10 Quit` readable while abbreviating middle buttons.
- The real keyboard integration now asserts the new contract: Space requests an opposite-pane preview without changing `active_pane`, and Tab performs the pane switch. Multi-key keymap state is held through a stable memo so asynchronous preview/task records cannot reset Vifm prefixes.
- Focused verification passed: compact core-probe integration, real keyboard session integration, app/keymap tests, and TUI typecheck.

## 2026-07-28 Core open result client bridge and PTY repair

- Connected the v3 core-owned `open` record to the OpenTUI app. Regular-file `l`/Enter now sends a structured `open` intent when `open-v1` is advertised, and the client launches only the resolved argv; the injected platform opener remains the renderer-test fallback.
- Added client coverage for resolved argv execution, NUL validation, and App routing. The core record is deduplicated by `command_sequence` and reports launch failure through the existing notice path.
- The post-layout production PTY regression was caused by the status line moving up after removing the spacer; mouse status targets now use the new row. Standalone production PTY and the real keyboard integration pass after the coordinate update.
- Verification after this slice: `bun run typecheck`, focused app/keymap/open-file tests, compact core-probe integration, real keyboard integration, and standalone production PTY all pass. Full integration and the next C metadata slice remain to be rerun.

## 2026-07-28 Bounded owner/group metadata slice

- Added additive `owner_display` and `group_display` fields to the immutable pane entry DTO, C snapshot ownership/free paths, JSON serialization, v0/v3 schemas, and TypeScript validation. Values are capped at 256 UTF-8 bytes and sanitized before display.
- Core metadata lookup deliberately avoids NSS/Directory Services in the snapshot path. It performs a bounded local `/etc/passwd` or `/etc/group` lookup and falls back to numeric uid/gid when the identity is remote, unavailable, malformed, or overlong.
- Added C snapshot/protocol assertions and a TypeScript upper-bound regression. `make -C src neovifm-core-session` and `bun run typecheck` pass; the focused protocol suite passes 20 tests / 51 expects. A full C test rerun is pending because earlier diagnostic processes are stuck in macOS uninterruptible I/O.

## 2026-07-28 Markdown and PDF preview slice

- Added additive preview kinds `markdown` and `pdf`. Markdown reuses the bounded cancellable text reader and renders through OpenTUI's `MarkdownRenderable`, so headings, lists, fenced code, and tables are no longer shown as an unformatted blob.
- PDF preview is a first-page text extraction path using a detected absolute `pdftotext` helper. The core worker passes argv directly, caps output, polls for cancellation/deadline, terminates the helper on cancellation/timeout, and publishes helper-unavailable/non-zero errors as structured preview failures.
- Added v3 schema/parser coverage for both kinds. `make -C src neovifm-core-session`, `bun run typecheck`, and the focused protocol suite (20 tests / 53 expects) pass. Image protocol rendering, PDF graphical pages, and audio/video metadata remain Phase 6 follow-up slices.

## 2026-07-28 Task history visibility cleanup

- Task Center now offers a clickable `Clear` action for completed/failed/cancelled rows. It only filters the current overlay view; core task events and the session reducer remain intact, so no task facts are deleted or hidden from a future refresh.
- The queue remains cancelable through the core-owned `cancel-action` command. Retry still requires preserving validated action identities beyond terminal acknowledgement and remains intentionally pending.

## 2026-07-28 task center details and open target identity

- Task Center Queue/History now supports selecting a terminal row and rendering a bounded details panel with task id, command sequence, action, pane, state, progress, failed item, error code and OS error. Failed/cancelled rows expose an explicit disabled `Retry unavailable` state because the current core protocol does not retain enough immutable source/destination identity for a safe retry; no fabricated retry command is sent.
- OpenTUI `open` commands now carry source pane, cwd/snapshot identity and entry device/inode/ctime. `core_session` validates those fields before resolving or publishing an external argv, rejects stale targets with `stale-open`, and refuses directories with `enter-required`.
- Added the bounded caller-supplied Vifm association resolver slice in `src/neovifm/open_resolver.c`: ordered filetype/filextype/fileviewer rules, basename/full-path globs, quoted argv tokenization, `%f/%c/%%` expansion and shell/control/macro validation. It is deliberately not presented as full Vifm config integration; the caller still needs to load the classic association source.
- A stale-open integration case now proves that changing `snapshot_revision` cannot trigger an external association. The production PTY preview assertion uses the stable `ins` content fragment because ANSI incremental redraw streams can consume a literal `i` as a control-sequence terminator; the navigation and preview content assertion remains intact.
- Verification after these slices: `make -C tests neovifm_snapshot` 9309 checks / 68 tests; serial `env -u VIFM -u MYVIFMRC make check` passed; TUI unit 128 tests / 461 expects, coverage 86.71% functions / 97.17% lines, typecheck and audit passed; full `bun run test:integration` passed 10 tests / 69 expects after explicit per-test scheduling budgets for PTY/core-session; `git diff --check` and JSON schema validation pass.
- The latest shared worktree remains uncommitted by request. Archive-as-directory, SSH/sshfs, full Vifm association loading/lifecycle, destructive undo/retry and full low-color/width matrix remain pending rather than being marked complete.

## 2026-07-28 MYVIFMRC bounded association loading

- Extended the core open resolver with a bounded `MYVIFMRC` loader for `filetype`, `filextype`, and `fileviewer`. It preserves declaration order, supports continuation lines and brace-separated glob patterns, skips MIME selectors, and keeps the first comma-separated command candidate.
- `nv_open_resolve()` now loads the configured file when no explicit association argv is supplied; explicit structured argv remains higher precedence. Missing/oversized/malformed configuration is reported as a structured resolver error rather than silently executing an unintended fallback.
- The loader owns copied rule strings and keeps the existing shell-free argv tokenizer and `%f/%c/%%` boundary. Complex Vifm shell commands, MIME matching, later candidates and unsupported macros remain explicit follow-up work.
- Added focused config/env/error coverage and wired `open_config.c/.h` into the core build. This slice is still bounded configuration adaptation, not full parity with classic `filetype.c` and `running.c`.

## 2026-07-28 Core-owned safe action retry

- Added a terminal `retryable` action-task field. Failed and cancelled copy/move/delete tasks retain their already validated source directory, destination directory, target paths, snapshot identities and stat identities inside the core session; mkdir remains non-retryable.
- Added the versioned `retry-action` command and a bounded 64-entry retry history. Retrying transfers the immutable prepared action back to the single FIFO worker, so the UI never reconstructs a request from display paths and stale/no-follow checks run again in the worker.
- Task Center now exposes a clickable `Retry task` only when core says the identity is retained; unavailable terminal rows remain visibly disabled. Queue acknowledgement and history cleanup preserve the existing event stream and avoid retaining unbounded action data.
- RED/GREEN evidence: the initial TUI app/protocol tests failed before the field/command implementation; after implementation focused TUI tests passed 51 tests / 231 expects, C action snapshot tests passed 9363 checks / 72 tests, real v3 integration passed 11 tests / 73 expects, full TUI coverage passed 128 tests / 461 expects at 86.70% functions and 97.11% lines, typecheck/audit passed, and serial `env -u VIFM -u MYVIFMRC make check` passed.

## 2026-07-28 Archive listing preview

- Added additive preview kind `archive`. ZIP-like files are classified by the C core before ordinary text/open handling; F3 and Space therefore show a read-only archive member listing instead of sending the archive to the platform opener.
- The preview worker invokes only absolute helper paths and structured argv: ZIP prefers `unzip -Z1`, while tar-family archives prefer `bsdtar -tf` and then `tar -tf`. Output remains bounded by the existing preview byte cap and inherits cancellation/deadline handling.
- This is intentionally a listing preview, not a mounted directory. It does not write archives, change pane cwd/tabs, or expose helper shell syntax; Phase 4 still needs the Vifm FUSE/sshfs-compatible enter/mount lifecycle.
- RED/GREEN evidence: the protocol archive fixture failed before the new kind was implemented; after implementation the protocol suite/typecheck passed, core snapshot passed 9364 checks / 72 tests, real integration passed 12 tests / 79 expects, full TUI coverage passed 129 tests / 464 expects at 86.72% functions and 97.18% lines, audit passed, serial `env -u VIFM -u MYVIFMRC make check` passed, and schema/diff checks passed.

## 2026-07-28 Archive enter intent boundary

- Added additive `resource_kind: "archive"` to immutable C snapshots for regular archive files. OpenTUI no longer sends an archive to the platform opener on `l`/Enter; it sends the core-owned `enter` command.
- Until a validated mounter lifecycle is available, core acknowledges that command with `archive-mount-unavailable`. This is an explicit safe boundary, not a claim that ZIP is already mounted; F3/Space listing preview remains available.
- RED/GREEN evidence: protocol and app tests first failed to preserve the marker and route `l`; after implementation focused protocol/schema/UI tests passed, the real ZIP session verified the marker and structured error, and C snapshot tests passed 9364 checks / 72 tests.

## 2026-07-28 Binary hex preview fallback

- Added additive preview kind `binary`. Core classifies a bounded list of common binary suffixes and renders fixed-width 16-byte hex/ascii rows in the existing preview worker.
- The fallback uses direct bounded reads with cancellation/deadline checks; it does not invoke a shell or external viewer, and truncation remains explicit in the preview event.
- RED/GREEN evidence: the real binary session first timed out because core emitted no binary preview; after implementation the protocol/schema fixture, C build, and real binary integration passed.

## 2026-07-28 Narrow Space viewer fallback

- RED/GREEN evidence: a new 60-column app test first observed `SPACE QUICK VIEW`; the narrow Space path now renders the same full-screen `F3 VIEW` component while retaining the opposite-pane preview identity and close behavior. Focused test and typecheck pass.

## 2026-07-28 Resource mounter capability boundary

- Added `src/neovifm/resource_mount.[ch]` and wired it into the core probe/session build lists. The module prepares Vifm-compatible archive and SSH mounts without shell interpolation: absolute helper discovery, bounded path/remote validation, explicit read-only argv, and required unmount helper capability.
- Added five C fixture tests using injected fake executables. Focused fixture passed 54 checks / 5 tests; the full snapshot binary passed 9418 checks / 77 tests after the cleanup fix.
- This slice intentionally does not fork a helper or mutate pane state. The host has `sshfs` and `umount` but no `fuse-zip`/`archivemount`, so `archive-mount-unavailable` remains the correct core enter result until the cancellable resource lifecycle is wired.
