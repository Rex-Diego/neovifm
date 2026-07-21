# NeoVifm TUI 可用性修复与视觉打磨

## 目标

修复当前 OpenTUI 双栏界面的实际可用性问题：移除常驻 preview pane，确保 Tab 能可靠切换左右 pane；把底部改造成 Starship 风格的分段状态栏与 Total Commander 风格功能键条；以 lsd 的信息层级美化文件行，同时保持无 Nerd Font 和窄终端可降级。

## 当前阶段

Phase 7：范围收敛与 Vifm 原生能力复用

## 本轮边界（2026-07-15）

- 用户要求完成 Phase 6 并提交推送；本轮只交付既有 Hybrid/OpenTUI 实验路径的可用性、安全性和回归修复。
- Yazi 后续只作为多媒体预览能力与视觉美化的参考，不默认引入其异步核心、VFS、并发插件或跨实例事件架构。
- Phase 7 仍待用户确认：盘点和优先复用 Vifm 原生 C/ncurses、`fileviewer`、`quickview`、`vcache`、Lua 与跨平台 watcher；不在本提交删除或回退 Hybrid/OpenTUI 成果。

## 追加修复（2026-07-15）

- [x] 文件上的 `l` 走 F3 同一预览入口，目录上的 `l` 继续由 core 进入目录；补覆盖两条路径的测试。
- [x] 在 core、JSON schema 和 TUI DTO 中新增 `ctime` 排序；Right/Left 顺序完整轮换 Size -> Created -> Modified -> Permissions。
- [x] 宽终端显示 btop 式 Name/Permissions/Size/Created/Modified 标题；空间不足时只显示当前可轮换字段，避免挤掉文件名。
- [x] 运行真实 PTY、TUI integration、typecheck 与完整回归，确认 `h/l` 和全部排序字段真实生效。

## 阶段

### Phase 1：复现问题与定义 UI 契约

- [x] 用现有 OpenTUI test renderer 复现 h/j/k/l、Tab 不生效、preview 占位和底栏信息不足
- [x] 明确宽/窄布局、Starship 分段、Total Commander 功能键与 lsd 行字段契约
- [x] 先写失败测试覆盖键盘、布局与文本帧
- **状态：** completed

### Phase 2：键盘与双栏布局修复

- [x] 删除常驻第三 preview pane，宽屏只保留左右双栏
- [x] 修复 h/j/k/l、方向键、gg/G、Space、Tab、Ctrl-N/P 与 Ctrl-W w/p 的 pane/导航事件归一化，并与 Vifm 语义保持一致
- [x] 保留 preview 数据并由 F3 全工作区 viewer 显示，不占第三栏
- **状态：** completed

### Phase 3：Starship 状态栏与 Total Commander 功能键条

- [x] 设计两行底部：上下文状态段 + F3/F4/F5/F6/F7/F8/F10 功能键段
- [x] 支持 ASCII 降级，不依赖 powerline/Nerd Font
- [x] 对当前未实现的 destructive action 明确 disabled，不制造假功能
- **状态：** completed

### Phase 4：lsd 风格文件行

- [x] 以类型图标/ASCII fallback、名称、大小、权限、mtime、selection/hidden 表达文件
- [x] 稳定列宽与窄屏降级，不让动态字段造成布局跳动
- [x] 颜色按 directory/executable/symlink 与扩展名语义区分
- **状态：** completed

### Phase 4.5：补充交互参考源码

- [x] 将 `linxinhong/ViATc` 以 shallow clone 放入统一参考目录 `/Users/rex/soft/_refs/neovifm/viatc`
- [x] 验证 origin、master 分支与当前 commit，保持参考仓库独立于 NeoVifm 工作树
- **状态：** completed

### Phase 5：验证

- [x] TUI unit/text-frame tests、coverage、typecheck、真实 C session integration
- [x] focused C tests、串行 `make check`、`git diff --check`
- [x] 核对 140/100/60 列布局、ASCII 无 Nerd Font 降级、连续 Space/Tab、F3/F4 显示契约
- **状态：** completed

### Phase 6：真实终端输入、鼠标按钮与状态栏返工

- [x] 用真实 PTY 启动正式入口，复现并记录 `h/j/k/l`、Tab 的原始事件与 command 发送链路
- [x] 先写失败测试覆盖正式入口输入与功能键条鼠标点击
- [x] 让 F3--F10 功能键条具有真实点击行为，鼠标与键盘统一进入同一个 action dispatcher
- [x] F5/F6/F7/F8 接入真实 copy/move/mkdir/delete 流程，作为 VSCode 等抢占 Fn 键时的可点击替代入口
- [x] 重新设计接近 Starship prompt 的连续 powerline segment、层级、颜色与降级方案
- [x] 每个 pane 增加 btop 风格列标题；左右方向键切换 sort 属性，单击列标题设置字段或切换升降序
- [x] sort 必须由 C core 应用并重新发布 snapshot，UI 不得本地重排导致 cursor/selection 身份错位
- [x] 文件行改为 lsd 级图标与紧凑对齐：Nerd Font 图标优先、扩展名/类型配色、无字体时显式 ASCII 降级
- [x] 完成最终 reviewer 回归、全量测试
- [x] 提交并 push
- **状态：** completed

### Phase 7：范围收敛与 Vifm 原生能力复用（等待 Phase 6 交接后执行）

