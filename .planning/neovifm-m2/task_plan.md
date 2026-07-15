# NeoVifm M2：可取消预览与任务事件首切片

## 目标

在保留 M1 双 pane 工作台、classic renderer、文件操作/undo 和 Lua API 的前提下，交付首个可观测、可取消的异步预览切片：C core 在进程内运行有限任务队列，按 pane/cwd/path-bytes/generation 发布不可变任务和预览 DTO；OpenTUI 显示文本/目录预览与 task drawer。

## 当前阶段

Phase 4：验收完成

## 阶段

### Phase 1：任务与预览协议契约

- [x] 盘点 `background.c`、M1 session、现有 JSONL v2 与 TUI reducer 的复用/隔离边界
- [x] 定义 M2 protocol v3：task lifecycle、preview snapshot、generation、pane/cwd/path identity、取消与错误语义
- [x] TDD 覆盖非法 context、序列、generation、取消、timeout 和资源上限
- **状态：** completed

### Phase 2：C 进程内任务与预览 core

- [x] 实现不触碰 ncurses/view_t 的 task queue；worker 只处理 I/O，主循环排空事件
- [x] 实现文本和目录 preview；预览输入使用 path bytes，显式 cwd/pane/generation/cancellation
- [x] 新 cursor/preview request 取消旧 generation；失败、取消、超时均发布结构化 event
- [x] 在 session 中实现 10--16 ms 合并窗口并保持 stdout 仅协议、stderr 仅诊断
- **状态：** completed

### Phase 3：OpenTUI preview 与 task drawer

- [x] 支持 v3 parser/reducer/client；严格校验 record、字段、顺序与输入尺寸
- [x] 宽终端显示双 pane + preview/task drawer；窄终端保持可操作降级
- [x] 显示 queued/running/done/failed/cancelled，并只渲染最新 generation 的 preview
- [x] 写 unit、文本 snapshot 与真实 C session integration
- **状态：** completed

### Phase 4：验收与后续边界

- [x] focused C tests、串行 make check、TUI tests/coverage/typecheck/integration、git diff --check（Bun scanner 与 npm audit 均因项目锁文件/配置缺失不可用，已记录）
- [x] 复核取消/超时/失败/worker-thread 边界和 classic 无回归
- [x] 记录 M2 未做项：图片/archive/Git、copy/sync/rename 会话、daemon/network/Rust/C++
- **状态：** completed

## 非范围

- 不引入 daemon、socket、网络协议、Rust/C++ 或替换 TUI runtime。
- 不改写 copy/move/delete、undo、Lua API、二进制名、配置目录或 classic renderer。
- 图片、archive、Git 元数据和文件操作 session 留在 M2 后续切片。

## 验收标准

1. 每项 task/event 都携带不可变 task id、pane、cwd、path bytes、generation 和状态。
2. worker 不读写 ncurses/OpenTUI 或 `view_t`；主线程在合并窗口内提交 DTO。
3. cursor/preview 变化取消旧 generation；过时结果永不覆盖最新 preview。
4. 文本、目录、失败、取消、超时和窄终端 UI 可验证。
5. classic Vifm 回归与新增 C/TypeScript 测试覆盖率均满足项目门槛。

## 错误记录

| 错误 | 尝试 | 处理 |
|---|---|---|
