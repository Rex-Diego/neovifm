# NeoVifm Core Protocol

本目录定义 C core 与客户端之间的实验性本地协议。v0 是单目录 M0 probe；v1 是 M1a 的原子双 pane workspace；v2 是 M1b 的 TUI 持有 session；v3 是 M2 的可取消预览 session。

## Transport

- UTF-8 JSON Lines over stdout。
- 每行一个完整 envelope，以 `\n` 结束。
- stderr 只用于人类可读诊断。
- M0/v0 core probe 不读取 stdin；一个目标目录通过命令行参数传入，正常顺序为 `hello`，随后 `snapshot` 或 `error`，然后进程退出。
- M1a/v1 probe 也不读取 stdin；两个目标目录参数产生 `hello`，随后一个原子的 `workspace-snapshot` 或 `error`，然后进程退出。
- M1b/v2 `neovifm-core-session <left> <right>` 从 stdin 接收受限 JSONL command，stdout 持续发布完整 workspace；stdin EOF 正常结束 session。M2/v3 保留该进程模型，并额外发布异步 task 与 preview record；它始终是 TUI 的子进程，不是 daemon、socket 或网络服务。
- client 遇到 framing、字段、顺序或 reducer 校验失败时会立即终止 probe；该失败之后的 stderr 不保证被保留。

## Compatibility

- `protocol` 固定为 `neovifm-core`。
- `version` 目前支持 `0`、`1`、`2` 与 `3`，都属于实验协议。
- 客户端必须拒绝不支持的 version。
- 同一 version 内，接收方必须忽略未知字段。
- 已存在字段不能改变类型或语义；只能增加可选字段。

## M0 Limits

- 客户端拒绝超过 4 MiB 的单条 JSONL record 或超过 8 MiB 的整个 stream。
- snapshot 最多 4096 个 entries；超过上限的 C probe 返回结构化 `snapshot-too-large` error。
- display 文本最大 16 KiB，hex 原始字节字段最大 32 KiB，十进制数值字段最大 32 字符。
- `sequence` 必须位于 JavaScript safe integer 范围内（0 至 9007199254740991）。
- `maxLength` 是 schema 的字符边界；client 另外按 UTF-8 字节实施上述资源边界，并在替换不安全展示字符后再次检查 display 字节数。
- `snapshot.cursor` 的跨字段关系为：空 snapshot 时必须为 `-1`，否则必须满足 `0 <= cursor < entry_count`。标准 JSON Schema 无法表达这个动态数组索引关系，schema 以 `$comment` 声明，C producer 与 TypeScript validator 都强制执行。

## M1a 双 Pane Workspace v1

- `neovifm-core-probe <left> <right>` 发出 v1，hello capability 为 `workspace-v1`，随后一条 `workspace-snapshot`；`left`、`right` 与 `active_pane` 必须同时存在，客户端不得先渲染半个工作区。
- 每个 pane 复用 v0 snapshot 字段，但整个 workspace 合计最多 4096 entries，且整个 JSONL record 仍不得超过 4 MiB。任何一 pane 扫描失败或工作区超限都发出 v1 `error`，不发布部分 snapshot。
- v0 的一个目录调用和 `snapshot-v0` 语义保持不变；v0 client 必须拒绝 v1，v1-capable client 仍可为兼容测试解析 v0。

## M1b 交互 Session v2

- hello capability 为 `workspace-session-v2`，随后必须先发 `command_sequence: 0`、`trigger: "initial"` 的完整 `workspace-snapshot`。command 成功或可恢复错误都会确认其 command sequence；成功 command 的 workspace 使用 `trigger: "command"`。macOS kqueue 刷新使用 `trigger: "watch"`，保留最后确认的 command sequence；client 只接受这种同 sequence 的更新。
- stdin command 必须是 `{protocol,version:2,type:"command",sequence,payload}`，payload action 仅允许 `focus`（`pane` 为 left/right）、`move`（delta 为 -1/1）、`enter`、`parent`、`toggle-selection`、`refresh`；不接受任意 shell、路径或文件操作。
- 不可执行的 command 输出可恢复 `command-error`（含 command_sequence/code/message/retryable），客户端保留上一份 workspace。协议/核心不可恢复错误才输出 `error` 并退出。
- v2 每条 stdout record 仍受 4 MiB 与 4096 combined entries 限制；client 对单个 session 设置 64 MiB/1,000,000 record 的硬边界，并不会累积历史 workspace。
- watcher 仅存在于 TUI 持有的 macOS 子进程：它监听两个 pane 的 cwd，并在目录进入/返回后重开对应 FD。watch 刷新失败只停用该 pane watcher 并写入 stderr，stdin command session 继续运行；后台不会接触 TUI 状态。

