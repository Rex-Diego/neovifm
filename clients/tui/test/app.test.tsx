import { afterEach, expect, test } from "bun:test"
import { createSignal } from "solid-js"
import { testRender } from "@opentui/solid"

import { App, type AppProps } from "../src/app.js"
import type { SnapshotPayload, WorkspaceSnapshotPayload } from "../src/protocol.js"

const snapshot: SnapshotPayload = {
  cwd_display: "/tmp",
  cwd_bytes_hex: "2f746d70",
  generated_at_unix_ms: "0",
  snapshot_revision: "1",
  cwd_device: "10",
  cwd_inode: "20",
  cwd_ctime_unix_ns: "30",
  cursor: 0,
  entry_count: 1,
  selection_count: 0,
  filtered_count: 0,
  sort_key: "name",
  sort_descending: false,
  filter_active: false,
  entries: [
    {
      name_display: "file.txt",
      name_bytes_hex: "66696c652e747874",
      path_display: "/tmp/file.txt",
      path_bytes_hex: "2f746d702f66696c652e747874",
      kind: "file",
      size_bytes: "12",
      mtime_unix_ms: "0",
      device: "11",
      inode: "21",
      ctime_unix_ns: "31",
      mode_octal: "100644",
      selected: false,
      hidden: false,
    },
  ],
}

const capabilities = ["workspace-sort-v1", "file-actions-v1"] as const

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
    width: 180,
    height: 20,
  })

  await setup.renderOnce()
  const wideFrame = setup.captureCharFrame()
  expect(wideFrame).toContain("file.txt")
  expect(wideFrame).toContain("right.txt")
  expect(wideFrame).toContain("LEFT ACTIVE")
  expect(wideFrame).toContain("RIGHT • /var")
  expect(wideFrame).toContain("180x20")
  expect(wideFrame).toContain("-rw-r--r--")
  expect(wideFrame).toContain("1970-01-01")
  expect(wideFrame).toContain("Name ▲")
  expect(wideFrame).toContain("Permissions")
  expect(wideFrame).toContain("Size")
  expect(wideFrame).toContain("Created")
  expect(wideFrame).toContain("Modified")
  expect(wideFrame).toContain(" file.txt")
  expect(wideFrame).toContain("NORMAL")
  expect(wideFrame).toContain("")
  expect(wideFrame).toContain("")
  expect(wideFrame).toContain("")
  expect(wideFrame).toContain("F3  View")
  expect(wideFrame).toContain("F4  Edit")
  expect(wideFrame).toContain("F5  Copy")
  expect(wideFrame).toContain("F10  Quit")
  expect(wideFrame).not.toContain("READ ONLY")
  expect(wideFrame).not.toContain("| NORMAL |")

  setup.resize(60, 20)
  await setup.renderOnce()
  const compactFrame = setup.captureCharFrame()
  expect(compactFrame).toContain("60x20")
  expect(compactFrame).toContain("LEFT ACTIVE")
  expect(compactFrame).not.toContain("right.txt")
  expect(compactFrame).toContain("F10")

  let sent: unknown
  setup?.renderer.destroy()
  setup = await testRender(() => <App workspace={workspace} onCommand={(command) => { sent = command }} />, { width: 60, height: 20 })
  await setup.renderOnce()
  setup.mockInput.pressTab()
  expect(sent).toEqual({ action: "focus-next" })
})

