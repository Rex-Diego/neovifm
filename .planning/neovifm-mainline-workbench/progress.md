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
- This slice intentionally does not fork a helper or mutate pane state. `archivemount-ng` 1b builds against the existing headers, but its OSXFUSE runtime is not available to the kernel; the temporary helper was removed after a probe showed false-success without a mount. Mounted browsing remains blocked until a user-approved FUSE runtime is enabled.

## 2026-07-29 Media metadata and transfer undo slice

- Added bounded core preview kinds for image, audio, and video. Image previews report detected format and dimensions when available; audio/video previews report container metadata. All output is sanitized ASCII metadata with an explicit metadata-only fallback, so binary payloads never reach the terminal renderer.
- Added copy/move undo groups to the core-owned classic undo bridge. Completed transfers retain immutable source/destination paths and no-follow identities; copy undo removes only an unchanged destination, while move undo moves it back only when both parent/child identities still match. Terminal action DTOs now publish `undo_available` only after the bridge records the group successfully.
- Added F9 SSH mount to the OpenTUI function row and guarded it by `resource-tasks-v1`. The modal validates one bounded remote string and sends the existing core `mount-ssh` command; resource task records intentionally omit the remote to keep credentials and sensitive parameters out of protocol/history.
- Fixed resource completion handling so a successful task without mount ownership is reported as a structured core error instead of being treated as an attached resource.
- RED/GREEN evidence: C snapshot suite passed 9585 checks / 85 tests; TUI typecheck and focused app/keymap tests passed 44 tests / 249 expects; real core-session integration passed 8 tests / 50 expects; core-probe + keyboard + core-session integration passed 14 tests / 90 expects; standalone production PTY passed 1 test / 8 expects; `git diff --check` passed.
- Known verification note: running every integration file in one Bun invocation after the PTY can intermittently time out the first core-session startup; the core-session file and the non-PTY integration set pass when scheduled separately/serially. A bare `bun test` without `NEOVIFM_CORE_PROBE`/`NEOVIFM_CORE_SESSION` correctly fails integration preconditions and is not a code regression.

## 2026-07-29 Optional helper setup

- Installed Homebrew `chafa` 1.18.2 and its image-loader dependencies. The helper is reserved for the future graphical preview fallback; current core output remains metadata-only.
- Built `archivemount-ng` 1b against Homebrew `libarchive` and the existing OSXFUSE 3.10.4 headers in a temporary prefix. A real ZIP mount probe reached the helper but failed with `kext load failed` / `file system is not available`; the helper was removed after the probe because its zero exit code did not imply an active mount. The existing FUSE runtime needs system approval or replacement with macFUSE/FUSE-T before a mount E2E can be accepted. ZIP listing preview remains deterministic and does not depend on FUSE.
- README now documents required/optional helper installation, macOS FUSE constraints, reproducible archivemount build, and targeted uninstall commands.

## 2026-07-29 Task-center transfer progress continuation

- 本轮目标：在不改变现有文件操作/undo 语义的前提下，扩展 action-task DTO，发布 source/destination/current path identity、条目级进度和仅对可确定的普通文件提供字节进度。
- 验收边界：目录或非普通文件只发布 `bytes_known=false`，不得伪造递归字节总量；协议字段 additive，旧 v3 fixture 仍必须可解析。
- C action queue 现在为 queued/running/progress/terminal 事件携带 source、destination、current path 的 hex identity；普通文件 copy/move 记录已完成条目和字节数，目录/非普通文件保持未知字节状态。
- Task Center 队列行展示短路径和字节进度，历史详情展示 transfer/current path、item/byte progress、failure 和既有 retry/undo 信息；路径只通过协议 hex 解码，不重建 shell 命令。
- RED/GREEN 与回归证据：`make -C tests neovifm_snapshot` 通过 9615 checks / 86 tests；TUI 全量 134 tests / 497 expects，coverage 85.51% functions / 89.44% lines，`bun run typecheck`、`bun audit` 通过；真实 core-probe/session/keyboard/production PTY 15 tests / 98 expects 通过；串行 `env -u VIFM -u MYVIFMRC make check`、`git diff --check` 通过。

