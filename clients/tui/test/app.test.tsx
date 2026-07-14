import { afterEach, expect, test } from "bun:test"
import { createSignal } from "solid-js"
import { testRender } from "@opentui/solid"

import { App, type AppProps } from "../src/app.js"
import type { SnapshotPayload, WorkspaceSnapshotPayload } from "../src/protocol.js"

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

const workspace: WorkspaceSnapshotPayload = {
  active_pane: "left",
  left: snapshot,
  right: {
    ...snapshot,
    cwd_display: "/var",
    cwd_bytes_hex: "2f766172",
    entries: [{ ...snapshot.entries[0]!, name_display: "right.txt" }],
  },
}

afterEach(() => {
  setup?.renderer.destroy()
  setup = undefined
})

test("renders two panes by default and degrades to the active pane when narrow", async () => {
  setup = await testRender(() => <App workspace={workspace} />, {
    width: 100,
    height: 20,
  })

  await setup.renderOnce()
  const wideFrame = setup.captureCharFrame()
  expect(wideFrame).toContain("file.txt")
  expect(wideFrame).toContain("right.txt")
  expect(wideFrame).toContain("LEFT ACTIVE")
  expect(wideFrame).toContain("RIGHT /var")
  expect(wideFrame).toContain("100x20")
  expect(wideFrame).toContain("[F]")

  setup.resize(60, 20)
  await setup.renderOnce()
  const compactFrame = setup.captureCharFrame()
  expect(compactFrame).toContain("60x20")
  expect(compactFrame).toContain("LEFT ACTIVE")
  expect(compactFrame).not.toContain("right.txt")

  let sent: unknown
  setup?.renderer.destroy()
  setup = await testRender(() => <App workspace={workspace} onCommand={(command) => { sent = command }} />, { width: 60, height: 20 })
  await setup.renderOnce()
  setup.mockInput.pressTab()
  expect(sent).toEqual({ action: "focus", pane: "right" })
})

test("renders a core error without a snapshot", async () => {
  setup = await testRender(() => <App error="cannot scan directory" />, {
    width: 60,
    height: 12,
  })

  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("cannot scan directory")
})

test("renders a cancellable loading state before a snapshot arrives", async () => {
  setup = await testRender(() => <App loading />, {
    width: 60,
    height: 12,
  })

  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("CONNECTING TO CORE")
})

test("reacts to the root app-props accessor used by the renderer", async () => {
  const [props, setProps] = createSignal<AppProps>({ loading: true })
  setup = await testRender(() => <App {...props()} />, {
    width: 100,
    height: 20,
  })

  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("CONNECTING TO CORE")

  setProps({ workspace })
  await setup.renderOnce()
  const frame = setup.captureCharFrame()
  expect(frame).toContain("file.txt")
  expect(frame).not.toContain("CONNECTING TO CORE")
})

test("uses a refreshed core workspace as the active-pane source of truth", async () => {
  const [props, setProps] = createSignal<AppProps>({ workspace })
  setup = await testRender(() => <App {...props()} />, { width: 60, height: 20 })

  await setup.renderOnce()
  setProps({ workspace: { ...workspace, active_pane: "right", left: { ...workspace.left }, right: { ...workspace.right } } })
  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("RIGHT ACTIVE")
})