test("column headers and the function bar are clickable mouse targets", async () => {
  const sent: unknown[] = []
  let cancelled = false
  setup = await testRender(() => <App
    workspace={workspace}
    capabilities={capabilities}
    onCommand={(command) => { sent.push(command) }}
    onCancel={() => { cancelled = true }}
  />, { width: 180, height: 20 })
  await setup.renderOnce()

  const sizeHeader = setup.renderer.root.findDescendantById("sort-left-size")
  expect(sizeHeader).toBeDefined()
  await setup.mockMouse.click(sizeHeader!.x, sizeHeader!.y)
  expect(sent.at(-1)).toEqual({ action: "sort-by", pane: "left", key: "size" })

  const createdHeader = setup.renderer.root.findDescendantById("sort-left-ctime")
  expect(createdHeader).toBeDefined()
  await setup.mockMouse.click(createdHeader!.x, createdHeader!.y)
  expect(sent.at(-1)).toEqual({ action: "sort-by", pane: "left", key: "ctime" })

  const copyButton = setup.renderer.root.findDescendantById("function-copy")
  expect(copyButton).toBeDefined()
  await setup.mockMouse.click(copyButton!.x, copyButton!.y)
  expect(sent.at(-1)).toEqual({
    action: "copy",
    pane: "left",
    cwd_bytes_hex: snapshot.cwd_bytes_hex,
    snapshot_revision: snapshot.snapshot_revision,
    cwd_device: snapshot.cwd_device,
    cwd_inode: snapshot.cwd_inode,
    cwd_ctime_unix_ns: snapshot.cwd_ctime_unix_ns,
    destination_cwd_bytes_hex: workspace.right.cwd_bytes_hex,
    destination_snapshot_revision: workspace.right.snapshot_revision,
    destination_cwd_device: workspace.right.cwd_device,
    destination_cwd_inode: workspace.right.cwd_inode,
    destination_cwd_ctime_unix_ns: workspace.right.cwd_ctime_unix_ns,
    targets: [{
      path_bytes_hex: snapshot.entries[0]!.path_bytes_hex,
      device: snapshot.entries[0]!.device,
      inode: snapshot.entries[0]!.inode,
      ctime_unix_ns: snapshot.entries[0]!.ctime_unix_ns,
      kind: snapshot.entries[0]!.kind,
    }],
  })

  const moveButton = setup.renderer.root.findDescendantById("function-move")
  expect(moveButton).toBeDefined()
  await setup.mockMouse.click(moveButton!.x, moveButton!.y)
  expect(sent.at(-1)).toEqual({
    action: "move-files",
    pane: "left",
    cwd_bytes_hex: snapshot.cwd_bytes_hex,
    snapshot_revision: snapshot.snapshot_revision,
    cwd_device: snapshot.cwd_device,
    cwd_inode: snapshot.cwd_inode,
    cwd_ctime_unix_ns: snapshot.cwd_ctime_unix_ns,
    destination_cwd_bytes_hex: workspace.right.cwd_bytes_hex,
    destination_snapshot_revision: workspace.right.snapshot_revision,
    destination_cwd_device: workspace.right.cwd_device,
    destination_cwd_inode: workspace.right.cwd_inode,
    destination_cwd_ctime_unix_ns: workspace.right.cwd_ctime_unix_ns,
    targets: [{
      path_bytes_hex: snapshot.entries[0]!.path_bytes_hex,
      device: snapshot.entries[0]!.device,
      inode: snapshot.entries[0]!.inode,
      ctime_unix_ns: snapshot.entries[0]!.ctime_unix_ns,
      kind: snapshot.entries[0]!.kind,
    }],
  })

  const mkdirButton = setup.renderer.root.findDescendantById("function-mkdir")
  expect(mkdirButton).toBeDefined()
  await setup.mockMouse.click(mkdirButton!.x, mkdirButton!.y)
  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("F7 MKDIR")
  const modalQuitButton = setup.renderer.root.findDescendantById("function-quit")
  expect(modalQuitButton).toBeDefined()
  await setup.mockMouse.click(modalQuitButton!.x, modalQuitButton!.y)
  expect(cancelled).toBe(false)
  await setup.mockInput.typeText("notes")
  setup.mockInput.pressEnter()
  await setup.renderOnce()
  expect(sent.at(-1)).toEqual({
    action: "mkdir",
    pane: "left",
    cwd_bytes_hex: snapshot.cwd_bytes_hex,
    snapshot_revision: snapshot.snapshot_revision,
    cwd_device: snapshot.cwd_device,
    cwd_inode: snapshot.cwd_inode,
    cwd_ctime_unix_ns: snapshot.cwd_ctime_unix_ns,
    name: "notes",
  })

  const deleteButton = setup.renderer.root.findDescendantById("function-delete")
  expect(deleteButton).toBeDefined()
  await setup.mockMouse.click(deleteButton!.x, deleteButton!.y)
  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("F8 DELETE")
  setup.mockInput.pressKey("y")
  await setup.renderOnce()
  expect(sent.at(-1)).toEqual({
    action: "delete",
    pane: "left",
    cwd_bytes_hex: snapshot.cwd_bytes_hex,
    snapshot_revision: snapshot.snapshot_revision,
    cwd_device: snapshot.cwd_device,
    cwd_inode: snapshot.cwd_inode,
    cwd_ctime_unix_ns: snapshot.cwd_ctime_unix_ns,
    targets: [{
      path_bytes_hex: snapshot.entries[0]!.path_bytes_hex,
      device: snapshot.entries[0]!.device,
      inode: snapshot.entries[0]!.inode,
      ctime_unix_ns: snapshot.entries[0]!.ctime_unix_ns,
      kind: snapshot.entries[0]!.kind,
    }],
  })

  const quitButton = setup.renderer.root.findDescendantById("function-quit")
  expect(quitButton).toBeDefined()
  await setup.mockMouse.click(quitButton!.x, quitButton!.y)
  expect(cancelled).toBe(true)
})

