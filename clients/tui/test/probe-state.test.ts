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
