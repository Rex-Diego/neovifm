# NeoVifm M1 开发计划

## 目标

在不改变经典 Vifm 行为的前提下，建立现代浏览 pane 的第一条可测试架构边界：由现有 `view_t` 生成独立、只读、可释放的 `PaneSnapshot`，后续 renderer 只能消费 snapshot。

## 当前阶段

Superseded - 2026-07-11 经架构决策调整为 C core + OpenTUI client，后续见 `.planning/neovifm-hybrid-m0/`。

## 阶段

### Phase 1：PaneSnapshot 数据契约

- [ ] 盘点 `view_t`、`dir_entry_t` 和现有 renderer 实际读取字段
- [ ] 定义最小 snapshot API、所有权与错误语义
- [ ] 写失败测试覆盖空列表、条目复制、selection 和字符串所有权
- **状态：** in_progress

### Phase 2：最小实现

- [ ] 实现 snapshot 构建、深拷贝和释放
- [ ] 保证构建失败不泄漏且不暴露 `view_t` 可变指针
- [ ] 通过新增单元测试
- **状态：** pending

### Phase 3：构建接入

- [ ] 更新 `src/Makefile.am`
- [ ] 更新 `src/Makefile.win`
- [ ] 更新测试构建清单
- [ ] 验证 macOS developer build
- **状态：** pending

### Phase 4：现代 renderer 骨架

- [ ] 增加实验选项和经典 renderer 回退
- [ ] 新 renderer 仅消费 snapshot
- [ ] 支持同步基础字段与终端降级
- **状态：** pending

### Phase 5：验证与审查

- [ ] 相关测试、串行 `make check`、`git diff --check`
- [ ] 代码审查并修复 HIGH/CRITICAL 问题
- [ ] 更新进度与下一阶段入口
- **状态：** pending

## 关键约束

- 不改变文件操作、配置目录、Lua API 或默认 renderer。
- 不在本阶段加入异步 Git/媒体字段、预览流水线或第二套 runtime。
- snapshot 必须拥有自己的字符串和条目数组，renderer 不得回写。
- 先测试后实现；新模块覆盖率目标不低于 80%。

## 风险

| 风险 | 缓解 |
|---|---|
| `view_t` 字段过多导致 snapshot 复制失控 | 第一版只纳入 renderer 所需的同步字段 |
| `dir_entry_t` 含复杂所有权 | 明确逐字段复制，不复制内部缓存指针 |
| 新文件遗漏 Windows 构建 | 同一阶段更新 `Makefile.am` 与 `Makefile.win` |
| snapshot 构建失败泄漏 | 单一清理路径并加入失败路径测试 |

## 错误记录

| 错误 | 尝试 | 处理 |
|---|---|---|
