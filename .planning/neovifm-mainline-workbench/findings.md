# NeoVifm 主线能力继承与任务工作台 Findings

## 2026-07-27 用户决策

- OpenTUI 是 NeoVifm 默认产品入口；当前 C core + OpenTUI 的演进路径就是正式主线。
- `Hybrid` 只描述实现边界：Vifm C core 拥有成熟领域能力，OpenTUI 是默认 UI，两者通过版本化 DTO/事件协作；它不等于“实验客户端”。
- 下一步最高优先级是继承 Vifm/ViATc 快捷键与动作语义，然后推进 archive/SSH 目录化、F3 富媒体预览和后台任务中心。
- 文件操作语义需要后续补齐；安装与发布暂缓；多媒体预览进入当前计划。
- 当前轮次只规划。用户明确说“可以开始执行”前，不修改源码、不提交。

## 本地参考仓库

- `/Users/rex/soft/_refs/neovifm/viatc`：`linxinhong/ViATc`，AutoHotkey 的 Vim Mode At Total Commander。适合提取多键序列、计数、动作命名和 Total Commander 行为矩阵，不适合直接移植实现。
- `/Users/rex/soft/_refs/neovifm/vifm-sixel-preview`：Vifm Sixel preview 参考。
- `/Users/rex/soft/_refs/neovifm/vifmimg`：Vifm 图像 preview 参考。
- `/Users/rex/soft/_refs/neovifm/yazi`：图片/PDF/媒体 preview、capability 和交互参考。
- ViATc 的 `actions/TCCOMMAND.ahk` 已列出快速查看、打开目录或压缩包、在另一 pane 打开、选择、历史、tabs、批量重命名、目录同步/比较和后台传输管理器等动作，可作为完整盘点入口。
- ViATc 的实现核心是 AHK window-class hotkey、发送键、宏和 Total Commander internal command 转发，不是文件管理 engine；只复用交互语义和动作清单，不移植窗口注入、tooltip、send-key 或 AHK plugin runtime。

## Vifm 已有能力

- `src/engine/keys.c` 与 `src/modes/normal.c` 已是成熟 Vim 语义来源；长期方案应桥接它们，而不是持续扩大 `clients/tui/src/keymap.ts` 的独立状态机。
- `src/int/fuse.c` 已实现 `FUSE_MOUNT`、`FUSE_MOUNT2`、`FUSE_MOUNT3`。Vifm 文档与 `data/vifmrc` 已给出 `fuse-zip`、`archivemount`、AVFS 和 `sshfs` 示例。
- `data/vifmrc` 已包含 PDF、图片、音频、视频、archive 等 `fileviewer` 示例；`fileviewer`/`previewprg`、`quickview`、`vcache`、`%pd/%pc/%px/%py/%pw/%ph` 提供 viewer 选择、后台缓存、pass-through 和清理边界。
- Vifm 已支持 `:copy/:move/:delete ... &` 后台执行，`fops_cpmv_bg()`、`ops`、`background.c` 和 `bg_op_t` 提供进度与取消，`:jobs` 菜单可查看和取消当前任务。
- 现有 `:jobs` 只关注当前运行 job，界面和历史都不足以满足用户提出的“队列 + 全部当前会话历史 + 右下角弹窗入口”。正确方向是在既有 background/ops 上补 task facade、排队和历史，而不是另造 executor。
- 当前 Hybrid `action_task` 是单 worker 且只接受一个未完成动作；额外请求返回 queue-full。它已经证明 UI 可在文件动作期间继续响应，但尚未形成多项排队、完整历史和 Vifm undo 复用。

## 规划判断

- 快捷键必须先做“全量矩阵、分批落地”，否则“抄过来”会变成无边界的零散映射；矩阵保证每个动作都有明确状态。
- 主线按键骨架应固定为 `F3--F10 + hjkl/gg/G/Ctrl-W/gt/gT`。Total Commander 行为作为动作层补充，不能削弱 Vifm 导航语义。
- Vifm normal mode 已有 `e` explore-contents 语义，适合做 archive 的显式入口；`l`/Enter 则通过统一 resource capability 判断是进入目录/archive/remote，还是对普通文件转入 F3 viewer。
- Classic quickview 是 pane/预览区布局，当前 OpenTUI F3 是全工作区 overlay。底层 viewer resolver/cache 可以复用，UI 形态不应重新合并成常驻第三栏。
- archive 首切片应利用 Vifm FUSE/association 能力，以只读浏览和 copy-out 建立闭环；写回 archive 后置，避免事务和损坏风险。
- SSH 首切片应使用 sshfs/FUSE 适配；不在 OpenTUI 中实现 SFTP 协议，也不让凭据进入 JSONL、日志或任务历史。
- F3 应是唯一显式 viewer 入口，继续使用全工作区 overlay；底层 viewer resolver 复用 Vifm 的 association/cache/cleanup 语义。
- 文件操作、mount、remote 和 preview 需要不同资源 lane，但统一显示在任务中心。文件操作初版串行可减少磁盘竞争，preview lane 必须保持独立。
- 任务历史本计划先保证当前应用会话完整可见。跨重启持久化需要单独定义 retention、隐私、敏感路径和崩溃恢复，不能顺手写无界日志。

