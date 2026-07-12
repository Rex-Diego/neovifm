# Findings & Decisions

## Requirements
- 调研 Total Commander 深度赏析，优先找用户记忆中的知乎文章与 “two window god” 线索。
- 寻找其他能解释 Total Commander 设计哲学的文章/官方文档。
- 比较 Total Commander 和 ViFM 在底层设计哲学、框架和抽象上的差距。
- 下载有用素材，形成图文并茂的 Markdown 调研报告。

## Research Findings
- 本地 README 将 ViFM 定位为 “Vim-like file manager”，核心不只是快捷键，而是 Vim 的 modes、options、registers、commands 等模型；同时强调 Unix philosophy：提供可定制手段，而不是固定解决方案。
- 本地 `HACKING.md` 显示源码按 `engine/`、`modes/`、`ui/`、`io/`、`int/`、`lua/` 等模块组织：底层重心是 Vim 式命令/模式引擎 + curses 文件视图 + 外部工具集成。
- `src/filetype.h` 中 ViFM 的打开/查看抽象主要是 matcher -> associated program/viewer，viewer 分为 textual、graphical、pass-through；这是“把动作绑定到文件”的抽象，不是 Total Commander 那种“把资源类型纳入统一面板语义”的抽象。
- `src/types.h` 的 `FileType` 仍是传统文件系统对象枚举：link/dir/device/exec/reg 等，没有看到“压缩包即目录/虚拟资源即目录”的一等模型。
- Total Commander 官方插件页把扩展分成四类：Packer、File system、Lister、Content。关键不只是插件多，而是每类插件都挂到一个稳定用户动词上：打包/进入、文件系统访问、F3 查看、列/搜索/批量改名可用的内容字段。
- Total Commander 官方截图说明主窗口包含文件夹标签、命令行、底部功能键按钮，功能键按钮还能作为拖放目标；这说明 TC 的 UI 抽象是“面板 + 命令行 + 固定动词栏”三者统一。
- Christian Ghisler 访谈中明确说做 TC 的原因是 Windows 上缺少双窗口文件管理器；他解释 TC 长盛的原因是既简单又强大：可以从两目录间复制这类简单任务起步，随着技能增长再学热键、批量重命名、插件等。
- Softpanorama 的 OFM 文章把 Orthodox File Manager 的核心归纳为：双对称面板、底部终端/命令行、用户菜单/扩展菜单与宏变量、键盘优先、可编程和朴素界面下的高功能密度。这个框架适合用来比较 TC 与 ViFM。
- ViFM 官方文档显示 `:filetype` 可为 zip 配置 FUSE_MOUNT、查看内容、就地解压等动作；`:fileviewer` 捕获命令输出放进 pane；`%u/%U` 可把命令输出解释为路径列表并组成 custom view。这些是很强的 Unix 管道能力，但仍然偏“命令输出变列表”，不是统一 Resource Provider 模型。
- ViFM 本地 `filelist.h` 的 custom view API 能把路径列表装进 view；`running.h` 的 `rn_for_flist()` 负责运行命令并把输出解析为 custom view；`macros.h` 也有菜单、预览、custom view、split 等输出重定向宏。这是 ViFM 追上/超越 TC 的重要支点。

## Technical Decisions
| Decision | Rationale |
|----------|-----------|
| 报告目录使用 `research/total-commander-vifm/` | 将研究产物、素材、来源索引集中管理 |

## Issues Encountered
| Issue | Resolution |
|-------|------------|
| `market-research` skill listed but local `SKILL.md` 不存在 | 继续按本地计划文件 + 浏览检索执行 |

## Resources
- 本地：`README`
- 本地：`HACKING.md`
- 本地：`src/filetype.h`
- 本地：`src/types.h`
- 本地：`src/ui/fileview.h`
- Total Commander 官方插件/插件类别：https://www.ghisler.com/addons.htm 和 https://www.ghisler.com/plugins.htm
- Total Commander 官方主窗口截图：https://www.ghisler.com/screenshots/en/01.html
- Total Commander 作者访谈：https://www.thinkjam.org/zoptuno/archives/2007/02/interview-with-christian-ghisler.html
- Orthodox File Manager 哲学文章：https://softpanorama.org/Articles/introduction_to_orthodox_file_managers.shtml
- ViFM 官方文档：https://vifm.info/docs/
- ViFM app 文档：https://vifm.info/docs/v0.14.4/vifm-app.txt
- ViFM Lua 文档：https://vifm.info/docs/v0.14.4/vifm-lua.txt

## Visual/Browser Findings
- TC 主窗口官方截图展示了两个文件列表、底部功能键动词栏、命令行、标签页；可以作为报告中的“TC 面板语法”配图。
- ViFM 仓库自带 `data/graphics/screenshot.png` 和 `screenshot2.png`，分辨率约 1900x1050，适合作为 ViFM 当前 UI 形态配图。
