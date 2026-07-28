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
