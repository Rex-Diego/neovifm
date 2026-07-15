# NeoVifm TUI 可用性修复与视觉打磨

## 目标

修复当前 OpenTUI 双栏界面的实际可用性问题：移除常驻 preview pane，确保 Tab 能可靠切换左右 pane；把底部改造成 Starship 风格的分段状态栏与 Total Commander 风格功能键条；以 lsd 的信息层级美化文件行，同时保持无 Nerd Font 和窄终端可降级。

## 当前阶段

完成

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

## 非范围

- 本轮不扩张到 copy/move/delete 的真实文件操作协议；未接入的功能键必须显式禁用。
- 不改变 C core v3 preview/task 协议，不恢复常驻第三 preview pane。
- 不引入外部图标包、网络依赖或新 runtime。
- ViATc 仅作为交互与快捷键参考，不直接复制其 AutoHotkey 实现。

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
