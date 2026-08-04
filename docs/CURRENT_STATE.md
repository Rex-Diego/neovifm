# NeoVifm 当前状态

最后核对：2026-08-04

## 阶段

NeoVifm 当前处于 **Workbench Alpha 0 (unreleased)**。

它已经越过只读 Hybrid M0：C core session 使用 protocol v3 发布双 pane、tab、排序、搜索、预览、任务和资源事件，OpenTUI 客户端负责交互与渲染。它仍不是可安装产品，没有 release、安装包或稳定兼容承诺。

经典 `vifm` 继续作为兼容入口和行为基准。当前阶段不进行品牌机械重命名，也不移除经典视图。

## 架构边界

- C 保留文件系统、Vifm 语义、undo、配置与 Lua 兼容基础。
- TypeScript/Bun/OpenTUI/SolidJS 负责新 TUI。
- 两端只通过版本化、不可变 JSONL DTO 通信。
- stdout 只传协议，stderr 只传诊断。
- OpenTUI 是建设中的产品主线，不代表已经达到经典 Vifm 的行为覆盖。

## Protocol v3 capability

`neovifm-core-session` 当前 hello 记录发布：

| Capability | macOS | Linux | Windows | 说明 |
|---|---:|---:|---:|---|
| `preview-session-v3` | 是 | 是 | 是 | 双 pane snapshot、命令确认、preview/task 事件 |
| `workspace-sort-v1` | 是 | 是 | 是 | core-owned 排序 |
| `pane-tabs-v1` | 是 | 是 | 是 | pane tab 新建、切换、关闭和顺序 |
| `open-v1` | 是 | 是 | 是 | 结构化 argv；Windows 没有默认平台 opener |
| `resource-tasks-v1` | 是 | 是 | 是 | 协议入口存在；真实 mount 仍依赖平台 helper |
| `file-actions-v1` | 是 | 否 | 否 | copy/move/mkdir/delete/undo 当前只在 macOS session 发布 |

不要把“capability 被发布”写成“所有 helper 和 E2E 都已经完成”。ZIP/SSH 的真实挂载、取消和恢复仍需要单独平台验收。

## 平台基线

### macOS

- 当前功能和导师验收的主要基线。
- 发布 `file-actions-v1`，使用 kqueue watcher。
- developer 构建使用 Apple Clang 的单项 warning 兼容 flag。
- 文件操作、undo、watcher 和 session 行为仍需 CI 持续证明。

### Linux

- 是干净构建、focused C、完整 C 回归和 TUI integration 的 CI 基线。
- protocol v3、导航、tabs、搜索、排序、预览与结构化 open 可构建和测试。
- 不发布 `file-actions-v1`，也没有 macOS kqueue watcher。

### Windows

- Phase A 只承诺 MinGW64 能构建经典 `vifm`、`neovifm-core-probe.exe` 和 `neovifm-core-session.exe`，并通过现有基础测试。
- 不发布 `file-actions-v1`。
- `open-v1` 没有默认 Win32 opener，只有显式 association 才可能解析成功。
- 默认 session state 路径尚未采用 `LOCALAPPDATA`/`USERPROFILE`，覆盖已有状态文件的替换语义也未完成验证。
- Windows persistence、真实 core/TUI integration 和文件操作属于 Phase B，不能宣称已经支持完成。

## 当前能做什么

- 双 pane 导航、tab、选择、排序和当前目录文件名搜索。
- 不改变目标 pane 状态的快速预览。
- 文本、目录、图片、PDF、音频、视频和 archive 的有界预览或 metadata 降级；效果依赖可用 helper。
- 有界 Vifm `filetype`/`filextype`/`fileviewer` association 解析和结构化 open 结果。
- task center、action/resource task 事件、取消和历史展示。
- 正常退出时保存 workspace session；当前只有 POSIX/macOS 有可靠证据。

## 尚未完成

- 完整 Vifm keymap、marks、registers、visual、history 和命令语义。
- 文件操作与 Vifm `ops`/`background`/`undo` 的最终收口。
- Windows persistence 和 Windows 文件操作。
- ZIP/SSH 跨平台真实挂载 E2E。
- Kitty/Sixel 等原生图形协议、音频封面和完整媒体体验。
- 安装器、发布包、稳定配置迁移和公开 release。
- 插件 SDK 和 agent session。

## 验证入口

Unix developer baseline：

```bash
scripts/fix-timestamps
./configure --enable-developer --without-glib
make -C src neovifm-core-session
env -u VIFM -u MYVIFMRC make -C tests neovifm_snapshot

cd clients/tui
bun install --frozen-lockfile
bun run test:coverage
bun run typecheck
bun audit
bun run test:integration

cd ../..
env -u VIFM -u MYVIFMRC make check
git diff --check
```

macOS configure 时增加：

```bash
CFLAGS='-Wno-error=gnu-folding-constant' \
  ./configure --enable-developer --without-glib
```

Windows CI 复用 `scripts/appveyor/win/`。三平台最终门槛见 `.github/workflows/ci.yml` 的 `CI / gate`。

## 文档优先级

1. `AGENTS.md`：协作和架构约束。
2. 本文件：当前阶段、平台能力和已知问题。
3. `protocol/README.md` 与 schema：协议契约。
4. `.planning/`：实施历史与未完成任务，不作为当前能力声明。

功能变化如果让本文件失真，必须在同一提交中更新；历史 planning 不回写成“早就完成”。