## M2 可取消预览 Session v3

- hello capability 为 `preview-session-v3`。v3 保留 v2 完整 `workspace-snapshot` 与 command acknowledgement 语义；每条 task lifecycle event 另发 `task` record，终态另发 `preview` record。
- task 与 preview 都必须携带 task id、generation、pane、preview kind、cwd/path 原始 hex identity、状态和（适用时）结构化 error。完成 preview 额外携带至多 64 KiB 的文本；过时 generation 的 preview 不得覆盖新 generation。请求在 deadline 前未开始或在受限读取循环中超时，会以 `failed` / `preview-timeout` 终态发布。
- v3 session 在主线程从当前不可变 pane snapshot 构造 preview request（pane、generation、cwd/path 原始 hex 与 kind），每次 cursor/focus 工作区更新都会替换同 pane 的旧请求。stdin 仍只接受受限导航 command，不接受 shell、任意外部路径或文件操作。
- v3 导航 command 在 v2 基础上增加 `focus-next` 与 `move-to`（`target` 仅允许 `first`/`last`）。前者由 core 基于当前 workspace 原子切换 pane，避免 TUI 使用陈旧 snapshot 推导目标 pane；后者用于 Vifm 的 `gg`/`G` 首尾移动。`focus` 仍用于 Ctrl-W h/l 的显式定向切栏。
- worker 只执行受限文本/目录 I/O；stdout 由 session 主循环批量发布，stderr 只留诊断。图片、archive、Git 元数据与文件操作 session 不在 v3。

## Pane 元数据

- 每个 snapshot 可携带由 core 计算的 `selection_count`、`filtered_count`、`sort_key`、`sort_descending` 与 `filter_active`。这些字段描述本次不可变条目集的来源状态；`selection_count` 必须等于 `entries[].selected` 的数量。
- headless core 的默认值是按名称升序、无过滤；classic bridge 在 classic UI 线程从已加载 `view_t` 深拷贝 selection、可见过滤数量和主排序键。它不调用目录加载、不改变 cwd，也不把 `view_t` 指针传给 session/TUI。

## Numeric Values

可能超过 JavaScript 安全整数的值使用十进制字符串：

- `size_bytes`
- `inode`
- `mtime_unix_ms`
- `generated_at_unix_ms`

## Paths

- `*_display` 是供 UI 使用的 UTF-8 字符串。
- POSIX 上，`*_bytes_hex` 是原始路径字节的十六进制表示，是 M0 的无损标识。
- Windows 端口在定义原生路径表示前，`*_bytes_hex` 使用 core 内部 UTF-8 路径字节；不能宣称是 Win32 原始字节。
- Windows probe 通过 `FindFirstFileW()` 保留符号链接身份（包括悬挂链接）；junction 和其他 reparse point 不会被误报为 `symlink`。其 M0 metadata 来自 no-follow directory entry，未承诺完整 Win32 stat 扩展字段。
- 若 probe 接收相对目录参数，`cwd_*` 与 entry `path_*` 均相对于启动 probe 时的工作目录；客户端在 M0 中只渲染这些值，不得把它们脱离该 spawn context 用于操作。
- 客户端不得用 display 字符串执行文件操作。

## Example

```json
{"protocol":"neovifm-core","version":0,"type":"hello","sequence":0,"payload":{"implementation":"neovifm-core-probe","capabilities":["snapshot-v0"]}}
{"protocol":"neovifm-core","version":0,"type":"snapshot","sequence":1,"payload":{"cwd_display":"/tmp","cwd_bytes_hex":"2f746d70","generated_at_unix_ms":"0","cursor":0,"entry_count":1,"entries":[{"name_display":"file.txt","name_bytes_hex":"66696c652e747874","path_display":"/tmp/file.txt","path_bytes_hex":"2f746d702f66696c652e747874","kind":"file","size_bytes":"12","mtime_unix_ms":"0","selected":false,"hidden":false}]}}
```

正式 schema 见 `neovifm-core-v0.schema.json`、`neovifm-core-v1.schema.json`、`neovifm-core-v2.schema.json` 和 `neovifm-core-v3.schema.json`。
