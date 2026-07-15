import type { EntryKind, SnapshotEntry } from "./protocol.js"

const TYPE_CHARACTER: Readonly<Record<EntryKind, string>> = {
  directory: "d",
  file: "-",
  executable: "-",
  symlink: "l",
  fifo: "p",
  socket: "s",
  "char-device": "c",
  "block-device": "b",
  unknown: "?",
}

export function iconForEntry(kind: EntryKind): string {
  switch (kind) {
    case "directory": return "d"
    case "executable": return "x"
    case "symlink": return "l"
    case "fifo": return "p"
    case "socket": return "s"
    case "char-device": return "c"
    case "block-device": return "b"
    case "unknown": return "?"
    case "file": return "-"
  }
}

export function formatMode(modeOctal: string | undefined, kind: EntryKind): string {
  const type = TYPE_CHARACTER[kind]
  if (modeOctal === undefined || !/^[0-7]+$/.test(modeOctal)) return `${type}?????????`
  const mode = Number.parseInt(modeOctal, 8)
  const masks = [0o400, 0o200, 0o100, 0o040, 0o020, 0o010, 0o004, 0o002, 0o001]
  const characters = ["r", "w", "x", "r", "w", "x", "r", "w", "x"]
  return `${type}${masks.map((mask, index) => (mode & mask) === 0 ? "-" : characters[index]).join("")}`
}

export function formatFileSize(sizeBytes: string): string {
  let bytes: bigint
  try {
    bytes = BigInt(sizeBytes)
  } catch {
    return "       ?"
  }
  const units = ["B", "KB", "MB", "GB", "TB", "PB"] as const
  let value = Number(bytes)
  let unit = 0
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024
    unit += 1
  }
  const number = unit === 0 ? value.toFixed(0) : value < 10 ? value.toFixed(1) : value.toFixed(0)
  return `${number} ${units[unit]}`.padStart(8)
}

export function formatMtime(mtimeUnixMs: string): string {
  const value = Number(mtimeUnixMs)
  if (!Number.isFinite(value)) return "---- -- -- --:--"
  const date = new Date(value)
  if (Number.isNaN(date.getTime())) return "---- -- -- --:--"
  return date.toISOString().slice(0, 16).replace("T", " ")
}

export function extensionGroup(entry: SnapshotEntry): "archive" | "code" | "document" | "image" | "media" | "plain" {
  const extension = entry.name_display.toLowerCase().split(".").pop() ?? ""
  if (["zip", "tar", "gz", "bz2", "xz", "7z", "rar"].includes(extension)) return "archive"
  if (["c", "h", "cpp", "hpp", "go", "rs", "py", "js", "ts", "tsx", "jsx", "lua", "sh", "toml", "json", "yaml", "yml"].includes(extension)) return "code"
  if (["md", "txt", "pdf", "doc", "docx", "odt"].includes(extension)) return "document"
  if (["png", "jpg", "jpeg", "gif", "webp", "svg", "ico"].includes(extension)) return "image"
  if (["mp3", "flac", "wav", "mp4", "mkv", "mov", "avi"].includes(extension)) return "media"
  return "plain"
}