## 2026-07-28 Phase 2 undo bridge

- classic `src/undo.c` is a process-global singleton. The first bridge slice is therefore limited to one `core_session` process, initialized once at startup and reset once at exit; `undo` is never executed from the action worker.
- Linking `undo.c` into the small core-session target would otherwise pull the full classic registers/trash/filesystem graph. `src/neovifm/undo_bridge.c` supplies a narrow compatibility boundary for the classic module when building the core target; normal test builds exclude that core-only shim and use the existing Vifm utility implementations.
- Only successful `mkdir` actions are recorded. The bridge stores the parent and created-entry no-follow identities and executes `OP_RMDIR` through `nv_fs_remove`, so a replaced or symlinked path fails instead of being removed by path alone.
- The protocol action is core-owned `{"action":"undo"}` and is intentionally limited to `u`/undo. Copy/move/delete currently return an explicit `undo-empty` result rather than claiming unsupported undo; redo and destructive-action undo remain outside this slice.
- Each mkdir record carries its source pane/tab location. Undo refreshes that exact tab, so creating a directory in an inactive tab and undoing from another tab cannot leave a stale snapshot behind.
- If the post-action undo record allocation or classic-group registration fails, the mkdir remains successful and the bridge reports the failure only to stderr; this slice does not yet publish undo availability in the task event. The limitation is explicit and must be removed before destructive actions claim stable undo support.

## 2026-07-28 双栏交互、信息层与文件打开关联

- 当前 `clients/tui/src/keymap.ts` 将 `space` 与 `tab` 都映射为 `focus-next`，用户判断准确；计划中保留 Tab 切换 pane，把 Space 改为对面 pane 临时预览。
- 对面 pane 预览必须是独立 render state，不能替换目标 pane 的 immutable directory snapshot。源 cursor 变化只更新 preview generation；退出预览后目标 pane 的 cwd、tab、selection 和 history 应与进入前完全相同。
- 当前排序已有 entry kind，可先在 core comparator 建立 `parent -> directory -> non-directory` 的稳定一级分组，再在组内应用 Name/Size/Modified 等字段与方向。客户端不得为判断 symlink-to-directory 再做一次隐式 stat。
- OpenTUI 底栏目前包含 spacer。目标布局固定为 status、divider、function bar 三行；这样既消除空行，也给 F3--F10 与右下角 Tasks 入口建立稳定边界。
- 终端没有 CSS 式像素圆角。scroll thumb、active sort 和新增 tab 按钮的“圆角”应通过 Unicode/Nerd Font 端帽字形实现，并提供不改变功能或宽度的 ASCII fallback。
- 权限彩色显示应使用 read/write/execute/sticky/no-access 等语义 token，而不是把整串权限涂成一个颜色。owner/group 解析属于 core 元数据能力；TypeScript 只消费 bounded display 字段，解析失败显示 uid/gid。
- lsd 的时间表达适合借鉴为 hour-old/day-old/older 三档主题语义。近期修改项可以提高亮度，但 cursor/selection/error 等状态必须拥有更高颜色优先级，避免一行出现互相争夺的样式。
- Vifm 已经由 `src/filetype.c` 管理 `filetype`、`filextype`、`fileviewer` association，并由 `src/running.c` 负责打开/执行生命周期；`data/vifmrc` 也给出 macOS 未知文件使用 `open` 的 fallback 示例。
- NeoVifm 不应在 TypeScript 中再维护“后缀 -> 命令”表。正确边界是 OpenTUI 发出 preview/edit/open intent，C core 复用 Vifm resolver；普通文件没有显式关联时，macOS 以结构化 argv 调用 `/usr/bin/open`。
- `preview`、`edit`、`open` 必须分离：F3/Space 不启动桌面应用，F4 不走 viewer，Enter/显式 open 普通文件才允许平台 opener；目录、archive、remote 的 enter capability 在 opener 之前判断。
- 外部程序调用必须传 argv、cwd 和 cancellation/生命周期信息，不拼接 shell 字符串；包含空格、引号、前导连字符和 non-UTF-8 名称都需要边界测试。

