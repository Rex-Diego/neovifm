import { afterEach, expect, test } from "bun:test"
import { mkdtemp, rm, writeFile } from "node:fs/promises"
import { tmpdir } from "node:os"
import { resolve } from "node:path"

import { startCoreSession } from "../src/core-client.js"
import { initialProbeState, reduceProbeState, type ProbeState } from "../src/probe-state.js"

let left: string | undefined
let right: string | undefined

afterEach(async () => {
  if (left !== undefined) await rm(left, { recursive: true })
  if (right !== undefined) await rm(right, { recursive: true })
  left = undefined
  right = undefined
})

async function waitFor(predicate: () => boolean, timeoutMs = 5_000): Promise<void> {
  const deadline = Date.now() + timeoutMs
  while (!predicate()) {
    if (Date.now() >= deadline) throw new Error("timed out waiting for core session")
    await Bun.sleep(10)
  }
}

test("real v3 session publishes cancellable preview lifecycle beside core-owned panes", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-right-"))
  await writeFile(resolve(left, "left-a"), "a")
  await writeFile(resolve(left, "left-b"), "b")
  await writeFile(resolve(right, "right-a"), "a")

  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected ready session")
    expect(state.workspace.active_pane).toBe("left")
    expect(state.workspace.right.entries.some((entry) => entry.name_display === "right-a")).toBe(true)
		await waitFor(() => state.phase === "ready" && "session" in state && state.preview?.content === "a")
		if (state.phase !== "ready" || !("session" in state)) throw new Error("expected preview session")
		expect(state.version).toBe(3)
		expect(state.tasks?.some((task) => task.state === "done" && task.kind === "text")).toBe(true)

    expect(await session.send({ action: "focus", pane: "right" })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.workspace.active_pane === "right")
    const beforeMove = state
    expect(await session.send({ action: "move", delta: 1 })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === 2)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected updated session")
    expect(state.workspace.left).toEqual(beforeMove.workspace.left)

    await writeFile(resolve(left, "left-refreshed"), "refresh")
    await waitFor(() => state.phase === "ready" && "session" in state && state.workspace.left.entries.some((entry) => entry.name_display === "left-refreshed"))
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected watcher refresh")
    expect(state.commandSequence).toBe(2)
    expect(state.workspace.right).toEqual(beforeMove.workspace.right)

    expect(await session.send({ action: "focus", pane: "left" })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.workspace.active_pane === "left")
    expect(await session.send({ action: "refresh" })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.workspace.left.entries.some((entry) => entry.name_display === "left-refreshed"))
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
})
