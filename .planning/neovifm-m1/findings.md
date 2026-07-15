# NeoVifm M1 Findings

## 2026-07-14 重新激活：双 Pane 产品缺口

- 用户明确指出新 TUI 的“单文件列表 + 详情”不符合 Vifm/Total Commander 双窗口体系。M0 只验证了边界，不能代替双 pane 产品交付。
- `.planning/neovifm-hybrid-m0/` 的单 pane 原型已经完成；本 M1 计划改为其顺序后继，活动计划应切换到 `neovifm-m1`。
- 当前 `neovifm-core-probe` 只接收一个目录，协议只承载单 `snapshot`，TUI 的右侧为 `Details`，没有第二个独立 cwd/cursor/selection。
- 旧 M1 的 C renderer 计划已经被 Hybrid C core + OpenTUI client ADR 取代，不能原样执行；M1 必须在现有版本化协议上建立 workspace DTO 和双 pane renderer。

## 2026-07-14 架构审查结论

- `core_probe.c` 只能接受一个目录，`protocol.ts` 只定义单 `SnapshotPayload`，`probe-state.ts` 在首个 snapshot 后终态，`core-client.ts` 固定为一个路径，`app.tsx` 的右列是 Details。因此仅替换布局不能成为双 pane 工作台。
- `view_t`（`src/ui/ui.h`）把 ncurses 窗口、watcher、history、过滤、可变文件列表混在一起；`filelist.c` 的目录加载还依赖全局 `curr_view/lwin/rwin` 与进程级 `vifm_chdir()`。M1 headless core 不能直接链接或保存 `view_t`。
- M1a 采用 v1 的原子 `workspace-snapshot`：payload 强制同时含 `left`、`right` 和 `active_pane`。v0 的单 `snapshot` 保持原语义与旧 probe/client 测试；v1 不能伪装为 v0 的可选字段扩展。
- v1 使用单个 workspace record，而不是两条可部分到达的 pane record：TUI 只在得到完整 workspace 后替换状态，避免半个工作区可见。整个 workspace 受 4 MiB record 与 4096 combined entries 限制，不能把 M0 的每 pane 限额简单相加。
- M1a 仅实现两个独立只读目录快照与双栏默认渲染；这足以消除“列表+详情”替代双 pane 的错误，但不应宣称完成 Vifm 式导航。M1b 再把 TUI 持有的 stdio 子进程演进为 session，加入 Tab、移动、进入、返回和 selection；它不是 daemon、socket 或网络服务。

## 2026-07-14 M1a 审查与修复

- C 审查发现 combined entries 超限曾被映射为普通 `serialization`；现已改为 `workspace-too-large`，并增加 2049+2048 entries 的 serializer 回归测试。
- C serializer 现在在写 JSON 前校验 snapshot 的 entry 总数、cursor、容器、字段指针和 hex/display 长度，避免未来调用者生成 TypeScript 边界会拒绝的 DTO。
- TUI 审查发现 client 可把 v0 hello 后的 v1 error 当作正常 core error；现已在 client 边界强制 hello 与 terminal 的 version 一致，并加入模拟进程回归测试。
- Tab 在窄终端切换时必须更换 pane 组件本身；已用 OpenTUI mock input 覆盖该行为。刷新 workspace DTO 后保留本地 focus，不会重置 Tab 选择。

## 2026-07-14 M1b session 边界

- `neovifm-core-probe` 的 v0/v1 都是 `hello + 一个终结记录 + EOF`，不能把常驻 session 偷塞进相同 CLI/协议语义。M1b 新增 TUI 独占的 `neovifm-core-session <left> <right>` 和 v2。
- v2 stdout 顺序为 hello、初始完整 workspace；每个 stdin JSONL command 产生一份完整 workspace 或可恢复 command-error。stdin EOF 正常退出；不引入 daemon、socket 或网络协议。
- session model 仅拥有两个 `nv_pane_snapshot_t` 和 active pane，不接触 `view_t`、`curr_view`、`lwin/rwin` 或 `chdir()`。进入目录必须由 `path_bytes_hex` 解码，不能用可能被净化的 display 文本。

## 已知基础

- `view_t` 当前混合文件列表、selection、history、watcher、布局和 ncurses 状态。
- 经典绘制入口位于 `src/ui/fileview.c`，目录和条目状态来自 `view_t`。
- 第一阶段应创建独立数据快照，而不是直接拆分 `view_t`。
- macOS 可靠测试命令见根目录 `AGENTS.md`。

## 待确认

- snapshot 第一版字段：pane 目录、条目数、游标/顶部位置、窗口行列、过滤数、选择数；条目名称、来源、size/time、平台 stat、nlinks、FileType、selected/dir_link、树层级与 search match。
- `name` 和 `origin` 必须独立复制；普通 view 的 `origin` 指向 `view_t::curr_dir`，不能借用。
- 排除 `id/link/tag/hi_num/name_dec_num/was_selected/marked/temporary/slow_target/owns_origin` 等缓存、操作和所有权字段。
- renderer 当前直接读 `view_t` 并可回写布局缓存，因此 M1 先建立独立模块和测试，再通过实验分派逐步替换。
- 测试目录和 fixture 的最合适落点。

## 代码证据