## 2026-07-28 core open result client bridge

- OpenTUI now treats the core `open-v1` record as the source of truth for regular-file `l`/Enter. The app does not reconstruct a suffix map; it launches the validated argv once per `command_sequence` and keeps the platform-injected opener only as a test/compatibility fallback.
- A real PTY status-row click needed to move with the three-line bottom layout. The updated production fixture confirms the stable row contract after the spacer was removed.
- The remaining open-association work is still core-side: parse Vifm `filetype/filextype/fileviewer`, expand macros with bounded identity validation, and connect external process lifecycle to background/task events.

## 2026-07-28 owner/group lookup boundary

- Synchronous `getpwuid_r`/`getgrgid_r` is unsafe for the core snapshot path on this macOS host: a direct lookup blocked for more than ten seconds, which would violate the non-blocking UI requirement.
- The snapshot now uses bounded local-file parsing for `/etc/passwd` and `/etc/group`, with a numeric id fallback for directory-service users or any malformed/overlong record. This keeps the protocol field bounded without shelling out or contacting remote NSS providers.

## 2026-07-28 preview helper boundary

- Markdown can use the core text reader safely because the content is already bounded, cancellable, and sanitized; the client can render it with OpenTUI's built-in MarkdownRenderable.
- PDF cannot be treated as ordinary text. The first helper slice uses absolute-path `pdftotext` candidates, `posix_spawn` argv, a non-blocking pipe, deadline polling, cancellation termination, and a 64 KiB output cap. Missing helpers and non-zero exits are structured preview errors.
- Image pass-through, terminal graphics capability negotiation, and Vifm `fileviewer`/`previewprg` precedence remain open; adding a fake textual image preview would violate the requested behavior.

## 2026-07-28 Phase 1A core boundary audit

- `src/neovifm/pane_snapshot.c` currently applies one comparator to all entries and reverses the complete result for descending order. Directory-first behavior therefore needs a direction-invariant group comparison before the selected sort key; otherwise descending mode would put files before directories. The current snapshot model has no explicit parent-entry flag, so this slice only groups real `NV_ENTRY_DIRECTORY` entries and leaves any future synthetic parent pinning to a separate capability.
- `src/neovifm/core_session.c` accepts only workspace-mutating session actions plus async file actions. `submit_active_preview()` derives one request from the active pane and uses `nv_preview_request_t.pane` both as source identity and cancellation lane. A source-to-opposite preview intent needs an explicit render target field and target-lane cancellation while preserving `pane` as the source field for protocol compatibility.
- Preview requests/events are immutable copied records in `preview_task.[ch]`; JSON serialization in `snapshot_json.c` can add a required `target_pane` field without changing path identity encoding. Existing auto previews should set `target_pane == pane`, while an explicit quick-view command can target the opposite pane without mutating workspace snapshots.
- The safe core command shape is an additive `preview` action carrying source `pane`, independent `target_pane`, raw `cwd_bytes_hex`/`path_bytes_hex`, source `snapshot_revision` and entry identity fields. The core must validate the supplied source snapshot before queueing, acknowledge the command without changing pane cwd/tab/selection, and increment the preview generation itself.

## 2026-07-28 core-owned open resolver

- 打开关联的边界现在由 `src/neovifm/open_resolver.c` 承担：输入是 intent、目标路径和已经由 core 配置层解析好的 argv 前缀，输出是拥有所有权的 argv。这样 OpenTUI 不需要维护后缀/MIME 映射，也不会把配置拼成 shell command。
- resolver 的优先级是显式 association 优先；无 association 时 macOS 固定使用 `/usr/bin/open`，Linux/BSD/Solaris/AIX 使用 `xdg-open`。当前只实现 `open` intent 的平台解析，`edit`/`preview` 明确返回 unsupported，避免把 F3/F4 viewer/editor 语义伪装成桌面 opener。
- v3 `open-v1` capability 与 `open` resolved record 只发布 `command_sequence`、intent/source/state、原始 `path_bytes_hex` 和结构化 argv。C session 不执行外部程序，客户端仍负责平台启动策略；协议 schema 与 TypeScript validator 对 argv 数量、参数非空和字节长度设上限。
- 本切片通过真实 core session 验证 association 优先路径；当前未接入 Vifm `filetype/filextype/fileviewer` 的 pattern/MIME 匹配、宏展开、terminal/GUI 生命周期，也未将 open command 绑定到 snapshot revision/device/inode/ctime identity。上述内容是 Phase 1B 的剩余工作，不能把当前 resolver 称作完整 Vifm association 兼容层。

