# NeoVifm Workbench Alpha 0 Baseline

## 目标

让干净 clone 在 Linux、macOS 和 Windows 获得可重复的构建与测试结果，并让仓库准确描述当前能力。

## 范围

- [x] 搬出调查报告，不纳入 Git 历史。
- [x] 配置只读 upstream 并独立同步 Vifm。
- [x] 修复 Linux GCC developer focused build。
- [x] 修复 focused suite 的 core session 对象依赖。
- [x] 适配上游 matcher API，保持 classic adapter 语义。
- [x] 添加三平台 CI 和 80% TUI 覆盖率门槛。
- [x] 建立当前状态文档并冻结失真的旧计划。
- [ ] fork Draft PR 的 `CI / gate` 三平台全绿。

## 不在范围

- Windows persistence 和 `file-actions-v1`。
- ZIP/SSH 新能力、原生图片协议、插件 SDK 和 agent。
- 安装器、release 和品牌机械重命名。
- 与基线无关的重构。

## 验收

- `upstream/master` 是工作分支祖先，upstream push URL 为 `DISABLED`。
- Linux/macOS developer focused C、TUI 和完整 C 回归通过。
- Windows 真实生成两个 NeoVifm core `.exe` 并运行现有 C/TUI 基础测试。
- `CI / gate` 只有三平台全绿才通过。
- 文档、代码 capability 和平台边界一致。
- `git diff --check` 通过。
