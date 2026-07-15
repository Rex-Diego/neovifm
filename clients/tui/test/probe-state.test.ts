import { describe, expect, test } from "bun:test"

import { initialProbeState, reduceProbeState } from "../src/probe-state.js"
import { parseProtocolRecord } from "../src/protocol.js"

const hello = parseProtocolRecord({
  protocol: "neovifm-core",
  version: 0,
  type: "hello",
  sequence: 0,
  payload: { implementation: "probe", capabilities: ["snapshot-v0"] },
})

const snapshot = parseProtocolRecord({
  protocol: "neovifm-core",
  version: 0,
  type: "snapshot",
  sequence: 1,
  payload: {
    cwd_display: "/tmp",
    cwd_bytes_hex: "2f746d70",
    generated_at_unix_ms: "0",
    cursor: -1,
    entry_count: 0,
    entries: [],
  },
})

describe("probe state reducer", () => {
  test("keeps a v2 session ready across core-owned workspace updates and command errors", () => {
    const pane = { cwd_display: "/tmp", cwd_bytes_hex: "2f746d70", generated_at_unix_ms: "0", cursor: -1, entry_count: 0, entries: [] }
    const hello2 = parseProtocolRecord({ protocol: "neovifm-core", version: 2, type: "hello", sequence: 0, payload: { implementation: "session", capabilities: ["workspace-session-v2"] } })
    const initial = parseProtocolRecord({ protocol: "neovifm-core", version: 2, type: "workspace-snapshot", sequence: 1, payload: { command_sequence: 0, trigger: "initial", active_pane: "left", left: pane, right: { ...pane, cwd_display: "/var" } } })
    const focused = parseProtocolRecord({ protocol: "neovifm-core", version: 2, type: "workspace-snapshot", sequence: 2, payload: { command_sequence: 1, trigger: "command", active_pane: "right", left: pane, right: { ...pane, cwd_display: "/var" } } })
    const watched = parseProtocolRecord({ protocol: "neovifm-core", version: 2, type: "workspace-snapshot", sequence: 3, payload: { command_sequence: 1, trigger: "watch", active_pane: "right", left: pane, right: { ...pane, cwd_display: "/private/var" } } })
    const commandError = parseProtocolRecord({ protocol: "neovifm-core", version: 2, type: "command-error", sequence: 4, payload: { command_sequence: 2, code: "not-directory", message: "not directory", retryable: false } })
    const watchedAfterError = parseProtocolRecord({ protocol: "neovifm-core", version: 2, type: "workspace-snapshot", sequence: 5, payload: { command_sequence: 2, trigger: "watch", active_pane: "right", left: pane, right: { ...pane, cwd_display: "/private/var/db" } } })
    const ready = reduceProbeState(reduceProbeState(initialProbeState(), hello2), initial)
    const updated = reduceProbeState(ready, focused)
    const refreshed = reduceProbeState(updated, watched)
    const afterError = reduceProbeState(refreshed, commandError)
    const refreshedAfterError = reduceProbeState(afterError, watchedAfterError)

    expect(updated).toMatchObject({ phase: "ready", workspace: { active_pane: "right" } })
    expect(refreshed).toMatchObject({ phase: "ready", workspace: { right: { cwd_display: "/private/var" } }, commandSequence: 1 })
    expect(afterError).toMatchObject({ phase: "ready", workspace: { active_pane: "right" }, commandError: { code: "not-directory" } })
    expect(refreshedAfterError).toMatchObject({ phase: "ready", workspace: { right: { cwd_display: "/private/var/db" } }, commandSequence: 2 })
  })

  test("retains v3 task state while ignoring stale preview generations", () => {
    const pane = { cwd_display: "/tmp", cwd_bytes_hex: "2f746d70", generated_at_unix_ms: "0", cursor: -1, entry_count: 0, entries: [] }
    const hello3 = parseProtocolRecord({ protocol: "neovifm-core", version: 3, type: "hello", sequence: 0, payload: { implementation: "session", capabilities: ["preview-session-v3"] } })
    const initial = parseProtocolRecord({ protocol: "neovifm-core", version: 3, type: "workspace-snapshot", sequence: 1, payload: { command_sequence: 0, trigger: "initial", active_pane: "left", left: pane, right: pane } })
    const task = parseProtocolRecord({ protocol: "neovifm-core", version: 3, type: "task", sequence: 2, payload: { task_id: "1", generation: "2", pane: "left", kind: "text", state: "running", cwd_bytes_hex: "2f746d70", path_bytes_hex: "2f746d702f61" } })
    const newest = parseProtocolRecord({ protocol: "neovifm-core", version: 3, type: "preview", sequence: 3, payload: { task_id: "1", generation: "2", pane: "left", kind: "text", state: "done", cwd_bytes_hex: "2f746d70", path_bytes_hex: "2f746d702f61", content: "new", truncated: false } })
    const stale = parseProtocolRecord({ protocol: "neovifm-core", version: 3, type: "preview", sequence: 4, payload: { task_id: "0", generation: "1", pane: "left", kind: "text", state: "done", cwd_bytes_hex: "2f746d70", path_bytes_hex: "2f746d702f6f6c64", content: "old", truncated: false } })
    const ready = reduceProbeState(reduceProbeState(initialProbeState(), hello3), initial)
    const running = reduceProbeState(ready, task)
    const current = reduceProbeState(reduceProbeState(running, newest), stale)

    expect(running).toMatchObject({ version: 3, tasks: [{ task_id: "1", state: "running" }] })
    expect(current).toMatchObject({ preview: { generation: "2", content: "new" }, sequence: 4 })
  })

  test("accepts an action refresh before the matching structured action terminal", () => {
    const pane = { cwd_display: "/tmp", cwd_bytes_hex: "2f746d70", generated_at_unix_ms: "0", cursor: -1, entry_count: 0, entries: [] }
    const hello = parseProtocolRecord({ protocol: "neovifm-core", version: 3, type: "hello", sequence: 0, payload: { implementation: "session", capabilities: ["preview-session-v3", "file-actions-v1"] } })
    const initial = parseProtocolRecord({ protocol: "neovifm-core", version: 3, type: "workspace-snapshot", sequence: 1, payload: { command_sequence: 0, trigger: "initial", active_pane: "left", left: pane, right: pane } })
    const acknowledged = parseProtocolRecord({ protocol: "neovifm-core", version: 3, type: "workspace-snapshot", sequence: 2, payload: { command_sequence: 1, trigger: "command", active_pane: "left", left: pane, right: pane } })
    const refreshed = parseProtocolRecord({ protocol: "neovifm-core", version: 3, type: "workspace-snapshot", sequence: 3, payload: { command_sequence: 1, trigger: "action", active_pane: "left", left: pane, right: pane } })
    const action = parseProtocolRecord({ protocol: "neovifm-core", version: 3, type: "action-task", sequence: 4, payload: { task_id: "3", command_sequence: 1, pane: "left", action: "mkdir", state: "done", completed_count: 1, total_count: 1, partial: false } })
    const state = reduceProbeState(reduceProbeState(reduceProbeState(reduceProbeState(initialProbeState(), hello), initial), acknowledged), refreshed)
    const complete = reduceProbeState(state, action)
    expect(complete).toMatchObject({ commandSequence: 1, actionTasks: [{ task_id: "3", state: "done" }] })
  })

  test("requires a v1 workspace atomically after the workspace capability", () => {
    const workspaceHello = parseProtocolRecord({
      protocol: "neovifm-core",
      version: 1,
      type: "hello",
      sequence: 0,
      payload: { implementation: "workspace", capabilities: ["workspace-v1"] },
    })
    const pane = {
      cwd_display: "/tmp", cwd_bytes_hex: "2f746d70", generated_at_unix_ms: "0",
      cursor: -1, entry_count: 0, entries: [],
    }
    const workspace = parseProtocolRecord({
      protocol: "neovifm-core",
      version: 1,
      type: "workspace-snapshot",
      sequence: 1,
      payload: { active_pane: "left", left: pane, right: { ...pane, cwd_display: "/var" } },
    })
    const ready = reduceProbeState(reduceProbeState(initialProbeState(), workspaceHello), workspace)

    expect(ready).toMatchObject({ phase: "ready", workspace: workspace.payload })
  })

  test("moves from hello to a ready snapshot without mutating prior state", () => {
    const initial = initialProbeState()
    const waiting = reduceProbeState(initial, hello)
    const ready = reduceProbeState(waiting, snapshot)

    expect(initial).toEqual({ phase: "awaiting-hello" })
    expect(waiting).toMatchObject({ phase: "awaiting-terminal", hello: hello.payload })
    expect(ready).toMatchObject({ phase: "ready", snapshot: snapshot.payload })
    expect(Object.isFrozen(ready)).toBe(true)
  })

  test("rejects a terminal record before hello and a repeated terminal record", () => {
    expect(() => reduceProbeState(initialProbeState(), snapshot)).toThrow("hello")

    const ready = reduceProbeState(reduceProbeState(initialProbeState(), hello), snapshot)
    expect(() => reduceProbeState(ready, snapshot)).toThrow("terminal")
  })
})
