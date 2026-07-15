import { expect, test } from "bun:test"

import { formatFileSize, formatMode, formatMtime, iconForEntry } from "../src/file-style.js"

test("formats snapshot metadata in stable lsd-style columns", () => {
  expect(formatMode("40755", "directory")).toBe("drwxr-xr-x")
  expect(formatMode("100644", "file")).toBe("-rw-r--r--")
  expect(formatMode(undefined, "symlink")).toBe("l?????????")
  expect(formatFileSize("12")).toBe("    12 B")
  expect(formatFileSize("1536")).toBe("  1.5 KB")
  expect(formatMtime("0")).toBe("1970-01-01 00:00")
})

test("uses single-cell ASCII type icons without requiring Nerd Font", () => {
  expect(iconForEntry("directory")).toBe("d")
  expect(iconForEntry("file")).toBe("-")
  expect(iconForEntry("executable")).toBe("x")
  expect(iconForEntry("symlink")).toBe("l")
})
