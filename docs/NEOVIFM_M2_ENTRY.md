# NeoVifm M2 入口评估

M1 已建立双 pane、只读 session、不可变 workspace DTO 和 macOS 局部刷新。M2 的目标是异步预览与任务中心，不改变经典 Vifm renderer、文件操作、undo、Lua API、二进制名或配置目录。

## 允许的首个纵向切片

1. C core 定义进程内、类型化的 task event queue：`queued`、`running`、`done`、`failed`、`cancelled`。
2. 预览请求显式携带 pane、cwd、当前条目的原始 path bytes、generation 与取消上下文；新 cursor 会取消旧 generation。
3. worker 只处理阻塞 I/O/解码，主线程将完成事件合并为不可变 snapshot，再交给 TUI 渲染。
4. TUI 先显示文本/目录预览及 task drawer；图片、archive、Git 字段和文件操作会话留在后续切片。

## 进入条件与量化验收

- 新任务 API 具有 unit/integration tests，且失败、取消、超时和 generation replacement 都可观测。
- 单次 preview 不能阻塞输入循环；完成事件在主线程批量提交，目标合并窗口为 10--16 ms。
- 每项异步任务带独立 pane/cwd/selection/cancellation context；后台不得操作 ncurses 或 OpenTUI 状态。
- 默认 classic renderer 的 `make check` 必须保持通过；TUI 继续验证窄终端、无 Nerd Font 和低色彩降级。

## 明确不做

- 不引入 daemon、socket、网络协议、Rust/C++ 或运行时替换；若未来需要，先新增 ADR 并给出可量化收益。
- 不把 preview worker 直接接入 Vifm 的可变 `view_t`，不让插件依赖内部结构体。
- 不在 M2 首切片改写 copy/move/delete、undo 或 Lua 行为。
