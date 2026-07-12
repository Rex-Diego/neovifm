# NeoVifm M1 Findings

## 已知基础

- `view_t` 当前混合文件列表、selection、history、watcher、布局和 ncurses 状态。
- 经典绘制入口位于 `src/ui/fileview.c`，目录和条目状态来自 `view_t`。
- 第一阶段应创建独立数据快照，而不是直接拆分 `view_t`。
- macOS 可靠测试命令见根目录 `AGENTS.md`。

## 待确认

- snapshot 第一版字段：pane 目录、条目数、游标/顶部位置、窗口行列、过滤数、选择数；条目名称、来源、size/time、平台 stat、nlinks、FileType、selected/dir_link、树层级与 search match。
- `name` 和 `origin` 必须独立复制；普通 view 的 `origin` 指向 `view_t::curr_dir`，不能借用。
- 排除 `id/link/tag/hi_num/name_dec_num/was_selected/marked/temporary/slow_target/owns_origin` 等缓存、操作和所有权字段。
- renderer 当前直接读 `view_t` 并可回写布局缓存，因此 M1 先建立独立模块和测试，再通过实验分派逐步替换。
- 测试目录和 fixture 的最合适落点。

## 代码证据

- `dir_entry_t` 定义：`src/ui/ui.h:217`。
- `view_t` 列表/游标/窗口字段：`src/ui/ui.h:372`。
- 经典绘制循环直接读取 `view->dir_entry`：`src/ui/fileview.c:310`。
- 现有深拷贝只复制 `name/origin`，但会保留其他内部字段：`src/filelist.c:2697`，不适合作为新公开契约。
- 测试框架会自动收集 suite 目录中的 `.c` 文件，新增 suite 只需加入 `tests/Makefile` 的 `suites` 列表。
