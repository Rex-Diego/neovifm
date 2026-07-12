# NeoVifm Core Protocol v0

本目录定义 C core 与客户端之间的实验性本地协议。

## Transport

- UTF-8 JSON Lines over stdout。
- 每行一个完整 envelope，以 `\n` 结束。
- stderr 只用于人类可读诊断。
- M0 core probe 不读取 stdin；目标目录通过命令行参数传入。
- 正常顺序：`hello`，随后 `snapshot` 或 `error`，然后进程退出。

## Compatibility

- `protocol` 固定为 `neovifm-core`。
- `version` 当前为 `0`，属于实验协议。
- 客户端必须拒绝不支持的 version。
- 同一 version 内，接收方必须忽略未知字段。
- 已存在字段不能改变类型或语义；只能增加可选字段。

## Numeric Values

可能超过 JavaScript 安全整数的值使用十进制字符串：

- `size_bytes`
- `inode`
- `mtime_unix_ms`
- `generated_at_unix_ms`

## Paths

- `*_display` 是供 UI 使用的 UTF-8 字符串。
- POSIX 上，`*_bytes_hex` 是原始路径字节的十六进制表示，是 M0 的无损标识。
- Windows 端口在定义原生路径表示前，`*_bytes_hex` 使用 core 内部 UTF-8 路径字节；不能宣称是 Win32 原始字节。
- 客户端不得用 display 字符串执行文件操作。

## Example

```json
{"protocol":"neovifm-core","version":0,"type":"hello","sequence":0,"payload":{"implementation":"neovifm-core-probe","capabilities":["snapshot-v0"]}}
{"protocol":"neovifm-core","version":0,"type":"snapshot","sequence":1,"payload":{"cwd_display":"/tmp","cwd_bytes_hex":"2f746d70","generated_at_unix_ms":"0","cursor":0,"entry_count":1,"entries":[{"name_display":"file.txt","name_bytes_hex":"66696c652e747874","path_display":"/tmp/file.txt","path_bytes_hex":"2f746d702f66696c652e747874","kind":"file","size_bytes":"12","mtime_unix_ms":"0","selected":false,"hidden":false}]}}
```

正式 schema 见 `neovifm-core-v0.schema.json`。
