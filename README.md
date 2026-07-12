# NeoVifm

NeoVifm 是一款正在建设中的终端文件工作台，以 Vifm 的 Vim 语义、双栏工作流和成熟文件操作为基础，吸收 btop 的高信息密度 TUI、OpenCode 的会话与命令工作流、Yazi 的异步与预览体系，以及 lsd 的结构化目录表达。

本项目是独立社区 fork，与 Vifm、btop、OpenCode、Yazi、lsd 的维护团队没有隶属或官方合作关系。

## 当前状态

NeoVifm 处于 Hybrid M0 原型阶段，尚未发布可安装版本，也不应通过系统包管理器安装 `vifm` 来替代本项目。

- 当前代码基线：Vifm 0.15 开发版。
- 经典二进制、配置目录和 Lua API 仍使用 `vifm`，用于保持兼容。
- 已加入实验性 `neovifm-core-probe`：无 ncurses、无用户配置、只读输出目录快照。
- 已加入 `clients/tui`：TypeScript/Bun/OpenTUI/SolidJS 客户端，通过版本化 JSONL 协议消费 C core 快照。
- 当前原型仅浏览和展示，不执行 copy/move/delete，不替换经典 Vifm 客户端。
- 架构决策见 [ADR 0001](docs/adr/0001-hybrid-core-opentui.md)，协议见 [NeoVifm Core Protocol v0](protocol/README.md)。

## 产品方向

- **Vifm**：Vim 模式、映射、命令、寄存器、双栏、文件操作、undo 和 Lua。
- **btop**：紧凑、清晰、自适应的终端信息设计。
- **OpenCode**：任务/会话生命周期、统一命令入口、事件流和权限边界。
- **Yazi**：非阻塞任务、可取消预览、预加载、VFS 和终端图像适配。
- **lsd**：图标、颜色、树和结构化文件字段。

详细边界和阶段计划见 [NeoVifm 产品与架构基线](docs/NEOVIFM_ARCHITECTURE.md)。参与开发前请阅读 [项目协作约定](AGENTS.md) 和 [Vifm 的开发说明](HACKING.md)。

## 原型运行

NeoVifm core 沿用 Vifm 的 Autotools 构建；新 TUI 使用 Bun 1.3 或更高版本。通用 C 依赖和平台说明见 [INSTALL](INSTALL)。macOS 还需要 `autoconf` 和 `automake`。

构建 core probe：

```bash
scripts/fix-timestamps
CFLAGS='-Wno-error=gnu-folding-constant' \
  ./configure --enable-developer --without-glib
make -C src neovifm-core-probe
```

启动新 TUI：

```bash
cd clients/tui
bun install --frozen-lockfile
bun run dev ../..
```

`q` 或 `Ctrl-C` 退出。也可通过 `NEOVIFM_CORE_PROBE=/path/to/probe` 指定 core probe。

## 测试

```bash
env -u VIFM -u MYVIFMRC make -C tests neovifm_snapshot

cd clients/tui
bun run test:coverage
bun run typecheck
bun audit
bun run test:integration

cd ../..
env -u VIFM -u MYVIFMRC make check
```

测试必须串行执行。现有 suite 会使用共享相对路径，`make -jN check` 存在竞态。

## 上游同步

- `origin`：NeoVifm fork。
- `upstream`：只读的 [vifm/vifm](https://github.com/vifm/vifm)。
- Vifm 的缺陷和 NeoVifm 的缺陷必须分别提交到对应项目；不要把 NeoVifm 功能请求提交给 Vifm 上游。

## 许可证与来源

Vifm 基线按 GNU GPL v2 或更高版本分发，详见 [COPYING](COPYING) 和现有源码头。

NeoVifm 默认只借鉴其他项目的公开设计，不复制实现。任何第三方代码导入都必须先记录来源、许可证和 NOTICE 要求；特别是 Apache-2.0 代码不能在未完成兼容审查时直接合入。若未来导入 Apache-2.0 实现，组合发行需要采用兼容的 GPLv3 路径并保留相应声明。

Vifm 的原作者和贡献者信息见 [AUTHORS](AUTHORS) 与 [THANKS](THANKS)。