- `dir_entry_t` 定义：`src/ui/ui.h:217`。
- `view_t` 列表/游标/窗口字段：`src/ui/ui.h:372`。
- 经典绘制循环直接读取 `view->dir_entry`：`src/ui/fileview.c:310`。
- 现有深拷贝只复制 `name/origin`，但会保留其他内部字段：`src/filelist.c:2697`，不适合作为新公开契约。
- 测试框架会自动收集 suite 目录中的 `.c` 文件，新增 suite 只需加入 `tests/Makefile` 的 `suites` 列表。

## 2026-07-14 classic adapter 接入证据

- `view_t` 的可读 pane 状态足够形成 DTO：`curr_dir`、`list_pos`、`list_rows`、`selected_files` 和 `dir_entry` 位于 `src/ui/ui.h`；`dir_entry_t` 已有 name、origin、metadata、type、selected 与 dir_link。
- 但 `filelist.c` 的加载/导航路径会触发 watcher、status、全局 view 与 `vifm_chdir()`，不能从 headless session 调用。adapter 必须只读已加载 `view_t` 并深拷贝，不得用于加载、导航或 session 状态写回。
- 现有测试支持 `view_setup()`、`view_teardown()`、`populate_dir_list()`（`tests/test-support/test-utils.c`），可用于未来 adapter 单测，而无需启动 ncurses。

## 2026-07-14 macOS watcher 方案

- session 私有 watcher 使用 macOS `kqueue`，不改通用 `fswatch_nix.c`。同一 queue 监听 stdin 的 `EVFILT_READ` 与 left/right cwd FD 的 `EVFILT_VNODE`（WRITE/EXTEND/DELETE/RENAME/ATTRIB）。
- watcher 输出完整 workspace，新增 v2 `trigger: watch`；保留最近 command sequence。TS reducer 只在 trigger=watch 时接受相同 command sequence，仍拒绝重复 command ack。
- cwd 变化后关闭/重开对应 FD；watch 失败只诊断并停止该 pane watch，不杀 session，stdin 命令继续可用。

## 2026-07-14 续作恢复

- `session-catchup.py` 未报告未同步上下文；活动计划为 `.planning/neovifm-m1`，当前工作树需保留既有 M0/M1 变更。
- Phase 4 当前只剩 macOS session watcher、真实 adapter 接入和 classic 回归确认；本次将从 kqueue 实现及其失败/刷新语义的 TDD 继续，不改变 v0/v1 probe 的终结记录语义。

## 2026-07-14 macOS watcher 交付

- `neovifm-core-session` 现在在 macOS 使用单一 `kqueue` 同时等待 stdin 和两个 pane cwd 的 `EVFILT_VNODE`。目录变化通过 `nv_workspace_session_refresh_pane()` 只重建变化 pane，保留另一个 pane 和 active focus。
- v2 `workspace-snapshot` 新增必填 `trigger`：启动为 `initial`、命令为 `command`、watcher 为 `watch`。watch record 保留最近确认的 `command_sequence`；TypeScript reducer 只为 `watch` 接受该相同序号，避免把重复 command ack 当作刷新。
- 成功和可恢复失败 command 都推进 session 的确认序号，使失败后的 watcher snapshot 不会被 client 当成 stale。cwd 因 enter/parent 改变后会关闭并重开对应目录 FD；单 pane watch 失败只记录 stderr 且不终止 stdin session。
- TDD 真实 Bun/C integration 已验证：外部写入 left cwd 后，无需 `refresh` command 即收到同 command sequence 的 workspace，并且 right pane 不变。focused C test 另覆盖刷新 inactive pane 时 active focus 不变。
- 安全审查结论：stdin 仅接受 16 KiB 上限内的版本 2、白名单 action JSON；TypeScript 端也限制命令大小并校验路径 NUL。stdout/record 在 client 端有 byte/record 上限，watcher 不拼接 shell 或执行外部命令；未发现 secrets、注入或 HIGH/CRITICAL 风险，`bun audit` 无漏洞。

## 2026-07-14 classic workspace adapter

- 新增 `nv_classic_workspace_snapshot_from_views()`：在 classic UI 线程把两个已加载 `view_t` 原子深拷贝为 owned left/right snapshots 与明确 active pane；任一 pane 复制失败时保留旧 workspace。
- 该 adapter 不接入 headless stdio session，也不调用 filelist 加载或改变 cwd；它是之后从 classic runtime 发布不可变 DTO 的受限桥接面，避免把 `view_t` 泄漏到 C/TypeScript 边界。

## 2026-07-14 M1 收尾证据

- `nv_workspace_session_init_from_classic_views()` 已把两个已加载的 classic panes 转为同一 owned session model；`vifm` 现在显式链接该 model，headless session 仍保持独立扫描以避免跨进程共享 `view_t`。
- snapshot DTO 现在包含 selection、filter 和 primary sort metadata；classic adapter 从可见 list 计算 selection count，复制 filtered count/排序方向，并在所有权转换失败时保持旧 workspace。
- M2 首切片评估见 `docs/NEOVIFM_M2_ENTRY.md`：只允许进程内 task queue 与可取消 preview，不引入 daemon、socket、网络或运行时替换。

## 2026-07-14 完整验收

- 当前活动计划无未勾选项。M1 仍保持 classic renderer 默认、v0/v1 one-shot probe 兼容与 v2 TUI-owned stdio session；未引入网络、daemon、Rust/C++ 或文件操作语义改动。
- 验收证据：focused C 26 tests/8492 checks，classic `make check` 通过；TUI 53 unit tests/115 assertions、coverage 87.85% functions/93.63% lines、typecheck、4 integration tests、Bun audit、Windows session target dry-run 与 `git diff --check` 均通过。
