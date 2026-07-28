import { afterEach, expect, test } from "bun:test"

import { CoreClientError, type CoreSession, type CoreSessionRequest } from "../src/core-client.js"
import {
  appPropsFor,
  defaultCoreProbePath,
  editorCommand,
  exitCodeFor,
  main,
  renderUntilDestroyed,
  type MainDependencies,
  toUiErrorMessage,
} from "../src/index.js"
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

const workspaceHello = parseProtocolRecord({
  protocol: "neovifm-core",
  version: 1,
  type: "hello",
  sequence: 0,
  payload: { implementation: "workspace", capabilities: ["workspace-v1"] },
})

const workspace = parseProtocolRecord({
  protocol: "neovifm-core",
  version: 1,
  type: "workspace-snapshot",
  sequence: 1,
  payload: {
    active_pane: "left",
    left: snapshot.type === "snapshot" ? snapshot.payload : {},
    right: snapshot.type === "snapshot" ? { ...snapshot.payload, cwd_display: "/var" } : {},
  },
})

afterEach(() => {
  process.exitCode = 0
})

function dependencies(start: (request: CoreSessionRequest) => CoreSession): MainDependencies {
  return {
    defaultCoreProbePath: () => "/mock/neovifm-core-probe",
    renderApp: async (props) => {
      props()
    },
    startCoreSession: start,
  }
}

test("derives loading, workspace-ready, and core-error app props from immutable state", () => {
  const waiting = reduceProbeState(initialProbeState(), workspaceHello)
  const ready = reduceProbeState(waiting, workspace)
  const failed = reduceProbeState(waiting, parseProtocolRecord({
    protocol: "neovifm-core",
    version: 1,
    type: "error",
    sequence: 1,
    payload: { code: "denied", message: "permission denied", retryable: false },
  }))

  expect(appPropsFor(waiting)).toMatchObject({ loading: true })
  expect(appPropsFor(ready)).toMatchObject({
    loading: false,
    workspace: workspace.type === "workspace-snapshot" ? workspace.payload : {},
    capabilities: ["workspace-v1"],
  })
  expect(appPropsFor(failed)).toMatchObject({ error: "permission denied" })
  expect(appPropsFor(ready, "client failed")).toMatchObject({ error: "client failed" })
})

test("uses a stable default location and sanitizes unknown errors", () => {
  expect(defaultCoreProbePath()).toContain("neovifm-core-session")
  expect(toUiErrorMessage(new Error("bad\u001bmessage"))).toBe("bad�message")
})

test("builds a direct editor argv without invoking a shell", () => {
  expect(editorCommand("/tmp/file name.ts", { VISUAL: "code --wait" })).toEqual([
    "code", "--wait", "--", "/tmp/file name.ts",
  ])
  expect(editorCommand("/tmp/file", { EDITOR: "'nvim' -f" })).toEqual([
    "nvim", "-f", "--", "/tmp/file",
  ])
})

test("preserves structured core error context without rendering stderr", () => {
  const error = new CoreClientError("permission denied", {
    kind: "core",
    coreCode: "open-directory",
    exitCode: 2,
    stderr: "diagnostic that must stay out of the UI",
    stderrTruncated: true,
  })

  expect(toUiErrorMessage(error)).toContain("core")
  expect(toUiErrorMessage(error)).toContain("open-directory")
  expect(toUiErrorMessage(error)).not.toContain("that must stay out of the UI")
  expect(toUiErrorMessage(error)).toContain("diagnostics truncated")
  expect(exitCodeFor(error)).toBe(2)
})

test("maps cancellation to a conventional non-zero CLI status", () => {
  const error = new CoreClientError("cancelled", { kind: "cancelled" })

  expect(exitCodeFor(error)).toBe(130)
})

test("starts the session before rendering and applies records through the reducer", async () => {
  const calls: string[] = []

  await main(["/tmp"], dependencies((request) => {
    request.onRecord(workspaceHello)
    request.onRecord(workspace)
    calls.push(request.executable, request.leftPath, request.rightPath)
    return { completion: Promise.resolve(), send: async () => true, close: () => undefined }
  }))

  expect(calls).toEqual(["/mock/neovifm-core-probe", "/tmp", "/tmp"])
})

test("injects the clipboard service into the rendered app", async () => {
  const copied: string[] = []
  const base = dependencies((request) => {
    request.onRecord(workspaceHello)
    request.onRecord(workspace)
    return { completion: Promise.resolve(), send: async () => true, close: () => undefined }
  })
  await main(["/tmp"], {
    ...base,
    copyText: async (text) => { copied.push(text) },
    renderApp: async (props) => {
      await props().onCopyText?.("~/project")
    },
  })

  expect(copied).toEqual(["~/project"])
})

test("keeps the production renderer lifecycle open until onDestroy", async () => {
  let destroy: (() => void) | undefined
  let finished = false
  const lifetime = renderUntilDestroyed(() => ({}), async (_node, config) => {
    destroy = config.onDestroy
  }).then(() => { finished = true })

  await Bun.sleep(0)
  expect(finished).toBe(false)
  expect(destroy).toBeDefined()
  destroy?.()
  await lifetime
  expect(finished).toBe(true)
})

test("keeps a non-zero exit status after displaying a structured core failure", async () => {
  await main([], dependencies((request) => {
    request.onError(new CoreClientError("permission denied", {
      kind: "core",
      coreCode: "open-directory",
      exitCode: 2,
    }))
    return { completion: Promise.resolve(), send: async () => true, close: () => undefined }
  }))

  expect(process.exitCode).toBe(2)
})

test("aborts and waits for the core probe when renderer startup fails", async () => {
  let startProbe!: () => void
  let aborted = false
  const probeStarted = new Promise<void>((resolve) => {
    startProbe = resolve
  })
  const failingDependencies: MainDependencies = {
    defaultCoreProbePath: () => "/mock/neovifm-core-probe",
    renderApp: async () => {
      await probeStarted
      throw new Error("renderer setup failed")
    },
    startCoreSession: (request) => {
      startProbe()
      const completion = new Promise<void>((resolve) => {
        request.signal?.addEventListener("abort", () => {
          aborted = true
          resolve()
        }, { once: true })
      })
      return { completion, send: async () => true, close: () => undefined }
    },
  }

  await expect(main([], failingDependencies)).rejects.toThrow("renderer setup failed")
  expect(aborted).toBe(true)
})
