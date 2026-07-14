# NeoVifm Hybrid M0 Progress

## 2026-07-11

- 用户批准采用 C core + TypeScript/Bun/OpenTUI client 的混合架构。
- 暂停原单体 C renderer 计划并保留历史记录。
- 初始化 Hybrid M0 计划。
- 新增 ADR 0001，确认 C core + OpenTUI client。
- 定义 `PaneSnapshot v0` JSON Schema 与 JSONL framing。
- 写入 C snapshot/protocol 测试和 TypeScript JSONL/layout 测试。
- RED 已确认：C 缺少 `pane_snapshot.h`；TUI 缺少 `protocol.js` 与 `app.js`。

## 2026-07-14

- 通过 session catchup 恢复工作；未发现未同步上下文。
- 确认活动计划为 `neovifm-hybrid-m0`，Phase 2 仍标记为进行中。
- 发现 HEAD 已提交 C probe、C 测试、TUI 客户端和集成测试，但持久计划未记录实现后的验证结果。
- 已开始按 TDD 验证路径核对实际测试和实现；阶段状态暂不提前变更。
- 聚焦 C suite 通过：`env -u VIFM -u MYVIFMRC make -C tests neovifm_snapshot`，16 个测试、171 个断言全部通过。
- TUI 单元测试通过：`bun run test`，16 个测试全部通过。
- TUI 静态类型检查通过：`bun run typecheck`。
- 一次独立只读 code review 子代理因上游 `429 Too Many Requests` 未完成；后续由主流程直接复核，未重复同一请求。
- 真实 C probe 集成通过：`clients/tui: bun run test:integration`，实际启动 `../../src/neovifm-core-probe` 并验证宽/窄布局渲染。
- TUI 覆盖率通过：`bun run test:coverage`，总函数 97.46%、总行 97.53%，三个模块均高于 80% 目标。
- 依赖审计通过：`bun audit` 报告无已知漏洞。
- `git diff --check` 通过；当前仅有本会话更新的活动 planning 文件未提交。
- 串行完整 C 基线通过：`env -u VIFM -u MYVIFMRC make check`，Autotools summary 为 1 PASS、0 FAIL/ERROR。
- 确认当前默认 developer build 未开 sanitizer；已确定使用隔离的 `--with-sanitize=basic` 构建进行 C probe 额外验证，避免改动用户当前构建配置。
- Sanitizer 配置第一次尝试被 Autoconf 拒绝：当前 source directory 已配置，错误已记录；未执行会破坏现有构建的 `make distclean`，将改用 detached git worktree。
- 隔离的 `--with-sanitize=basic` build 已成功编译 C probe；首次运行因 macOS ASan 不支持 `detect_leaks=1` 而中止，已记录并改用不含 LeakSanitizer 的 ASan/UBSan 运行参数。
- 改用 `ASAN_OPTIONS=halt_on_error=1` 与 `UBSAN_OPTIONS=halt_on_error=1` 后，隔离 probe 在非空测试目录成功输出 `hello` 和 `snapshot`，未报告 ASan/UBSan 错误；本机无法提供 LeakSanitizer 覆盖。
- 阶段复核发现尚未满足的实现边界：C scanner 仍直接依赖 POSIX 目录 API、`Makefile.win` 未接入 probe；TUI 缺少计划要求的 reducer/state 测试。Phase 2/3 暂保持未完成，转入 TDD 缺口修复。
- C 跨平台构建 RED 已确认：`make -C src -f Makefile.win -n neovifm-core-probe.exe` 报 `No rule to make target`。
- TUI 强化测试 RED 已确认：新增严格空行 framing、字节/条目限额、运行时冻结、安全展示文本、state reducer、逐记录回调、调用方取消和 loading UI 测试后，聚焦 `bun test` 为 15 pass、11 fail、1 module-not-found；失败与预期缺失实现一一对应。
- TUI 强化实现 GREEN：重写 JSONL 为逐字节限额/严格 framing，限制记录、总流、条目和字段；冻结 DTO、净化展示文本；新增纯 reducer、逐记录回调、调用方取消、loading/结构化错误和退出码处理。聚焦测试现为 29 pass、0 fail、65 assertions。
- C 目录上限 RED 已确认：新增 `NV_PANE_SNAPSHOT_MAX_ENTRIES` 溢出测试后，`neovifm_snapshot` 因常量/实现尚未定义而按预期失败；下一步实现跨平台 adapter、mtime fallback 和结构化 `snapshot-too-large` 错误。
- C portability/limit GREEN（Unix）：新增窄 `compat/neovifm_fs` adapter，C scanner 改用其目录/metadata API，补 `st_mtime` fallback 与特殊文件类型 capability 降级；目录超过 4096 条时返回 `snapshot-too-large`。`neovifm_snapshot` 现为 17 tests、8371 checks 全部通过。
- Windows build target GREEN（静态）：`src/Makefile.win` 已加入最小 probe source closure、all/tests 依赖和目录规则；`make -C src -f Makefile.win -n neovifm-core-probe.exe` 现可生成完整编译/链接配方。本机无 MinGW 交叉编译器，实际 Windows 二进制编译待 CI/Windows 环境验证。
- 回归验证：C focused suite 仍为 17 tests、8371 checks 通过；完整 TUI unit suite 为 29 pass、0 fail；第一次 typecheck 暴露两个新 TypeScript narrowing/array 类型错误，已做最小修正后 `bun run typecheck` 通过。
- 真实 probe integration 通过 2 tests（成功 snapshot 与缺失目录 error）；schema JSON 可解析。TUI coverage 总函数 80.49%、总行 87.48%，但 `index.tsx` 仅 28.57% 函数覆盖，需补依赖注入式启动测试使新增模块本身达到 80%。
- `git diff --check` 当前唯一失败为自动生成 `src/Makefile.in` 的一处尾随空白，已记录待修复。
- 为确保新 `index.tsx` 模块本身达标，新增可注入 renderer/probe 的启动测试和纯 app-props 映射测试；一次 TSX 对象字面量语法错误已修正。最新 coverage：总函数 91.45%、总行 97.76%，`index.tsx` 函数 83.33%、行 95.65%，全部新增 TS 模块达到 80% 目标。
- 当前修改后的 C probe 已用独立 `clang -fsanitize=address,undefined` 直接编译；在非空目录运行并输出有效 `hello`/`snapshot`，未报告 ASan/UBSan 错误。编译仅出现既有 third-party Parson 的 `sprintf` deprecation warning；macOS 不支持 LeakSanitizer，未声称 leak coverage。
- 最终前聚焦验证通过：C suite 17 tests、8371 checks；TUI unit 34 pass、0 fail、76 assertions（含 schema 资源边界一致性）；typecheck 通过；Windows Makefile dry-run 可生成 probe 配方。串行 `env -u VIFM -u MYVIFMRC make check` 通过（1 PASS、0 FAIL/ERROR）。
- 后续独立审查发现两项 C 阻断：producer 可生成超过 client 4 MiB record 上限的 snapshot，以及 Windows `_wstat()` 会跟随链接而丢失 symlink 身份；另发现 renderer 初始化失败时未取消/等待 probe。根级 props 展开是否响应式经真实 OpenTUI 测试复核为正常，不作为缺陷处理。
- 针对新 C 限额/API 的 RED 已确认：`neovifm_snapshot` 因缺少 byte-limit constants 和有状态 serializer API 发生预期编译失败。针对 TUI 的 RED 已确认：第三条 JSONL record、schema cursor invariant、诊断截断标记和 renderer 失败清理测试在实现前失败。
- GREEN：C snapshot builder 现保守追踪最大 JSON byte budget，serializer 以实际 JSON 大小拒绝超过 4 MiB 的 record；字段过长、条目过多和累计字节过多均映射到结构化 `snapshot-too-large`。`core-probe` 真实集成以 3500 个长文件名验证：未触及 4096 条目上限时输出 `error`、`os_error=E2BIG`、`retryable=false`，client 不会先触发输入限额。
- GREEN：Windows adapter 以 `FindFirstFileW()` no-follow 目录项信息识别真正的 `IO_REPARSE_TAG_SYMLINK`，保留悬挂链接身份；本机无 MinGW，只完成 Makefile 目标静态验收。
- GREEN：JSONL decoder 在解码第三条记录前拒绝它；schema `$comment` 与 parser 测试共同锁定 cursor/entry_count 语义；TUI 对截断诊断只显示安全标记而不泄露 stderr。renderer 启动失败时 abort 并等待 probe 后重新抛出原错误。
- 最新聚焦验证：`env -u VIFM -u MYVIFMRC make -C tests neovifm_snapshot` 通过（19 tests、8385 checks）；TUI 相关单元测试 36 pass、79 assertions；真实 C probe integration 3 pass（含累计 byte-budget error）；`bun run typecheck` 通过；非 Apple `pane_snapshot.c` syntax-only 验证通过。最终完整回归与 post-fix 审查仍待完成。
- TUI post-fix 审查又发现两项问题：不安全 display 字符被替换成 U+FFFD 后可能突破 16 KiB，以及协议失败后若 probe 后代持有 stdout/stderr pipe，等待 drain 会把 protocol error 错映射为 timeout。两项均先补 RED 测试后修复：sanitize 后复查 UTF-8 byte budget；首个 protocol/reducer failure 取消两个 reader、杀死 probe 并优先返回 protocol error。背景子进程持 pipe 与 timeout 两个回归测试均通过。
- 完整基线已重新配置：`scripts/fix-timestamps` 与 `CFLAGS='-Wno-error=gnu-folding-constant' ./configure --enable-developer --without-glib` 成功；串行 `env -u VIFM -u MYVIFMRC make check` 成功。
- 最终 TUI 验证：`bun run test` 43 pass、89 assertions；并行运行 test 与 coverage 也通过，消除了短 timeout 的负载相关脆弱性。`bun run test:coverage` 总函数 88.78%、总行 98.12%，每个新增/改动 TypeScript 模块均至少 80%；`bun run typecheck`、`bun audit`（无漏洞）、真实 core integration 3 pass 和 schema parse 均通过。
- 最终 C/跨平台验证：focused suite 为 19 tests、8385 checks；`make -C src -f Makefile.win -B -n neovifm-core-probe.exe` 生成完整 source closure；非 Apple syntax-only 通过；当前源码的 AddressSanitizer/UBSan probe 在非空目录运行无报告（仅第三方 Parson 既有 `sprintf` deprecation warning，macOS 无 LeakSanitizer）。`git diff --check` 通过。
- 两轮 post-fix C/TypeScript 审查均无阻断或高风险问题。M0 决策完成：保持短命只读 probe；M1 才接入真实 `PaneModel` adapter、双 pane、watcher 与增量事件，常驻 service 仍受 ADR 0001 复审门槛约束。
- 额外覆盖了“外部取消与不完整 JSONL 同时发生”的优先级：外部取消/真正 deadline 在其先发生时保留 `cancelled`/`timeout`，已检测到的 protocol failure 则保持 `protocol`，避免内部 abort 与外部 abort 相互遮蔽。

## 测试结果

| 命令 | 结果 |
|---|---|
| `make -C tests neovifm_snapshot` | RED：预期的缺失实现编译失败 |
| `bun test` | RED：预期的缺失模块失败 |
| `env -u VIFM -u MYVIFMRC make -C tests neovifm_snapshot` | PASS：16 tests、171 checks |
| `clients/tui: bun run test` | PASS：16 tests |
| `clients/tui: bun run typecheck` | PASS |
| `clients/tui: bun run test:integration` | PASS：真实 C probe、1 test |
| `clients/tui: bun run test:coverage` | PASS：97.46% funcs、97.53% lines |
| `clients/tui: bun audit` | PASS：No vulnerabilities found |
| `git diff --check` | PASS |
| `env -u VIFM -u MYVIFMRC make check` | PASS：1 PASS、0 FAIL/ERROR |
