import { expect, test } from "bun:test"

import { formatStatusPath } from "../src/status-path.js"

test("toggles only the home directory and its descendants to tilde form", () => {
  expect(formatStatusPath("/Users/rex", "/Users/rex", "home")).toBe("~")
  expect(formatStatusPath("/Users/rex/project", "/Users/rex", "home")).toBe("~/project")
  expect(formatStatusPath("/Users/rex-other/project", "/Users/rex", "home")).toBe("/Users/rex-other/project")
  expect(formatStatusPath("/tmp", "/Users/rex", "home")).toBe("/tmp")
  expect(formatStatusPath("/Users/rex/project", "/Users/rex", "absolute")).toBe("/Users/rex/project")
})

test("preserves Windows separators while shortening a home path", () => {
  expect(formatStatusPath("C:\\Users\\rex\\project", "C:\\Users\\rex", "home")).toBe("~\\project")
  expect(formatStatusPath("C:\\Users\\rex-other", "C:\\Users\\rex", "home")).toBe("C:\\Users\\rex-other")
})
