import { afterEach, expect, test } from "bun:test"
import { testRender } from "@opentui/solid"

import { App } from "../src/app.js"
import type { SnapshotPayload } from "../src/protocol.js"

const snapshot: SnapshotPayload = {
  cwd_display: "/tmp",
  cwd_bytes_hex: "2f746d70",
  generated_at_unix_ms: "0",
  cursor: 0,
  entry_count: 1,
  entries: [
    {
      name_display: "file.txt",
      name_bytes_hex: "66696c652e747874",
      path_display: "/tmp/file.txt",
      path_bytes_hex: "2f746d702f66696c652e747874",
      kind: "file",
      size_bytes: "12",
      mtime_unix_ms: "0",
      selected: false,
      hidden: false,
    },
  ],
}

let setup: Awaited<ReturnType<typeof testRender>> | undefined

afterEach(() => {
  setup?.renderer.destroy()
  setup = undefined
})

test("renders snapshot in wide and compact terminals", async () => {
  setup = await testRender(() => <App snapshot={snapshot} />, {
    width: 100,
    height: 20,
  })

  await setup.renderOnce()
  const wideFrame = setup.captureCharFrame()
  expect(wideFrame).toContain("file.txt")
  expect(wideFrame).toContain("100x20")
  expect(wideFrame).toContain("DETAILS")
  expect(wideFrame).toContain("[F]")

  setup.resize(60, 20)
  await setup.renderOnce()
  const compactFrame = setup.captureCharFrame()
  expect(compactFrame).toContain("60x20")
  expect(compactFrame).not.toContain("DETAILS")
})

test("renders a core error without a snapshot", async () => {
  setup = await testRender(() => <App error="cannot scan directory" />, {
    width: 60,
    height: 12,
  })

  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("cannot scan directory")
})
