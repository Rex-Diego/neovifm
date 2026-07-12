# Source Index

## 下载的本地素材

| 本地路径 | 来源 | 用途 |
|---|---|---|
| `assets/tc-main-window.png` | https://www.ghisler.com/screenshots/en/01.html | Total Commander 主窗口截图 |
| `assets/tc-breadcrumb.png` | https://www.ghisler.com/screenshots/en/09.html | Total Commander 路径/面包屑导航截图 |
| `assets/vifm-main.png` | `data/graphics/screenshot.png` | ViFM 主界面截图 |
| `assets/vifm-miller.png` | `data/graphics/screenshot2.png` | ViFM Miller/预览式界面截图 |

## 归档的网页快照

| 本地路径 | 来源 |
|---|---|
| `sources/tc-addons.html` | https://www.ghisler.com/addons.htm |
| `sources/tc-plugins.html` | https://www.ghisler.com/plugins.htm |
| `sources/tc-screenshot01.html` | https://www.ghisler.com/screenshots/en/01.html |
| `sources/tc-screenshot09.html` | https://www.ghisler.com/screenshots/en/09.html |
| `sources/ghisler-interview.html` | https://www.thinkjam.org/zoptuno/archives/2007/02/interview-with-christian-ghisler.html |
| `sources/softpanorama-ofm.html` | https://softpanorama.org/Articles/introduction_to_orthodox_file_managers.shtml |
| `sources/networm-total-commander.html` | https://networm.me/2022/01/09/total-commander/ |
| `sources/vifm-docs.html` | https://vifm.info/docs/ |
| `sources/vifm-app.html` | https://vifm.info/docs/v0.14.4/vifm-app.txt |
| `sources/vifm-lua.html` | https://vifm.info/docs/v0.14.4/vifm-lua.txt |

## 本地代码证据

| 路径 | 重点 |
|---|---|
| `README` | ViFM 自我定位：Vim-like、modes/options/registers/commands、Unix philosophy |
| `HACKING.md` | 模块划分：engine、modes、int、io、lua、ui |
| `src/types.h` | 传统文件系统对象枚举 |
| `src/filetype.h` | 文件关联、viewer 类型、matcher -> program/viewer |
| `src/filelist.h` | custom view API |
| `src/running.h` | 运行外部命令、命令输出组成 custom view |
| `src/ops.h` | 文件操作枚举与冲突/错误策略 |
| `src/macros.h` | 输出重定向到菜单、预览、custom view、split 等宏 |