## 2026-07-29 Chafa ASCII image fallback

- 图片 preview 现在按 capability 顺序先尝试 `chafa` 的 `symbols/ascii` 无色输出，再回退原有 metadata；helper 只接受显式绝对路径 `NEOVIFM_CHAFA_EXECUTABLE` 或固定绝对候选路径，不经过 shell。
- `--polite on`、`--relative off`、`--colors none` 和 `--symbols ascii` 保证 ANSI/Kitty/Sixel 控制序列不进入 line-safe v3 JSONL；输出有界、可取消、受 deadline 约束，helper 缺失、非零退出或空输出不会阻塞可读 metadata。
- C test 以带空格的图片路径和注入 helper 验证结构化 argv；RED/GREEN 与回归证据：`make -C src neovifm/preview_task.o neovifm-core-session`、`make -C tests neovifm_snapshot` 通过 9635 checks / 87 tests，`git diff --check` 通过；实际仓库 PNG 的 `chafa` 命令行输出为纯 ASCII。

## 2026-07-29 Action task lifecycle timestamps

- action-task DTO 新增可选 `started_at_unix_ms` 与 `finished_at_unix_ms` 字符串字段；queued 事件不填充时间，worker 进入 running 时记录开始时间，终态记录结束时间并钳制为不早于开始时间。
- snapshot JSON、v3 schema 和 TypeScript parser 保持 additive/旧记录兼容；parser 拒绝结束时间早于开始时间，Task Center 终态详情在字段存在时显示本地时间区间。
- RED/GREEN 与回归证据：C snapshot suite 通过 9639 checks / 87 tests；TUI focused 67 tests / 311 expects、全量 134 tests / 502 expects，coverage 85.51% functions / 89.42% lines，`bun run typecheck`、`bun audit` 和 `git diff --check` 通过；Task Center 时间行在 100x24 详情布局中仍保留 Retry 操作可见。

## 2026-07-29 Exit collaboration guard

- F10/`ZZ` 退出请求在存在 queued/running action/resource task 时先打开明确的 EXIT overlay：等待任务完成、发送 core-owned cancellation 后退出，或返回应用；无活动任务时保持原有直接退出路径。
- 等待/取消均设置退出 pending 状态，只有 reducer 观察到活动任务数归零后才销毁 renderer；取消路径只发送结构化 `cancel-action`/`cancel-resource`，不把任务提升为常驻 daemon。
- 退出选项在窄终端自动纵向排列，避免 60 列布局中的横排按钮溢出；新增窄屏渲染测试覆盖三项入口仍可见。
- RED/GREEN 证据：新增 app test 先验证旧实现会直接销毁渲染器，再验证鼠标入口、返回、取消和任务清空后的退出；窄终端测试覆盖纵排布局。最终 TUI unit/coverage 为 136 tests / 517 expects、85.50% functions / 89.38% lines，`bun run typecheck`、`bun audit`、真实 integration 15 tests / 98 expects、C snapshot 9639 checks / 87 tests、串行 `env -u VIFM -u MYVIFMRC make check` 与 `git diff --check` 均通过。

## 2026-07-29 F3 preview loading flicker fix

- 根因是 F3 function key 的可用状态错误绑定到 `matchingPreview()?.content`；预览在 queued/running/terminal 事件之间切换时，按钮会在禁用和启用样式之间抖动，鼠标点击也会在加载阶段被吞掉。
- 改为只要当前 pane 有选中条目就保持 F3 可用。F3 鼠标和键盘路径因此一致，查看器仍可在预览内容尚未到达时打开并展示任务状态。
- queued/running 预览现在统一显示 `Loading preview...`，只有终态且没有内容时才显示错误码或 `Preview unavailable`；F3 与 Space viewer 共用这个判断。
- RED/GREEN 证据：新增加载态鼠标回归测试先在旧实现失败，按钮修复后通过；加载文案断言随后先失败，再通过；TUI 全量 137 tests / 521 expects，coverage 85.51% functions / 89.38% lines，`bun run typecheck`、`bun audit` 和 `git diff --check` 通过。

## 2026-07-29 Vifm fileviewer preview argv

