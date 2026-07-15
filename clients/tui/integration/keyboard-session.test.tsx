import { afterEach, expect, test } from "bun:test"
import { mkdtemp, mkdir, rm, writeFile } from "node:fs/promises"
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
  await mkdir(left)
  await mkdir(right)
  await writeFile(resolve(left, "a-file"), "a")
  await writeFile(resolve(left, "b-file"), "b")
  await mkdir(resolve(right, "a-dir"))

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
  const setup = await testRender(() => {
    const current = state()
    return <App
      workspace={current.phase === "ready" && "workspace" in current ? current.workspace : undefined}
      onCommand={(command) => { session.send(command) }}
    />
  }, { width: 100, height: 20 })

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
    expect(errors).toEqual([])
  } finally {
    setup.renderer.destroy()
    session.close()
    await session.completion
  }
}, 20_000)
