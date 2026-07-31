# NeoVifm 交接说明

## 当前基线

NeoVifm 当前主线是 Hybrid M0：C core 负责文件系统、Vifm 语义、任务和版本化 JSONL 协议；`clients/tui` 使用 TypeScript/Bun/OpenTUI/SolidJS 负责渲染和输入标准化。经典 Vifm 视图和配置兼容面仍然保留，OpenTUI 是默认产品入口。

当前分支为 `master`，上游 `upstream` 只读指向 `vifm/vifm`，`origin` 指向 NeoVifm fork。交接提交包含源码、测试、`AGENTS.md`、planning 文件和项目文档。

## 本次交接内容

- 正常退出时保存 workspace 现场：左右 pane 路径、pane tab 顺序、活动 tab、活动 pane、排序状态和当前光标目标。
- 无参数启动时恢复上次现场；显式传入路径时显式路径优先，但仍会在正常退出时更新状态。
- 状态文件默认位于 `$XDG_STATE_HOME/neovifm/session.json`，否则为 `~/.local/state/neovifm/session.json`；可用 `NEOVIFM_SESSION_STATE` 覆盖。
- 状态写入使用版本化、有界 JSON、`0600` 权限、同目录独占临时文件、同步和原子替换。损坏、过期或挂载资源状态会安全降级，不会把资源伪造成普通本地目录。
- `↑/↓/←/→` 分别等同于 `k/j/h/l`；排序切换由排序控件和命令入口完成。
- 快捷键矩阵、README、planning 的 findings/progress/task plan 已同步更新。

## 启动与验证

```bash
scripts/fix-timestamps
CFLAGS='-Wno-error=gnu-folding-constant' \
  ./configure --enable-developer --without-glib
make -C src neovifm-core-session

cd clients/tui
bun install --frozen-lockfile
bun run dev
```

显式指定初始 pane 可以运行 `bun run dev /path/to/left /path/to/right`。不传路径时会尝试恢复现场。状态文件可以用 `NEOVIFM_SESSION_STATE=/tmp/neovifm-session.json` 指向临时位置进行隔离验收。

本次交接前已通过：

- `make -C tests neovifm_snapshot`：9849 checks / 96 tests
- TUI unit：144 pass / 545 expects；coverage 85.57% functions / 89.37% lines
- TUI integration：23 pass / 122 expects
- `bunx tsc --noEmit`
- `bun audit`
- `env -u VIFM -u MYVIFMRC make check`
- `git diff --check`

## 已知边界与后续

- 现场保护针对正常退出；崩溃或强制 kill 不保证能写入最新状态。
- 跨重启保存的是每个 tab 当前光标目标；目录级返回历史仍主要是当前进程内存状态。
- ZIP/SSH 挂载 tab 不会作为虚假的本地目录恢复，真实挂载浏览仍依赖 helper 和运行时能力。
- Vifm marks、registers、完整 visual/history、批量重命名、compare/sync、完整 background facade、真实 SSH/ZIP E2E 和原生图形协议预览仍按 planning 文件排队。
- 安装和发布不作为当前主线完成门槛，继续留在后续独立计划。

接手时先阅读 `AGENTS.md`、`docs/NEOVIFM_ARCHITECTURE.md`、`.planning/neovifm-mainline-workbench/task_plan.md`、`progress.md` 和 `findings.md`，再从当前 `master` 的最新提交开始工作。
