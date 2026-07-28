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

export type IconMode = "fancy" | "ascii"

const FANCY_EXTENSIONS: Readonly<Record<string, string>> = {
  c: "", h: "", cpp: "", hpp: "",
  go: "", rs: "", py: "", js: "", jsx: "", ts: "", tsx: "",
  lua: "", sh: "", zsh: "", fish: "",
  json: "", toml: "", yaml: "", yml: "",
  md: "", txt: "", pdf: "", doc: "󰈬", docx: "󰈬",
  png: "", jpg: "", jpeg: "", gif: "", webp: "", svg: "󰜡",
  zip: "", tar: "", gz: "", bz2: "", xz: "", "7z": "", rar: "",
  mp3: "", flac: "", wav: "", mp4: "", mkv: "", mov: "", avi: "",
} as const

const FANCY_FILENAMES: Readonly<Record<string, string>> = {
  "readme.md": "󰂺",
  "license": "",
  "license.md": "",
  "makefile": "",
  "dockerfile": "󰡨",
  ".gitignore": "",
} as const

function asciiIcon(kind: EntryKind): string {
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

export function iconForEntry(entry: SnapshotEntry, mode: IconMode = "fancy"): string {
  if (mode === "ascii") return asciiIcon(entry.kind)
  if (entry.kind === "directory") return ""
  if (entry.kind === "symlink") return ""
  if (entry.kind === "fifo") return "󰟥"
  if (entry.kind === "socket") return "󰆨"
  if (entry.kind === "char-device" || entry.kind === "block-device") return ""
  if (entry.kind === "unknown") return ""
  const filename = entry.name_display.toLowerCase()
  const named = FANCY_FILENAMES[filename]
  if (named !== undefined) return named
  const dot = filename.lastIndexOf(".")
  const extension = dot < 0 ? "" : filename.slice(dot + 1)
  const byExtension = FANCY_EXTENSIONS[extension]
  if (byExtension !== undefined) return byExtension
  return entry.kind === "executable" ? "" : ""
}

export function formatMode(modeOctal: string | undefined, kind: EntryKind): string {
  const type = TYPE_CHARACTER[kind]
  if (modeOctal === undefined || !/^[0-7]+$/.test(modeOctal)) return `${type}?????????`
  const mode = Number.parseInt(modeOctal, 8)
  const masks = [0o400, 0o200, 0o100, 0o040, 0o020, 0o010, 0o004, 0o002, 0o001]
  const characters = ["r", "w", "x", "r", "w", "x", "r", "w", "x"]
  const permissions = masks.map((mask, index) => (mode & mask) === 0 ? "-" : characters[index])
  if ((mode & 0o4000) !== 0) permissions[2] = (mode & 0o100) !== 0 ? "s" : "S"
  if ((mode & 0o2000) !== 0) permissions[5] = (mode & 0o010) !== 0 ? "s" : "S"
  if ((mode & 0o1000) !== 0) permissions[8] = (mode & 0o001) !== 0 ? "t" : "T"
  return `${type}${permissions.join("")}`
}

export type PermissionTokenKind = "type" | "read" | "write" | "execute" | "sticky" | "none" | "unknown"

export interface PermissionToken {
  readonly character: string
  readonly kind: PermissionTokenKind
}

/**
 * Keeps permission coloring semantic: the renderer can choose a palette without
 * having to parse the display string or duplicate setuid/sticky handling.
 */
export function permissionTokens(modeOctal: string | undefined, kind: EntryKind): readonly PermissionToken[] {
  const text = formatMode(modeOctal, kind)
  if (text.includes("?")) {
    return Array.from(text, (character, index) => ({
      character,
      kind: index === 0 ? "type" : "unknown",
    }))
  }
  return Array.from(text, (character, index) => {
    if (index === 0) return { character, kind: "type" as const }
    if (character === "-") return { character, kind: "none" as const }
    if (character === "r") return { character, kind: "read" as const }
    if (character === "w") return { character, kind: "write" as const }
    if (character === "x") return { character, kind: "execute" as const }
    return { character, kind: "sticky" as const }
  })
}

export type MtimeAge = "hour" | "day" | "older" | "unknown"

export function mtimeAge(mtimeUnixMs: string, nowUnixMs = Date.now()): MtimeAge {
  const value = Number(mtimeUnixMs)
  if (!Number.isFinite(value)) return "unknown"
  const age = nowUnixMs - value
  if (age < 0 || age <= 60 * 60 * 1000) return "hour"
  if (age <= 24 * 60 * 60 * 1000) return "day"
  return "older"
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
  const pad = (part: number) => String(part).padStart(2, "0")
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}`
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