- 预览队列现在接受最多 32 个、每项最多 16 KiB 的结构化 `viewer_argv`，入队时深拷贝；worker 通过 `posix_spawnp` 按 PATH 查找 Vifm `fileviewer` 命令，不经过 shell，仍共享既有输出上限、取消和超时处理。
- core session 在 F3/Space 自动或显式预览提交前读取有界 `MYVIFMRC`，按已有 Vifm `fileviewer` resolver 展开 `%f/%c/%%` 和 glob 规则；无匹配、配置未设置或配置错误时回退内建 preview，队列失败不会阻塞 UI。
- 真实集成覆盖带空格路径、临时 PATH 命令和本机默认 `fileviewer *.zip zip -sf %c`；`make -C tests neovifm_snapshot` 通过 9657 checks / 88 tests，`make -C src neovifm-core-session` 通过，清除用户配置后分组串行 integration 通过 17 tests / 102 expects，串行 `env -u VIFM -u MYVIFMRC make check` 通过。
- 当前边界仍是安全的第一步：`previewprg` 优先级、多个逗号候选的可执行性回退、图形终端 passthrough，以及 archive/SSH 挂载生命周期尚未接入；用户本机已有 `MYVIFMRC` 会影响未清环境的旧集成夹具，因此验证命令需清除该变量。

## 2026-07-30 Vifm previewprg priority

- `MYVIFMRC` loader 现在识别 `set`/`setl`/`setg`/long-form scope 的 `previewprg=`，保留最后一个有界值并去除 option 外层引号；配置释放时一并释放该值，旧 association rule DTO 保持兼容。
- F3/Space 的 core resolver 现在按 classic 优先级先尝试全局 `previewprg`，命令不安全或不可解析时再尝试匹配的 `fileviewer`，没有可用 association 才回退内建 preview。所有命令仍经结构化 argv、PATH 查找、输出上限、取消和超时边界。
- RED/GREEN 证据：配置单测先因缺少 `previewprg` 字段失败，随后通过；真实 core session 覆盖 `previewprg` 胜过 `fileviewer`，以及含管道的 unsafe `previewprg` 回退到 `fileviewer`；当前 C snapshot 为 9657 checks / 88 tests，focused previewprg integration 两项均通过。
- 尚未宣称完整 Vifm parity：`%px/%py/%pw/%ph/%pc/%pd`、终端暂停/恢复、多个逗号候选可执行性回退、preview cache 和图形协议传输仍留在 Phase 6 后续切片。

## 2026-07-30 Preview Unicode and copy behavior

- C preview sanitizer 现在按 UTF-8 code point 边界验证合法的 2/3/4-byte 序列；合法中文和其他 Unicode 原样保留，非法字节及不允许的控制字符才替换为 `?`，外部 fileviewer/chafa 输出沿用同一边界。
- JSONL 客户端保持路径、错误等 display 字段的严格控制字符过滤，只对 preview content 保留 `\n`、`\r`、`\t`，避免预览复制丢失换行又不让文件名注入布局控制。
- F3 Viewer 与宽终端 Space quick preview 的文本内容显式启用 OpenTUI selection/highlight；新增 `Copy` 入口复制全文，预览内 `y` 复制全文，`Ctrl-Shift-C` 与 macOS `Cmd-C` 复制当前终端选区。
- RED/GREEN 证据：C snapshot suite 通过 9672 checks / 89 tests；TUI 全量 139 tests / 528 expects，coverage 85.51% functions / 89.35% lines，`bun run typecheck`、`bun audit` 和 `git diff --check` 通过；真实 core-probe/session/keyboard/production PTY 分组通过 17 tests / 102 expects，串行 `env -u VIFM -u MYVIFMRC make check` 通过。

## 2026-07-30 Media rendering continuation started

- 用户反馈仍无法预览多媒体；现场核对确认当前 image 的 chafa ASCII 输出可工作，但 PDF 只走 `pdftotext`，audio/video 只返回 header metadata。
- 本轮实现边界已写入 Phase 6：PDF 首页和视频首帧走临时 PNG + chafa 的有界文本降级，音频/视频补 ffprobe 信息；任何 helper 失败仍回退已有安全路径，不引入原始图形协议字节。

