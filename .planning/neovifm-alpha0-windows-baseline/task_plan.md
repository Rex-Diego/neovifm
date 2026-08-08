# NeoVifm Windows Session Baseline

## 目标

让 Windows 从“能生成 exe”进入可运行、可恢复、由真实 CI 证明的 Workbench Alpha 0 会话基线。

## 范围

- [x] 从导师 `master@58f26509` 创建独立分支。
- [x] 以独立 merge commit 同步 `upstream/master@f5d60eaa`。
- [x] 使用 Unicode Windows 路径处理 core 参数、preview 和 session state。
- [x] 让无输入 session 持续发布 preview/task/resource 事件。
- [x] 支持默认状态路径、同目录临时文件、替换已有状态和跨进程恢复。
- [x] 运行 Windows NeoVifm focused C fixtures 和真实 core/TUI integration。
- [x] 保持 Linux、macOS 完整回归和 80% 覆盖率门槛。
- [x] 更新当前状态、README 和交接事实。

## 不在范围

- Windows/Linux copy、move、mkdir、delete 和 undo。
- Windows watcher 和默认 Win32 opener。
- 安装器、release、tag、品牌机械重命名或 protocol/schema/public API 变化。

## 验收

- Windows 无输入时能收到 hello、workspace 和初始 preview。
- `%LOCALAPPDATA%\neovifm\session.json` 能创建、替换并恢复 Unicode 会话。
- Windows 不发布 `file-actions-v1`。
- Windows focused C 和 5 个真实 core integration tests 通过。
- Linux、macOS、Windows 和 `CI / gate` 全绿。
