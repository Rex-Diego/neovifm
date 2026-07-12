# NeoVifm 产品与架构基线

状态：实施契约
日期：2026-07-11

## 产品定义

NeoVifm 是键盘优先、双栏优先、异步优先的终端文件工作台。它把文件浏览、结构化预览、文件操作、外部命令、长期任务和可选智能代理放在一套统一交互中。

它不是：

- 给 Vifm 换皮；
- 把多个项目的界面拼在一起；
- 为追求现代技术栈而重写成熟文件操作；
- 第一版就内置完整 AI coding agent；
- 用大量默认动画和装饰牺牲终端效率。

## 借鉴边界

| 范本 | 应吸收 | 不照搬 |
|---|---|---|
| [Vifm](https://github.com/vifm/vifm) | Vim 模式、映射、命令、寄存器、双栏、文件操作、undo、配置与 Lua | 全局状态、进程 CWD 传递上下文、model/UI 混合的 `view_t` |
| [btop](https://github.com/aristocratos/btop) | 高信息密度、自适应 panel、实时状态、主题、鼠标和清晰的选中态 | 监控器专用图表语义、持续全屏重绘 |
| [OpenCode](https://github.com/anomalyco/opencode) | 会话与任务生命周期、统一命令入口、事件流、权限边界、前后端协议 | 把文件管理器核心绑定到 LLM、Node/Bun 或远端服务 |
| [Yazi](https://github.com/sxyazi/yazi) | 非阻塞 I/O、可取消任务、预加载、预览协议、VFS、并发插件和跨实例事件 | 在未建立兼容边界前重写为 Rust 或复制其 UI |
| [lsd](https://github.com/lsd-rs/lsd) | 图标、颜色、树、权限/大小/时间/Git 等结构化字段和独立主题配置 | 把静态 `ls` 输出模型直接塞进交互式 pane |

这些项目默认只提供设计参照，不复制实现。NeoVifm 新代码沿用 GPL-2.0-or-later；任何第三方代码导入都必须记录来源、许可证和 NOTICE。Apache-2.0 实现若被合入，组合发行必须走兼容的 GPLv3 路径，不能继续按 GPLv2-only 处理。

## 核心原则

1. **保留成熟内核**：优先保留 Vifm 的 `engine/`、`io/`、`ops`、undo、文件枚举、watcher、filetype 和 Lua 语义。
2. **快照驱动渲染**：pane model 产生不可变 snapshot，renderer 只消费 snapshot，不持有领域对象的可变指针。
3. **显式上下文**：路径、pane、selection、workspace、权限和取消信号通过参数传递。
4. **UI 永不等待重活**：I/O、预览、Git、目录大小、媒体解析和外部命令进入 task runtime。
5. **能力驱动动作**：动作根据资源和 provider 的 capability 决定是否可用，而不是散落扩展名判断。
6. **渐进兼容**：经典 Vifm 视图在新视图达到行为等价前保持默认可用。
7. **可降级显示**：图标、truecolor、图片协议和鼠标都是增强，不是正确运行的前提。
8. **插件有边界**：Lua 插件使用版本化 facade、事件和 capability，不直接穿透内部结构体。

## 当前基础

可以直接继承：

- `src/engine/`：按键树、模式、命令、变量、表达式、选项、补全和自动命令；
- `src/io/`、`src/ops.c`、`src/fops_*`、`src/undo.c`：跨平台文件操作与恢复能力；
- `src/filelist.c`：目录枚举、排序、过滤、历史、custom view 和 watcher；
- `src/background.c`：外部命令、内部任务、进度、取消和错误收集；
- `src/filetype.c`、`src/running.c`：打开、查看、外部程序和宏；
- `src/lua/`、`src/plugins.c`：命令、键、事件、列、handler、job 和插件加载。

必须逐步拆开的耦合：

- `view_t` 同时承载 model、selection、history、watcher、window、layout 和 redraw；
- `lwin`、`rwin`、`curr_view`、`curr_stats` 等全局状态跨层传播；
- 主循环用进程 CWD 表示当前 pane 上下文；
- `fops_*` 同时负责领域操作、消息、selection、reload 和 redraw；
- mode 依赖 enum、平行数组和多处 switch；
- Lua 插件共享单一进程级状态，缺少版本和卸载生命周期。

## 目标架构

```text
Terminal input / IPC / plugin events
                 |
                 v
        Command and mode engine
                 |
                 v
          Application dispatcher
          /          |           \
         v           v            v
   Pane models   Action service   Session service
         |           |            |
         +-----------+------------+
                     |
                     v
              Task runtime
       (priority, progress, cancel, errors)
                     |
                     v
        Immutable snapshots and events
                     |
                     v
        Layout + renderer + terminal backend

Providers: file / archive / search / remote / plugin
Fields: type / icon / permission / size / time / git / media
Renderers: text / code / image / table / tree / archive / hex
```

### 领域层

- `PaneModel`：位置、游标、selection、排序、过滤和历史。
- `ResourceEntry`：稳定 ID、URI、类型、基础字段和 capability。
- `PaneSnapshot`：一次渲染所需的不可变条目、布局提示和状态。
- `ActionRequest/ActionResult`：copy、move、delete、open、preview 等结构化协议。

### 运行时

- 统一外部命令和内部任务的状态机：queued、running、blocked、done、failed、cancelled。
- 每个任务具有 ID、父任务、workspace、资源 lane、优先级、进度、取消、错误和可选输出流。
- 文件操作、外部命令、目录统计和预览共享调度协议，但使用独立并发上限，预览不得挤占 copy/delete。
- 后台任务只发布类型化事件；主线程批量排空事件并合并 redraw，避免固定帧率和无意义全屏刷新。
- UI 只订阅事件并提交 snapshot，不在后台线程操作 ncurses。
- 文件操作形成可恢复的操作会话，保存来源 pane、输入集合、步骤、结果和错误；未来 agent session 复用同一会话外壳。

### 展示层

- 根据终端尺寸选择双栏、单栏+预览或紧凑布局。
- 主题、图标和字段独立配置；字段宽度稳定，动态内容不得造成布局跳动。
- 字段和图标使用 `name -> extension -> file type -> fallback` 的确定性解析顺序，并用 `wcwidth` 校验显示宽度。
- preview 是 renderer pipeline，不是散落的外部命令特例；请求包含路径、mime、mtime、geometry 和 generation，游标变化时取消旧 generation。
- command palette、task/session drawer 作为注册式 overlay，不能继续扩大 mode switch。

### 插件层

- 第一阶段继续使用 Lua。
- facade 逐步拆成 actions、fields、renderers、providers、events、tasks。
- 插件声明 API 版本和 capability；加载失败不得破坏主界面。
- OpenCode 集成先作为可选 session provider，而不是核心依赖。

## 第一纵向切片：Hybrid M0 只读浏览

目标是先证明 C core 与现代 TUI 可以通过稳定边界协作，再逐步把 Vifm 的真实 pane、操作和任务能力移入 headless core。

### 范围

- `src/neovifm/` 提供独立、只读、headless 的目录 snapshot probe。
- `protocol/neovifm-core-v0.schema.json` 定义 `hello -> snapshot|error` 的 JSONL 契约。
- `clients/tui/` 使用 TypeScript、Bun、OpenTUI 和 SolidJS 渲染快照。
- 客户端校验协议版本、字段、记录顺序和进程退出状态，不直接访问文件系统领域对象。
- 宽终端显示文件列表与详情，窄终端降级为单列表；标记使用 ASCII，不依赖 Nerd Font。
- 经典 Vifm renderer 和默认行为保持不变。

### 非范围

- 不重写 copy/move/delete；
- 不删除或更改经典 ncurses 客户端；
- 不引入 Rust runtime；
- 不进行全仓品牌重命名；
- 不改变现有配置目录和 Lua API；
- 不实现 copy/move/delete、Git 异步字段、预览流水线、任务中心、command palette 或 session overlay；
- M0 probe 是短命进程，不引入 daemon、socket、认证或网络。

### 测试先行

1. 为目录 snapshot 的空目录、条目、隐藏文件、错误和序列化写 C 单元测试。
2. 为 JSONL 任意分块、UTF-8、字段校验和错误记录写 TypeScript 单元测试。
3. 为窄终端、宽终端和 ASCII 标记生成稳定的 OpenTUI 文本快照测试。
4. 用真实 C probe 驱动 TypeScript client 和 OpenTUI test renderer 做集成测试。
5. 串行完整运行 `make check`、Bun coverage、typecheck 和依赖审计。

### 验收标准

- `neovifm-core-probe <path>` 输出符合 v0 契约的快照或结构化错误；
- Bun 客户端能启动 probe、校验输入并渲染目录；
- renderer 不读取或修改 snapshot 之外的 core 状态；
- 宽窄终端均可用，无 Nerd Font 时信息仍完整可读；
- 经典 Vifm 构建和行为零回归；
- 新增 TypeScript 模块行覆盖率达到 80% 以上，C 模块有聚焦单元和 sanitizer 验证。

## 路线图

### M0：混合架构验证

- 明确 NeoVifm 产品契约、上游同步策略和构建基线；
- 建立 C core probe、版本化协议和 OpenTUI 只读客户端；
- 保留 Vifm 兼容面，避免过早重命名。

### M1：Headless Pane Core

- 将 probe 的独立目录扫描替换为可测试的真实 `PaneModel` adapter；
- 加入导航、双 pane、selection、watcher 和增量 snapshot/event；
- 经典 renderer 继续保留并作为兼容基准。

### M2：异步预览与任务中心

- 类型化 UI 事件队列、局部 dirty 标记和 10-16ms redraw 合并；
- 统一 task runtime 与有限并发资源 lane；
- Git/媒体等异步字段和缓存；
- 文本、代码、图片、目录、archive 预览；
- generation cancellation、缓存键、优先级、预加载、进度、取消、重试和错误抽屉；
- copy/sync/rename 等操作会话及父子任务关系。

### M3：动作与会话工作台

- 注册式 command/action palette；
- shell、Git、搜索和可选 agent session；
- 权限确认、结构化输出和可恢复会话。

### M4：资源 Provider

- `file://`、`archive://`、`search://`、`remote://`；
- provider-aware copy/move/view/enter；
- capability 和明确的 unsupported 错误。

### M5：稳定插件 SDK 与品牌迁移

- 版本化 providers、fields、renderers、actions、tasks；
- 插件生命周期和隔离；
- 在兼容迁移方案完成后再切换二进制、配置目录和公开品牌。

## 决策门槛

下列变化必须先写 ADR：

- 引入 Rust/C++ 或替换 ncurses；
- 改变配置目录、二进制名或 Lua API；
- 改变文件操作/undo 语义；
- 引入常驻服务进程；
- 默认启用网络、遥测或 LLM；
- 无法通过 feature flag 或兼容 adapter 渐进交付的架构改动。