## 2026-07-30 Media preview rendering completed

- PDF 预览先用可配置/固定候选 `pdftoppm` 将首页渲染到受限临时 PNG，再复用 `chafa --format symbols --symbols ascii --colors none` 输出 line-safe ASCII；栅格器、chafa 缺失或非零退出时回退 `pdftotext`。
- 视频预览先用可配置/固定候选 `ffmpeg` 提取首帧 PNG，再复用同一 chafa ASCII 路径；失败时先回退 ffprobe 信息，再回退已有头部格式/字节 metadata。
- 音频和视频的 metadata 优先经 `ffprobe` 输出有界的 format/duration/codec/尺寸/采样字段；helper 不可用、损坏或失败时仍不阻塞，保留安全 header fallback。PDF/媒体请求使用 5 秒 bounded deadline，其他预览保持 2 秒。
- 所有 helper 仍走 shell-free `posix_spawnp`、输出上限、取消/超时和临时文件清理；二进制帧不进入 JSONL，终端只收到可选择、可复制的 UTF-8 文本。
- TDD/验收：C snapshot `9734 checks / 92 tests` 通过；真实 core session 注入 fake `pdftoppm`/`ffmpeg`/`ffprobe`/`chafa` 覆盖带空格 PDF/视频/音频终态；`make check` 通过；TUI `139 pass / 0 fail / 528 expects`，coverage `85.51% funcs / 89.35% lines`，typecheck/audit 通过；core-session 11/11 与 probe/keyboard/PTY 7/7 integration 通过。
- README 已补齐 `pdftoppm`/`ffmpeg` 依赖、helper 环境变量和检查/卸载说明；图形 passthrough、音频封面、完整 MIME/candidate 语义和 quickview cache/lifecycle 仍留在 Phase 6 后续范围。

## 2026-07-30 Media viewer fallback verification

- 现场配置中的 `fileviewer *.pdf pdftotext ...` 可能对图形 PDF 正常退出但返回空内容；preview worker 现在只对 image/audio/video/PDF 在 helper 错误或空白成功结果时回退内建媒体 renderer，文本和 Markdown 仍尊重非空外部 viewer 结果，取消/超时不被吞掉。
- 新增 C queue 回归和真实 core-session 回归：空输出 `MYVIFMRC` PDF viewer 仍得到 `pdftoppm` + `chafa` ASCII；外部 viewer 输出非空时继续保持 Vifm association 优先级。
- 最新验收：C snapshot `9737 checks / 92 tests`、串行 `env -u VIFM -u MYVIFMRC make check`、TUI `bun run typecheck`、`bun audit`、coverage `139 pass / 528 expects / 85.51% funcs / 89.35% lines`、core-session `12 pass / 60 expects`、probe/keyboard/PTY `7 pass / 48 expects`、`git diff --check` 均通过。

## 2026-07-30 Search, cursor history and media stability

- 依据 Vifm gap audit，先实现当前主线最直接的缺口：`/` 正向搜索、`?` 反向搜索、`n/N` 循环重复搜索。查询和方向由 C session 按 pane 保存，协议与 TUI 只传有界结构化命令；marks、registers、visual、完整目录历史、批量重命名和 compare/sync 继续标为后续缺口。
- 返回父目录的 cursor 现在按 `tab_id + directory_bytes_hex` 保存原始 `path_bytes_hex`，目录刷新、进入/返回和排序后优先恢复该条目；历史限制为每 pane 128 条，session 释放时清理。新增 C 进入/返回回归，避免退回目录总是落到第一项。
- PDF 栅格化 deadline 从媒体通用预算中分离为 30 秒，普通预览保持 2 秒，取消、输出上限和 helper 清理边界不变。新增 6 秒 fake `pdftoppm` core-session 回归，验证不会误报 `preview-timeout`。
- 对图片关联增加终端安全优先级：当 `fileviewer` 规则只会给出 metadata/桌面命令时，先尝试内建 `chafa` ASCII；内建 renderer 不可用时再回退到外部 viewer/metadata。新增带空格路径的 PNG 集成回归。
- 最新验收：C snapshot `9803 checks / 95 tests`；TUI `141 pass / 535 expects`，coverage `85.52% funcs / 89.35% lines`；core-session `15 pass / 71 expects`；`bun run typecheck`、`bun audit`、串行 `env -u VIFM -u MYVIFMRC make check`、`git diff --check` 全部通过。