test("never allocates a third pane for preview or tasks", async () => {
  setup = await testRender(() => <App workspace={workspace} preview={{
    task_id: "1", generation: "2", pane: "left", kind: "text", state: "done",
    cwd_bytes_hex: snapshot.cwd_bytes_hex, path_bytes_hex: snapshot.entries[0]!.path_bytes_hex,
    content: "preview text", truncated: false,
  }} tasks={[{
    task_id: "1", generation: "2", pane: "left", kind: "text", state: "done",
    cwd_bytes_hex: snapshot.cwd_bytes_hex, path_bytes_hex: snapshot.entries[0]!.path_bytes_hex,
  }]} />, { width: 120, height: 20 })

  await setup.renderOnce()
  expect(setup.captureCharFrame()).not.toContain("preview text")
  expect(setup.captureCharFrame()).not.toContain("done:text")
  expect(setup.captureCharFrame()).toContain("file.txt")
  expect(setup.captureCharFrame()).toContain("right.txt")

  setup.resize(60, 20)
  await setup.renderOnce()
  expect(setup.captureCharFrame()).not.toContain("preview text")
})

test("opens F3 preview as a full workspace viewer instead of a third pane", async () => {
  setup = await testRender(() => <App workspace={workspace} preview={{
    task_id: "1", generation: "2", pane: "left", kind: "text", state: "done",
    cwd_bytes_hex: snapshot.cwd_bytes_hex, path_bytes_hex: snapshot.entries[0]!.path_bytes_hex,
    content: "preview body", truncated: false,
  }} />, { width: 100, height: 20 })

  await setup.renderOnce()
  expect(setup.captureCharFrame()).not.toContain("preview body")
  setup.mockInput.pressKey("F3")
  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("F3 VIEW")
  expect(setup.captureCharFrame()).toContain("preview body")
  setup.mockInput.pressKey("F3")
  await setup.renderOnce()
  expect(setup.captureCharFrame()).not.toContain("preview body")
})

test("opens a file with l through the same preview path as F3", async () => {
  setup = await testRender(() => <App workspace={workspace} preview={{
    task_id: "1", generation: "2", pane: "left", kind: "text", state: "done",
    cwd_bytes_hex: snapshot.cwd_bytes_hex, path_bytes_hex: snapshot.entries[0]!.path_bytes_hex,
    content: "opened with l", truncated: false,
  }} />, { width: 100, height: 20 })

  await setup.renderOnce()
  setup.mockInput.pressKey("l")
  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("F3 VIEW")
  expect(setup.captureCharFrame()).toContain("opened with l")
})

test("opens a loading viewer instead of rendering a stale preview from another pane or cursor identity", async () => {
  const staleWorkspace: WorkspaceSnapshotPayload = {
    ...workspace,
    active_pane: "right",
    right: { ...workspace.right, entries: [{ ...workspace.right.entries[0]!, path_bytes_hex: "2f7661722f6f74686572" }] },
  }
  setup = await testRender(() => <App workspace={staleWorkspace} preview={{
    task_id: "1", generation: "2", pane: "left", kind: "text", state: "done",
    cwd_bytes_hex: snapshot.cwd_bytes_hex, path_bytes_hex: snapshot.entries[0]!.path_bytes_hex,
    content: "stale preview", truncated: false,
  }} />, { width: 100, height: 20 })
  await setup.renderOnce()
  setup.mockInput.pressKey("F3")
  await setup.renderOnce()
  expect(setup.captureCharFrame()).not.toContain("stale preview")
  expect(setup.captureCharFrame()).toContain("F3 VIEW")
  expect(setup.captureCharFrame()).toContain("Loading preview")
})

test("keeps the C-owned cursor visible after it moves below the first viewport", async () => {
  const entries = Array.from({ length: 40 }, (_, index) => ({
    ...snapshot.entries[0]!,
    name_display: `entry-${String(index).padStart(2, "0")}`,
    path_bytes_hex: `2f746d702f${index.toString(16).padStart(2, "0")}`,
  }))
  const deepWorkspace: WorkspaceSnapshotPayload = {
    ...workspace,
    left: { ...workspace.left, cursor: 35, entry_count: entries.length, entries },
  }
  setup = await testRender(() => <App workspace={deepWorkspace} />, { width: 100, height: 12 })
  await setup.renderOnce()
  await Bun.sleep(5)
  await setup.renderOnce()
  const list = setup.renderer.root.findDescendantById("entries-left") as { scrollTop?: number } | undefined
  expect(list).toBeDefined()
  expect(list?.scrollTop ?? 0).toBeGreaterThan(0)
  expect(setup.captureCharFrame()).toContain("entry-35")
})