## 2026-07-28 open target identity and task details

- `open` 与 file actions/preview 一样必须携带 source pane 的 cwd/snapshot identity 以及 entry device/inode/ctime。只验证 raw `path_bytes_hex` 会允许旧列表中的 path 在替换后被外部 opener 执行；core 现在在 resolver 前返回 `stale-open`，目录返回 `enter-required`。
- `nv_open_resolve_rules()` 是适配边界而不是配置存储：调用方传入已经按 Vifm precedence 排序的有限规则，resolver 不维护后缀表，也不读取全局状态。完整主线仍需要把 classic `filetype/filextype/fileviewer` 加载过程桥接到这一 DTO，并保留 terminal/graphical viewer 生命周期。
- TaskCenter 终态详情可以安全展示已有 action event 字段，但 retry 不能只凭 task id 重建 copy/move/delete。没有源/目标 raw identity、snapshot revision 和冲突策略时，UI 必须显示 disabled，而不是发送一个可能作用于错误目录的“重试”命令。
- macOS PTY 的 production test 在和其他 integration suite 连续运行时可能遇到 ANSI incremental redraw 的控制序列与普通内容字符边界，导致严格的 `inside` 文本断言误报；改为等待稳定的 `ins` 内容片段后仍验证目录进入和 preview 输出，同时保留独立 C session 的完整内容断言。

## 2026-07-28 bounded MYVIFMRC association source

- `MYVIFMRC` is now an optional, bounded input source for the open resolver. The parser accepts the three Vifm association namespaces, continuation lines, brace glob sets and the first command candidate while skipping MIME selectors; it does not claim full `filetype.c` semantics.
- Explicit core-provided association argv continues to win over the environment-loaded rules. An unset environment variable leaves platform fallback available; a configured but unreadable or malformed file returns a structured error so configuration failures remain visible.
- The loaded rule set owns copied pattern/command strings and is freed as one object. The resolver still rejects shell operators, unsupported macros and control bytes before producing a structured argv.

## 2026-07-28 Core-owned safe action retry

- Retry must be core-owned: keeping only a task id or display path is insufficient because the source/destination directories and selected entries may be replaced after the original request. The retained action therefore includes all original raw paths plus cwd/target device, inode and ctime identities.
- `retry-action` submits that retained prepared action to the same bounded FIFO worker. The worker repeats directory and target validation, so a stale or replaced source fails safely and an existing destination remains a visible `destination-exists` failure rather than being overwritten.
- Only copy, move and delete terminal failures/cancellations are retryable in this slice. `mkdir` is excluded because its undo/redo semantics are separately bridged and a blind retry would create a different name/resource.
- Retained actions are capped at 64 per core session. The UI can clear its current history view, but core owns the retry retention and frees evicted identities; no cross-restart persistence or daemon is introduced.

## 2026-07-28 Archive listing preview

- Archive listing is a safe intermediate capability before mounting: the core classifies common archive suffixes and the preview lane returns member names without changing the pane's directory model.
- Helper execution is shell-free and bounded. ZIP uses `unzip -Z1` when available; tar-family formats use `bsdtar -tf` or `tar -tf`. Missing helpers and non-zero exits remain structured preview failures.
- The listing deliberately does not claim Phase 4 completion. ZIP/SSH enter, mount ownership, copy-out from a mounted resource, unmount cleanup, and helper capability reporting still require a separate resource lifecycle.

## 2026-07-28 Archive enter intent boundary

- Snapshot entries now carry an additive `resource_kind: "archive"` marker so the client does not infer archive behavior from display names or duplicate the extension map.
- `l`/Enter routes that marker to the core resource command. Until a mounter is installed and lifecycle code exists, the core returns `archive-mount-unavailable`; platform `open` is intentionally not used for archives.
- This narrows the safety boundary but leaves actual ZIP mounting, tab/history integration and cleanup in Phase 4.