## 2026-07-31 Media timeout and symbol correction

- 根因已确认：`@` 来自 Chafa 被固定为 `--symbols ascii`，不是 UTF-8 sanitizer 产生的乱码。图片、PDF 首页和视频首帧现在统一使用 `--symbols block`，仍保持无色、无 ANSI/Kitty/Sixel 原始序列、可选择复制的 line-safe UTF-8 文本；真实仓库 PNG 的 core session 输出已验证为块字符且不含 `@`。
- 图片、音频、视频的 core 预览 deadline 从普通 2 秒/媒体 5 秒统一提升为有界 30 秒；PDF 保持独立 30 秒。PDF `pdftoppm` 追加 72 dpi 与 1200px 上限，降低超大页面栅格化超时风险。
- TDD RED/GREEN：Chafa argv 回归在旧 ASCII 实现下失败，改为 block 后通过；新增 6 秒 fake PNG renderer 在旧图片 2 秒预算下触发 `preview-timeout`，媒体预算修复后完成。完整验收：C snapshot `9803 checks / 95 tests`；TUI `141 pass / 535 expects`，coverage `85.52% funcs / 89.35% lines`；core-session integration `16 pass / 74 expects`；`bunx tsc --noEmit`、`bun audit`、串行 `env -u VIFM -u MYVIFMRC make check`、`git diff --check` 全部通过。

## 2026-07-31 Cursor viewport restoration

- core 的路径身份恢复已确认正确；真正造成“返回后目标跑到最后一行”的是 TUI scrollbox：进入只有少量条目的子目录后 scrollTop 被压回 0，返回父目录时只做 nearest-edge `scrollChildIntoView`。
- `EntryList` 现在按 pane/目录缓存最多 128 个 scrollTop。目录切换先恢复该目录的位置，再执行现有 cursor 可见性校正；同一目录内上下移动仍由 cursor 驱动滚动，刷新/排序不会重置目录历史。
- RED/GREEN：新增父目录 40 项、目标原本在可视区域倒数第五行、子目录仅 1 项的回归；旧实现返回时 scrollTop 从 37 漂到 33，修复后保持原值。全量 TUI `142 pass / 539 expects`，coverage `85.52% funcs / 89.35% lines`；`bunx tsc --noEmit`、`bun audit`、`git diff --check` 通过。

## 2026-07-31 Session persistence and arrow navigation

- OpenTUI now requests `NEOVIFM_SESSION_RESUME=1` only for a no-argument launch and requests `NEOVIFM_SESSION_PERSIST=1` for normal sessions. Explicit paths remain authoritative at startup while still updating the last clean-exit snapshot.
- Core persists a bounded, versioned JSON state file atomically. It records active pane, per-pane tab order/active tab, local directory identities, sort state and cursor path identities. Missing, malformed, stale or mounted-resource entries are ignored safely rather than restored as fake local directories. The state file is written with mode `0600` on POSIX systems, through a same-directory exclusive random temporary file with a last-writer-wins rename.
- A cross-process core-session smoke test restored the active right pane, a nested left directory, the remembered cursor target and two right-pane tabs from the previous process. The C snapshot suite also covers save/load round-trip and stale-state fallback boundaries.
- Arrow keys now match Vifm navigation: `Up/Down/Left/Right` map to `k/j/h/l`. The previous left/right sort-cycle mapping is removed; sorting remains available through the sort control/command path.
- RED/GREEN evidence for this slice: TUI `144 pass / 545 expects` with `85.57%` function and `89.37%` line coverage, `bunx tsc --noEmit`, `bun audit`, C snapshot `9849 checks / 96 tests`, serial `make check`, full integration `23 pass / 122 expects`, core-session build and `git diff --check` all passed. One full-suite startup timing miss was reproduced once and the isolated test plus the next full serial run passed.