test("mouse clicks provide F3 and F4 fallbacks when the host captures function keys", async () => {
  let editedPath: string | undefined
  setup = await testRender(() => <App
    workspace={workspace}
    preview={{
      task_id: "1", generation: "2", pane: "left", kind: "text", state: "done",
      cwd_bytes_hex: snapshot.cwd_bytes_hex, path_bytes_hex: snapshot.entries[0]!.path_bytes_hex,
      content: "mouse preview", truncated: false,
    }}
    onEdit={(path) => { editedPath = path }}
  />, { width: 100, height: 20 })
  await setup.renderOnce()

  const viewButton = setup.renderer.root.findDescendantById("function-view")
  expect(viewButton).toBeDefined()
  await setup.mockMouse.click(viewButton!.x, viewButton!.y)
  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("mouse preview")
  await setup.mockMouse.click(viewButton!.x, viewButton!.y)
  await setup.renderOnce()

  const editButton = setup.renderer.root.findDescendantById("function-edit")
  expect(editButton).toBeDefined()
  await setup.mockMouse.click(editButton!.x, editButton!.y)
  await setup.renderOnce()
  expect(editedPath).toBe("/tmp/file.txt")
})

test("routes F4 edit through the shared action service", async () => {
  let editedPath: string | undefined
  const displayOnlyWorkspace: WorkspaceSnapshotPayload = {
    ...workspace,
    left: {
      ...workspace.left,
      entries: [{ ...workspace.left.entries[0]!, path_display: "/display-only" }],
    },
  }
  setup = await testRender(() => <App workspace={displayOnlyWorkspace} onEdit={(path) => { editedPath = path }} />, { width: 100, height: 20 })
  await setup.renderOnce()
  setup.mockInput.pressKey("F4")
  await setup.renderOnce()
  expect(editedPath).toBe("/tmp/file.txt")
})

test("refuses to pass a lossy non-UTF-8 display path to an editor", async () => {
  let edited = false
  const invalidIdentityWorkspace: WorkspaceSnapshotPayload = {
    ...workspace,
    left: {
      ...workspace.left,
      entries: [{ ...workspace.left.entries[0]!, path_bytes_hex: "ff" }],
    },
  }
  setup = await testRender(() => <App workspace={invalidIdentityWorkspace} onEdit={() => { edited = true }} />, { width: 100, height: 20 })
  await setup.renderOnce()
  setup.mockInput.pressKey("F4")
  await setup.renderOnce()
  expect(edited).toBe(false)
  expect(setup.captureCharFrame()).toContain("non-UTF-8 path")
})

test("reports a closed core channel instead of pretending a click succeeded", async () => {
  setup = await testRender(() => <App workspace={workspace} capabilities={capabilities} onCommand={() => false} />, { width: 100, height: 20 })
  await setup.renderOnce()
  const copyButton = setup.renderer.root.findDescendantById("function-copy")
  expect(copyButton).toBeDefined()
  await setup.mockMouse.click(copyButton!.x, copyButton!.y)
  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("Core command channel is unavailable")
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

test("cycles the compact metadata column between size, time, and permissions", async () => {
  const [props, setProps] = createSignal<AppProps>({ workspace })
  setup = await testRender(() => <App {...props()} />, { width: 100, height: 20 })

  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("Size")
  setProps({ workspace: { ...workspace, left: { ...workspace.left, sort_key: "mtime" } } })
  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("Modified ▲")
  expect(setup.captureCharFrame()).toContain("1970-01-01")
  setProps({ workspace: { ...workspace, left: { ...workspace.left, sort_key: "mode" } } })
  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("Permissions ▲")
  expect(setup.captureCharFrame()).toContain("-rw-r--r--")
})

test("keeps the workspace operable without Nerd Font or powerline glyphs", async () => {
  setup = await testRender(() => <App workspace={workspace} iconMode="ascii" />, { width: 100, height: 20 })
  await setup.renderOnce()
  const frame = setup.captureCharFrame()
  expect(frame).toContain("- file.txt")
  expect(frame).toContain("NORMAL")
  expect(frame).not.toContain("")
  expect(frame).not.toContain("")
})

test("delete confirmation describes the whole selection instead of only the cursor", async () => {
  const selectedWorkspace: WorkspaceSnapshotPayload = {
    ...workspace,
    left: {
      ...workspace.left,
      selection_count: 1,
      entries: [{ ...workspace.left.entries[0]!, selected: true }],
    },
  }
  setup = await testRender(() => <App workspace={selectedWorkspace} capabilities={capabilities} onCommand={() => true} />, { width: 100, height: 20 })
  await setup.renderOnce()
  setup.mockInput.pressKey("F8")
  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("Delete 1 selected items?")
})
