# Findings

## Windows 运行时

- `core_probe` 和 `core_session` 必须从 UTF-16 `wmain` 接收参数，再在边界转换为 UTF-8；窄字符 `main` 无法可靠接收中文目录。
- snapshot 的 owner/group 在 Windows 没有 POSIX 身份来源，空值不是内存分配失败。
- preview 文件读取需要 UTF-16 `_wopen` 和二进制模式；Unix `O_NONBLOCK` 不能直接带到 Windows。
- `GetStdHandle(STD_INPUT_HANDLE)` 配合 `PeekNamedPipe` 和短轮询可以避免空闲时阻塞在 `fgets()`，同时识别关闭的 pipe。

## Persistence

- JSON 文件读写改成 Unicode 路径还不够；恢复前的目录存在性检查也必须使用 `GetFileAttributesW`，否则中文目录会被误判为不存在并触发安全降级。
- Windows 状态路径按 `NEOVIFM_SESSION_STATE`、`LOCALAPPDATA`、`USERPROFILE` 排序。
- 同目录独占临时文件、`_commit()` 和 `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` 能保留旧状态并可靠替换。

## 测试

- Unicode 文件名的区域排序顺序不能写死；用 ASCII 前缀稳定排序，同时保留 Unicode 路径覆盖。
- Windows 原始字节夹具必须使用 `O_BINARY`，否则换行会被 CRT 转成 CRLF。
- 超大目录用例在 Windows 删除数千个文件较慢，cleanup hook 需要独立于测试主体的合理时限。