- [x] 以未改动的上游文件为基线，完整盘点 Vifm 已有 `event_loop`、`fswatch`、`fileviewer`、`quickview`、`vcache`、`background`、colorscheme、`classify`、`viewcolumns`、`statusline` 和 `fillchars`
- [x] 新增可选原生 `neovifm-mocha` colorscheme，建立经典 ncurses 的视觉基线且不改变默认主题
- [ ] 明确多媒体目标矩阵：图片、视频缩略图/首帧、音频 metadata/封面，以及 Kitty/Sixel/iTerm2/chafa 等终端能力与纯文本降级
- [ ] 设计最小增强路径：优先扩展现有 viewer 命令、缓存、清理宏和配置，不新建第二套 preview/task/session 架构
- [ ] 设计 Vifm 原生 ncurses 美化路径：默认主题、文件类型/扩展名图标、紧凑列、状态栏、边框、选中态和 Miller/quickview 布局
- [ ] 审计现有 Hybrid/OpenTUI 改动，形成“主线保留 / 实验分支保留 / 可移除”清单；未经用户确认不得执行删除、回退或历史改写
- [ ] 验证 Linux 使用现有 inotify、macOS 使用现有 filemon/polling fallback、Windows 使用原有 change notification；平台差异不得改变 Vifm 主架构
- [x] 用户确认推进 Phase 7；先按最小原生增强路径实施和验收
- **状态：** in_progress

## Phase 6 非范围

- 保持 C core v3 preview/task record 兼容，不恢复常驻第三 preview pane；command 侧只做向后兼容的 additive 扩展。
- 不引入外部图标包、网络依赖或新 runtime。
- ViATc 仅作为交互与快捷键参考，不直接复制其 AutoHotkey 实现。
- 在 Phase 6 交接和用户确认前，不修改或清理其他 agent 的工作树。
- 不再把 Yazi 的 VFS、并发插件、跨实例事件或 Rust 架构视为默认产品需求。

## 错误记录

| 错误 | 尝试 | 处理 |
|---|---|---|
| `keymap.test.ts` 找不到 `src/keymap.js` | RED 1 | 预期失败；实现独立 Vifm keymap 模块 |
| C session test 找不到 `NV_SESSION_MOVE_FIRST/LAST` | RED 1 | 预期失败；扩展 workspace session command 与 v3 parser |
| 旧 App test 仍要求第三 preview pane | GREEN 过渡 | 用户已明确移除；更新断言为宽窄布局都不显示 preview/task pane |
| lsd/Starship 文本帧断言失败，`file-style.js` 不存在 | RED 2 | 预期失败；实现 metadata formatter、文件行与双层底栏 |
| 新底栏与 size formatter 的空格数和初始断言不一致 | GREEN 细化 | 保留稳定 8 列 size 与键帽/标签分段，更新测试为实际文本帧契约 |
| F3/F4 keymap 与 overlay 测试失败 | RED 3 | 预期失败；实现 F3 全工作区 viewer、F4 显式 unavailable notice、F10 quit |
| 扩展后的真实键盘 integration 命中 Bun 默认 5s test timeout | 验证 1 | 将该多阶段真实进程测试预算提高到 20s，再由内部每步 5s wait 精确定位失败键位 |
| Space 后 Tab 无法切回 left | 验证 2 | 定位为键盘 callback 使用陈旧 active pane；改为 core-owned `focus-next` command |
| App callback 单测仍期望显式 `focus right` | GREEN 过渡 | 更新为新的 core-owned `focus-next` 契约 |
| Vifm 一致性测试发现 Home/End、Ctrl-L、ZZ/ZQ 未实现，旧 q/r/Ctrl-C 语义错误 | RED 4 | 按 `normal.c` 键表实现受支持导航，删除不兼容快捷键，并隔离大写键 |
| 用户真实终端中 `h/j/k/l`、Tab 仍无效，底栏按钮不可点击且视觉不达标 | 现场反馈 1 | 旧 test renderer 不能代表真实 PTY；Phase 6 改用正式入口与 PTY/鼠标事件作为验收依据 |
| 双 pane 缺少列标题和交互式排序 | 现场反馈 2 | 增加 core-owned sort command、pane header、方向键字段切换与鼠标升降序测试 |
| 文件行仅显示 `d/x/-` 且左侧留白过大，不符合 lsd | 现场反馈 3 | 参考本地 lsd 源码/真实输出，加入图标能力开关、紧凑 marker 与扩展名图标映射 |
| 功能键被 VSCode 等宿主占用时无法使用，底部按钮又不可点击 | 现场反馈 4 | 鼠标点击必须调用与键盘同一 dispatcher；扩展 core action 能力完成真实 F5--F8 流程 |
| lifecycle test 找不到 `renderUntilDestroyed` export | RED 5 | 预期失败；新增 onDestroy 驱动的正式 renderer 生命周期封装 |
| Phase 6 UI tests 缺少 sort-cycle、click ids、powerline header 与 fancy icons | RED 6 | 预期失败；实现 core sort、统一 dispatcher、Catppuccin powerline 与 lsd icon theme |
| C session test 缺少 `NV_SESSION_SORT_BY/SORT_CYCLE` | RED 6 | 预期失败；扩展 snapshot sort API 与 workspace command |
| C action test 缺少 `NV_SESSION_COPY/MOVE_FILES/MKDIR/DELETE` | RED 7 | 预期失败；接入 core-owned 文件动作与安全的 no-overwrite filesystem adapter |
| 直接复用完整 `io/ior` 导致最小 core probe 链接缺少 I/O 依赖 | GREEN 细化 | 将 headless 文件动作收敛到已有 `compat/neovifm_fs`，保持 probe/session 的小型依赖闭包 |
| 真实 PTY 中 Unicode cursor marker 在单列单元内不可见 | 视觉验收 | 改用稳定单宽 `>`/`*` marker，Nerd Font 只用于文件类型图标 |
