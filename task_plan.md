# Task Plan: Total Commander / ViFM Research

## Goal
形成一份带本地素材、来源清单和架构对齐建议的 Markdown 调研报告，用于判断 ViFM 如何向 Total Commander 的抽象哲学对齐并超越。

## Current Phase
Complete

## Phases

### Phase 1: 资料检索与来源锁定
- [ ] 搜索知乎/中文互联网中关于 Total Commander 的深度赏析，重点寻找 “two window god” 线索
- [ ] 收集官方文档、插件/文件系统抽象、查看器/压缩包等核心机制资料
- [ ] 保存关键发现到 findings.md
- **Status:** complete

### Phase 2: ViFM 现有设计梳理
- [ ] 阅读 README、帮助文档与关键源码入口
- [ ] 梳理 ViFM 的双栏、命令/模式、文件类型、查看器、外部集成与操作抽象
- [ ] 记录与 Total Commander 可比的设计点
- **Status:** complete

### Phase 3: 素材下载与归档
- [ ] 建立报告目录与 assets 目录
- [ ] 下载或生成可合法引用的截图/图片素材
- [ ] 保存来源索引与本地文件清单
- **Status:** complete

### Phase 4: 报告写作
- [ ] 写成图文并茂的 Markdown 报告
- [ ] 包含 Total Commander 哲学、ViFM 差距、颠覆式路线图、参考资料
- [ ] 控制引用为摘要/短引，避免大段搬运
- **Status:** complete

### Phase 5: 验证与交付
- [ ] 检查图片链接、Markdown 链接、文件存在性
- [ ] 补齐来源和素材说明
- [ ] 向用户说明报告位置和关键结论
- **Status:** complete

## Key Questions
1. “two window god” 是否能找到原始来源？如果找不到，最接近的中文深度赏析是什么？
2. Total Commander 的核心抽象到底是什么：双栏、压缩包即目录、查看、插件、选择集、队列、命令模型分别如何统一？
3. ViFM 的底层框架更像 Vim 化文件列表，还是已经有可扩展的资源/动作抽象？
4. 要让 ViFM 对齐/超越 Total Commander，最小而根本的架构变化是什么？

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| 以官方文档和可访问深度文章为主，知乎线索作为可验证则引用、不可验证则标明未找到原文 | 避免把记忆线索当成事实 |
| 报告和素材放在 `research/total-commander-vifm/` | 不污染源码目录，便于后续继续补充 |

## Errors Encountered
| Error | Attempt | Resolution |
|-------|---------|------------|
| `market-research` skill path missing | 1 | 记录为缺失，继续用浏览器检索和本地代码分析完成 |

## Notes
- 优先中文输出。
- 报告要有图、有本地素材、有来源。
