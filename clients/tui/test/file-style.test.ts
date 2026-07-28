import { expect, test } from "bun:test"

import { formatFileSize, formatMode, formatMtime, iconForEntry, mtimeAge, permissionTokens } from "../src/file-style.js"
import type { SnapshotEntry } from "../src/protocol.js"

const entry = (name: string, kind: SnapshotEntry["kind"] = "file"): SnapshotEntry => ({
  name_display: name,
  name_bytes_hex: "00",
  path_display: `/tmp/${name}`,
  path_bytes_hex: "00",
  kind,
  size_bytes: "0",
  mtime_unix_ms: "0",
  selected: false,
  hidden: false,
})

test("formats snapshot metadata in stable lsd-style columns", () => {
  expect(formatMode("40755", "directory")).toBe("drwxr-xr-x")
  expect(formatMode("100644", "file")).toBe("-rw-r--r--")
  expect(formatMode(undefined, "symlink")).toBe("l?????????")
  expect(formatFileSize("12")).toBe("    12 B")
  expect(formatFileSize("1536")).toBe("  1.5 KB")
  const epoch = new Date(0)
  const pad = (value: number) => String(value).padStart(2, "0")
  expect(formatMtime("0")).toBe(`${epoch.getFullYear()}-${pad(epoch.getMonth() + 1)}-${pad(epoch.getDate())} ${pad(epoch.getHours())}:${pad(epoch.getMinutes())}`)
  expect(formatMode("104755", "file")).toBe("-rwsr-xr-x")
  expect(formatMode("101777", "file")).toBe("-rwxrwxrwt")
})

test("uses lsd-style Nerd Font icons by default with an explicit ASCII fallback", () => {
  expect(iconForEntry(entry("src", "directory"), "fancy")).toBe("")
  expect(iconForEntry(entry("app.tsx"), "fancy")).toBe("")
  expect(iconForEntry(entry("core-client.ts"), "fancy")).toBe("")
  expect(iconForEntry(entry("README.md"), "fancy")).toBe("󰂺")
  expect(iconForEntry(entry("src", "directory"), "ascii")).toBe("d")
  expect(iconForEntry(entry("run", "executable"), "ascii")).toBe("x")
})

test("exposes semantic permission tokens and recent-file age buckets", () => {
  expect(permissionTokens("100755", "file").map((token) => token.kind)).toEqual([
    "type", "read", "write", "execute", "read", "none", "execute", "read", "none", "execute",
  ])
  expect(permissionTokens("104755", "file")[3]?.kind).toBe("sticky")
  expect(mtimeAge("3600000", 3600000 + 30 * 60 * 1000)).toBe("hour")
  expect(mtimeAge("0", 2 * 24 * 60 * 60 * 1000)).toBe("older")
  expect(mtimeAge("not-a-time", 0)).toBe("unknown")
})
