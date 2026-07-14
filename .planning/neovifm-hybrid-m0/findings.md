# NeoVifm Hybrid M0 Findings

## 已确认

- OpenCode 当前核心为 TypeScript/Bun monorepo，TUI 使用 OpenTUI + SolidJS。
- 值得吸收的是逻辑 client/server、事件流、session/task 和版本化 SDK。
- NeoVifm 的文件系统与操作内核继续使用 C，OpenTUI 作为新客户端验证。
- 原先的 `PaneSnapshot` 仍保留，但升级为跨进程 DTO。

## 待确认

- OpenTUI 当前最小启动 API和测试方式。
- C probe 最合适的复用层：`compat`、`utils/fs` 或现有 filelist。
- JSON Schema 到 TypeScript/C 的生成与校验是否值得在 M0 引入工具。

## 当前决策

- 本机已有 Bun 1.3.14，可直接建立官方 OpenTUI 0.4.3 原型。
- C 端已有 Parson、`os_opendir/os_readdir/os_lstat` 和 FileType 转换能力。
- M0 使用 `hello -> snapshot|error -> EOF` 的 JSONL 流。
- stdout 只传协议，stderr 只传诊断。
- 64 位数值用十进制字符串；路径同时传 display UTF-8 与 raw bytes hex。

## 2026-07-14 会话恢复核对

- `.planning/.active_plan` 指向本计划；根目录调研计划已完成，`neovifm-m1` 已被 Hybrid M0 取代，均不作为后续执行对象。
- 当前 `HEAD` 已包含 `src/neovifm/` 的 C probe、`tests/neovifm_snapshot/` 测试、`clients/tui/` 客户端及其单元和集成测试；计划进度仍停在最初的 RED 记录，不能仅凭文件存在判断阶段验收完成。
- 工作树干净，且现有 macOS developer 构建产物存在；下一步应以聚焦 C、TUI、真实 probe 集成、覆盖率和 typecheck 的实际结果校准阶段状态。
- `tests/Makefile` 的 C suite 链接会排除 `core_probe.c`，因此必须额外运行 TUI 的真实 probe 集成测试来覆盖子进程 JSONL 生命周期。

## 2026-07-14 实现与安全复核

- `core_probe.c` 只依赖 stdio 和 snapshot/JSON 模块：先写 `hello`，再写唯一 `snapshot` 或 `error`，没有 ncurses 初始化、vifmrc 读取、shell 执行或网络路径。
- `pane_snapshot.c` 使用 `opendir/readdir/lstat` 做只读枚举；目录项、路径和 JSON 字段均为 probe 自有分配，展示字符串会替换控制字符和双向控制字符，原始字节以 hex 保留。
- 当前 C 测试覆盖空目录、常规文件/目录、隐藏项、控制/双向字符、重建失败时保留旧 snapshot、特殊文件类型、数组扩容和结构化错误；协议测试覆盖 64 位数字字符串、所有 entry kind、stat 错误和错误路径标识。
- 仍需用真实子进程集成测试核验 core probe 的 `hello -> terminal record -> EOF` 生命周期，以及确认 TypeScript 的行/流大小限制和进程超时处理与 C 输出相容。

## 2026-07-14 验证环境

- 当前工作树的 developer 配置未启用 sanitizer（`src/Makefile` 中 `SANITIZERS_CFLAGS` 为空）；`configure` 支持 `--with-sanitize=basic`（AddressSanitizer + UndefinedBehaviorSanitizer）。
- 为不重配用户当前的开发构建，若执行 sanitizer 验证应使用独立的临时 out-of-tree build。

## 2026-07-14 阶段缺口（待修复）

- Phase 2 的原型边界要求复用 compat/filesystem helpers；当前 `pane_snapshot.c` 直接使用 POSIX 的 `opendir/readdir/lstat/closedir`，而 `src/compat/os.h` 已提供跨平台 `os_*` 封装。
- `src/Makefile.win` 尚未定义 `neovifm-core-probe`，因此当前 C probe 不能满足跨平台构建约束。
- Phase 3 计划要求 schema/parser 和状态 reducer 测试；现有 client 有 schema/parser 测试，但尚未存在 reducer/state 模块或其测试，且 `core-client` 在收齐进程输出后才一次性返回终态。

## 2026-07-14 TUI 审查缺口（待修复）

- JSONL decoder 当前会忽略空白记录和 EOF 空白尾巴，放宽了“每行完整 envelope、以换行结束”的协议 framing。
- 记录限额按已解码的 JavaScript 字符串检查，且请求可传入无限大的 safe integer；缺少预解析 UTF-8 字节、总输入、条目数和展示字段长度的不可突破上限。
- DTO 只有 TypeScript `readonly`，运行时仍可变；renderer 也没有独立限制由未来 producer 传入的控制字符展示文本。
- CLI 入口在 probe 完成前没有可取消的 UI 状态，并将 `CoreClientError` 的结构化诊断折叠成普通 message、未设置非零退出码。
- 这些问题不影响已通过的基础 happy-path 测试，但阻断“客户端校验输入大小、不可变 DTO、任务取消/可观察错误”的 M0 验收；修复必须先补针对性失败测试。

## 2026-07-14 后续审查修复（待最终复核）

- C producer 原先只限制 4096 个条目，长名称/路径仍可让 snapshot JSON 超过 client 的 4 MiB 单记录上限。现已在 snapshot 构建时按保守 JSON byte budget 截断，并在 serializer 中以 Parson 的实际序列化长度作第二道精确上限；core probe 对两种越界均输出 `snapshot-too-large`、`E2BIG`、`retryable: false`。
- C 端现将 display 与 hex 字段限制在 schema/M0 的 16 KiB/32 KiB 内；超出路径上限的错误记录省略可选 path identity，避免错误记录本身再次越界。
- Windows adapter 不再以 `_wstat()` 作为 lstat 等价物：用 `FindFirstFileW()` 的 no-follow `WIN32_FIND_DATAW` 识别 `IO_REPARSE_TAG_SYMLINK`，并以目录项填充有限 metadata。当前 macOS 没有 MinGW；已能静态生成 Windows recipe，真实 Windows 悬挂 symlink 验证仍应交由 CI。
- OpenTUI 根级 `<App {...props()} />` 经真实 `testRender` 验证可从 loading 响应式切换到 snapshot；不需要额外 wrapper。renderer 初始化失败时则必须 abort 并等待 probe，且不能让取消状态码覆盖 renderer 自身错误。
- JSON Schema 标准无法表达 `cursor < entry_count` 的动态关系；schema `$comment` 明确该语义，C producer 和 TypeScript runtime parser 都强制执行，测试覆盖该漂移。
- TUI protocol failure 会立即取消 stdout/stderr reader 与 probe；即使被杀 probe 的后代继续持有 pipe，也优先返回 `protocol` 而不是被 timeout 遮蔽。此时不承诺保留后续 stderr。
- `maximumStderrChars` 只限制保留的诊断文本，而不是 deadline 内可被 trusted local probe 产生的原始 stderr 总字节数；M0 的 producer 在本地可信边界内，若未来扩展为不可信或长生命周期 producer，应另加原始字节预算/速率策略。
