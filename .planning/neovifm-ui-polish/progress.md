# NeoVifm TUI 可用性 Progress

## 2026-07-15

- 运行 `planning-with-files` session catchup，确认 M2 完成且工作树未提交。
- 阅读当前 M2 计划、架构基线与 `HACKING.md`；创建独立 `neovifm-ui-polish` 计划并切换 active plan。
- 下一步：检查 OpenTUI keyboard event、当前 app 渲染测试与真实文本帧，先写失败测试。
- 用户补充要求：快捷键必须与 Vifm 一致，当前 h/j/k/l 也不可用。
- 检查 OpenTUI KeyEvent：真实事件含 `name/sequence/raw/eventType/source`；当前实现只比较 `name` 且测试只覆盖模拟 Tab。下一步抽出可单测的 key normalization/command mapping，并从 renderer mock 输入验证 h/j/k/l/Tab。
- 进一步检查 OpenTUI 0.4.3 实现，字母与 Tab 的 `name` 解析正常，问题更可能在完整 session 链路或 UI 状态反馈。下一步新增真实 C session + test renderer 的键盘集成测试，不再只断言回调参数。
- 新增 `integration/keyboard-session.test.tsx`，真实验证 `j/k/Tab/l/h` 贯穿 renderer、Bun stdin、C v3 session 与 workspace snapshot；当前测试通过。
- 下一步仍按用户现场修复：抽出键位归一化/序列状态机，处理 focus/default propagation，并补 `gg/G`；随后重做布局与底栏。
- 对照 `src/modes/normal.c` 确认当前另一个明确 bug：Vifm 的 Space 切 pane，而 OpenTUI 错映射为 toggle selection。已把 `gg/G`、Ctrl-N/P、Ctrl-W w/p 与 Space/Tab 纳入修复契约。
- 已进入 TDD RED：新增 keymap 测试与 C first/last cursor 测试；分别因缺少 `src/keymap.js` 与 `NV_SESSION_MOVE_FIRST/LAST` 按预期失败。
- 实现 `VifmKeymap` 与 C `move-to first/last`，keymap 4 tests 和 focused C 35 tests 已通过；App 已删除常驻 preview/task pane。
- 旧 preview UI 测试因需求已被用户否定而失败，已改为断言无论宽窄布局都不分配第三 pane。
- 确认 OpenTUI text/span 支持独立 `fg/bg`；Starship 分段状态与功能键条可以在现有 runtime 内实现。下一步先写新的宽/窄文本帧断言，再重构 `app.tsx` 的文件行和底栏。
- 已加入 lsd metadata formatter 与 Starship/Total Commander 文本帧 RED 测试；当前因缺少 `file-style.js` 且仍渲染旧底栏而按预期失败。
- 实现 `file-style.ts`、lsd 风格 mode/icon/name/size/mtime 行、Starship 色块状态行和 Total Commander 功能键条。首次文本帧已符合视觉结构，仅测试的空格精确值需按稳定列宽修正。
- 已为 F3/F4/F10 写 RED 测试：要求 F3 打开覆盖工作区的 viewer（不恢复第三 pane），F4 只报告未接 action service，F10 退出；当前因未实现按预期失败。
- 实现 F3 全工作区 viewer 与 F3/Esc 关闭；F4/F5/F6/F7/F8 会在状态段明确显示 action service 未连接，F10/q 退出。相关 App/keymap/file-style 14 tests 与 typecheck 通过。
- Phase 1--4 实现完成，进入 Phase 5：扩展真实 C 键盘集成到 `gg/G/Space`，再跑 coverage、integration、C focused 与完整回归。
- 扩展 integration 后命中 Bun 默认 5s 总测试时限；已提高该真实进程多阶段测试的总预算，保留每个键位 5s 的单步诊断边界。
- 单步诊断定位 Tab 真 bug：UI callback 使用初始 active pane，导致 Space 切到 right 后 Tab 再次发送 `focus right`。开始将 pane toggle 下沉为 C `focus-next`。
- 完成 core-owned `focus-next`：Space、Tab、Ctrl-W w/p 均发送无目标 pane 的命令，C core 原子切换；真实 renderer + C session 集成已验证连续 Space/Tab 能 left -> right -> left。
- 清理 keymap 的 active pane 参数和新版底栏未使用 helper，并在 v3 协议文档记录 `focus-next`、`move-to first/last` 契约。
- TUI 全量 unit/文本帧测试通过：67 tests；覆盖率 85.65% functions / 94.50% lines；TypeScript typecheck 通过。
- TUI integration 全量通过：5 tests，其中真实键盘 session 覆盖 `j/k/G/gg/Space/Tab/l/h` 与连续双向切 pane。
- focused C `neovifm_snapshot` 通过：8625 checks / 36 tests。
- 串行 `env -u VIFM -u MYVIFMRC make check` 全部通过；`bun audit` 未发现漏洞；`git diff --check` 通过。
- 对照 Vifm 完整 `builtin_cmds[]` 新增一致性 RED，确认旧映射仍错误地把 `q`/Ctrl-C 当退出、`r` 当刷新且混淆大写 H/L；开始修正为 Ctrl-L、Home/End、ZZ/ZQ，并让不支持的大写语义保持未处理。
- 按用户要求补充 ViATc 参考源码：`linxinhong/ViATc` 已 shallow clone 到 `/Users/rex/soft/_refs/neovifm/viatc`，验证 origin/master/HEAD 正常，未污染主仓库。
- Vifm 一致性修正 GREEN：Home/End、Ctrl-L、ZZ/ZQ 通过；普通 q/r/Ctrl-C 和不支持的大写 H/L 不再误触发退出、刷新或目录导航。
- 最终 TUI 验收：68 unit/text-frame tests，85.65% functions / 94.50% lines，typecheck 通过；5 integration tests 通过。
- 最终仓库验收：focused C 8625 checks / 36 tests、串行 `make check`、`bun audit`、`git diff --check` 全部通过。Phase 5 完成。
