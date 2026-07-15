import { afterEach, expect, test } from "bun:test"
import { chmod, mkdtemp, mkdir, rm, writeFile } from "node:fs/promises"
import { tmpdir } from "node:os"
import { resolve } from "node:path"
import { createSignal } from "solid-js"
import { testRender } from "@opentui/solid"

import { App } from "../src/app.js"
import { startCoreSession } from "../src/core-client.js"
import { initialProbeState, reduceProbeState, type ProbeState } from "../src/probe-state.js"

let root: string | undefined

afterEach(async () => {
  if (root !== undefined) await rm(root, { recursive: true })
  root = undefined
})

async function waitFor(predicate: () => boolean, timeoutMs = 5_000): Promise<void> {
  const deadline = Date.now() + timeoutMs
  while (!predicate()) {
    if (Date.now() >= deadline) throw new Error("timed out waiting for keyboard session update")
    await Bun.sleep(10)
  }
}

test("real keyboard h/j/k/l and Tab commands update the C-owned workspace", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) {
    throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  }
  root = await mkdtemp(resolve(tmpdir(), "neovifm-keyboard-"))
  const left = resolve(root, "left")
  const right = resolve(root, "right")
  const trashHelper = resolve(root, "test-trash.sh")
  await mkdir(left)
  await mkdir(right)
  await writeFile(trashHelper, "#!/bin/sh\nexec /bin/rm -rf -- \"$1\"\n")
  await chmod(trashHelper, 0o700)
  await writeFile(resolve(left, "a-file"), "a")
  await writeFile(resolve(left, "b-file"), "larger")
  await mkdir(resolve(right, "a-dir"))

  const originalTrash = process.env.NEOVIFM_TRASH_EXECUTABLE
  process.env.NEOVIFM_TRASH_EXECUTABLE = trashHelper
  const [state, setState] = createSignal<ProbeState>(initialProbeState())
  const errors: Error[] = []
  const session = startCoreSession({
    executable,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => setState((previous) => reduceProbeState(previous, record)),
    onError: (error) => errors.push(error),
  })
  await waitFor(() => state().phase === "ready" && "session" in state())
  const appProps = () => {
    const current = state()
    return {
      workspace: current.phase === "ready" && "workspace" in current ? current.workspace : undefined,
      capabilities: current.phase === "ready" ? current.hello.capabilities : undefined,
      onCommand: (command: Parameters<typeof session.send>[0]) => {
        return session.send(command)
      },
    }
  }
  let setup = await testRender(() => <App {...appProps()} />, { width: 100, height: 20 })

  try {
    await setup.renderOnce()
    setup.mockInput.pressKey("j")
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current && current.workspace.left.cursor === 1
    })

    setup.mockInput.pressKey("k")
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current && current.workspace.left.cursor === 0
    })

    setup.mockInput.pressKey("G")
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current && current.workspace.left.cursor === 1
    })

    setup.mockInput.pressKey("g")
    setup.mockInput.pressKey("g")
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current && current.workspace.left.cursor === 0
    })

    setup.mockInput.pressKey(" ")
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current && current.workspace.active_pane === "right"
    })

    setup.mockInput.pressTab()
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current && current.workspace.active_pane === "left"
    })

    setup.mockInput.pressTab()
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current && current.workspace.active_pane === "right"
    })

    setup.mockInput.pressKey("l")
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current && current.workspace.right.cwd_display.endsWith("/a-dir")
    })

    setup.mockInput.pressKey("h")
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current && current.workspace.right.cwd_display === right
    })

    setup.mockInput.pressTab()
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current && current.workspace.active_pane === "left"
    })
    setup.mockInput.pressArrow("right")
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current && current.workspace.left.sort_key === "size"
    })
    await setup.renderOnce()
    const sizeHeader = setup.renderer.root.findDescendantById("sort-left-size")
    expect(sizeHeader).toBeDefined()
    await setup.mockMouse.click(sizeHeader!.x, sizeHeader!.y)
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current && current.workspace.left.sort_descending
    })

    if (process.platform !== "darwin") {
      expect(errors).toEqual([])
      return
    }

    await setup.renderOnce()
    const copyButton = setup.renderer.root.findDescendantById("function-copy")
    expect(copyButton).toBeDefined()
    await setup.mockMouse.click(copyButton!.x, copyButton!.y)
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current
        && current.workspace.right.entries.some((entry) => entry.name_display === "a-file")
    })

    await setup.renderOnce()
    const afterCopy = state()
    if (!(afterCopy.phase === "ready" && "workspace" in afterCopy
      && afterCopy.workspace.left.entries[afterCopy.workspace.left.cursor]?.name_display === "b-file")) {
      if (!(afterCopy.phase === "ready" && "workspace" in afterCopy)) {
        throw new Error("workspace disappeared after copy")
      }
      const targetIndex = afterCopy.workspace.left.entries.findIndex((entry) => entry.name_display === "b-file")
      if (targetIndex < 0) {
        throw new Error(`b-file disappeared after copy: ${afterCopy.workspace.left.entries.map((entry) => entry.name_display).join(",")}`)
      }
      const beforeSequence = "session" in afterCopy ? afterCopy.commandSequence : -1
      if (!await session.send({ action: "move", delta: targetIndex > afterCopy.workspace.left.cursor ? 1 : -1 })) {
        throw new Error("failed to position cursor for move test")
      }
      await waitFor(() => {
        const current = state()
        return current.phase === "ready" && "session" in current
          && current.commandSequence > beforeSequence
      })
      const positioned = state()
      if (!(positioned.phase === "ready" && "workspace" in positioned
        && positioned.workspace.left.entries[positioned.workspace.left.cursor]?.name_display === "b-file")) {
        throw new Error(`move command targeted ${positioned.phase === "ready" && "workspace" in positioned ? `${positioned.workspace.active_pane}:${positioned.workspace.left.cursor}:${positioned.workspace.left.entries.map((entry) => entry.name_display).join(",")}` : positioned.phase}`)
      }
    }
    const positioned = state()
    if (!(positioned.phase === "ready" && "workspace" in positioned)) {
      throw new Error("workspace disappeared before move click")
    }
    setup.renderer.destroy()
    setup = await testRender(() => <App
      workspace={positioned.workspace}
      capabilities={positioned.hello.capabilities}
      onCommand={(command) => session.send(command)}
    />, { width: 100, height: 20 })
    await setup.renderOnce()
    const moveButton = setup.renderer.root.findDescendantById("function-move")
    expect(moveButton).toBeDefined()
    await setup.mockMouse.click(moveButton!.x, moveButton!.y)
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current
        && !current.workspace.left.entries.some((entry) => entry.name_display === "b-file")
        && current.workspace.right.entries.some((entry) => entry.name_display === "b-file")
    })

    const afterMove = state()
    if (!(afterMove.phase === "ready" && "workspace" in afterMove)) {
      throw new Error("workspace disappeared after move")
    }
    setup.renderer.destroy()
    setup = await testRender(() => <App
      workspace={afterMove.workspace}
      capabilities={afterMove.hello.capabilities}
      onCommand={(command) => session.send(command)}
    />, { width: 100, height: 20 })

    await setup.renderOnce()
    const mkdirButton = setup.renderer.root.findDescendantById("function-mkdir")
    expect(mkdirButton).toBeDefined()
    await setup.mockMouse.click(mkdirButton!.x, mkdirButton!.y)
    await setup.renderOnce()
    await setup.mockInput.typeText("clicked-dir")
    setup.mockInput.pressEnter()
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current
        && current.workspace.left.entries.some((entry) => entry.name_display === "clicked-dir")
    })

    const afterMkdir = state()
    if (!(afterMkdir.phase === "ready" && "workspace" in afterMkdir)) {
      throw new Error("workspace disappeared after mkdir")
    }
    setup.renderer.destroy()
    setup = await testRender(() => <App
      workspace={afterMkdir.workspace}
      capabilities={afterMkdir.hello.capabilities}
      onCommand={(command) => session.send(command)}
    />, { width: 100, height: 20 })
    await setup.renderOnce()
    const deleteButton = setup.renderer.root.findDescendantById("function-delete")
    expect(deleteButton).toBeDefined()
    await setup.mockMouse.click(deleteButton!.x, deleteButton!.y)
    await setup.renderOnce()
    setup.mockInput.pressKey("y")
    await waitFor(() => {
      const current = state()
      return current.phase === "ready" && "workspace" in current
        && !current.workspace.left.entries.some((entry) => entry.name_display === "a-file")
    })
    expect(errors).toEqual([])
  } finally {
    setup.renderer.destroy()
    session.close()
    await session.completion
    if (originalTrash === undefined) delete process.env.NEOVIFM_TRASH_EXECUTABLE
    else process.env.NEOVIFM_TRASH_EXECUTABLE = originalTrash
  }
}, 20_000)
