import { afterAll, afterEach, beforeAll, describe, expect, test } from "bun:test"
import { link, mkdtemp, readFile, rm, writeFile } from "node:fs/promises"
import { tmpdir } from "node:os"
import { dirname, join } from "node:path"

import { CoreClientError, runCoreProbe, startCoreSession } from "../src/core-client.js"
import type { FakeCoreScenario } from "./fixtures/fake-core.js"

const hello =
  '{"protocol":"neovifm-core","version":0,"type":"hello","sequence":0,"payload":{"implementation":"test-probe","capabilities":["snapshot-v0"]}}'
const snapshot =
  '{"protocol":"neovifm-core","version":0,"type":"snapshot","sequence":1,"payload":{"cwd_display":"/tmp","cwd_bytes_hex":"2f746d70","generated_at_unix_ms":"0","cursor":-1,"entry_count":0,"entries":[]}}'
const workspaceHello =
  '{"protocol":"neovifm-core","version":1,"type":"hello","sequence":0,"payload":{"implementation":"test-workspace","capabilities":["workspace-v1"]}}'
const workspaceSnapshot =
  '{"protocol":"neovifm-core","version":1,"type":"workspace-snapshot","sequence":1,"payload":{"active_pane":"left","left":{"cwd_display":"/tmp","cwd_bytes_hex":"2f746d70","generated_at_unix_ms":"0","cursor":-1,"entry_count":0,"entries":[]},"right":{"cwd_display":"/var","cwd_bytes_hex":"2f766172","generated_at_unix_ms":"0","cursor":-1,"entry_count":0,"entries":[]}}}'

const temporaryDirectories: string[] = []
const deferredWindowsDirectories: string[] = []
let runnerDirectory: string | undefined
let runnerPromise: Promise<string> | undefined

afterEach(async () => {
  const directories = temporaryDirectories.splice(0)
  if (process.platform === "win32") {
    await Promise.all(directories.map(async (path) => {
      try {
        await rm(path, { recursive: true, force: true })
      } catch {
        deferredWindowsDirectories.push(path)
      }
    }))
  } else {
    await Promise.all(directories.map((path) => rm(path, { recursive: true, force: true })))
  }
})

afterAll(async () => {
  if (deferredWindowsDirectories.length > 0) await Bun.sleep(6250)
  await Promise.all(deferredWindowsDirectories.map((path) => rm(path, { recursive: true, force: true })))
  if (runnerDirectory !== undefined) await rm(runnerDirectory, { recursive: true, force: true })
}, 10_000)

async function getProbeRunner(): Promise<string> {
  runnerPromise ??= (async () => {
    runnerDirectory = await mkdtemp(join(tmpdir(), "neovifm-core-runner-"))
    const executable = join(runnerDirectory, process.platform === "win32" ? "fake-core.exe" : "fake-core")
    await Bun.build({
      entrypoints: [join(import.meta.dir, "fixtures", "fake-core.ts")],
      compile: { outfile: executable, autoloadBunfig: false },
    })
    return executable
  })()
  return runnerPromise
}

beforeAll(async () => {
  await getProbeRunner()
}, 30_000)

async function makeProbe(scenario: FakeCoreScenario): Promise<string> {
  const directory = await mkdtemp(join(tmpdir(), "neovifm-core-client-"))
  temporaryDirectories.push(directory)
  const executable = join(directory, process.platform === "win32" ? "probe.exe" : "probe")
  await Promise.all([
    link(await getProbeRunner(), executable),
    writeFile(join(directory, "scenario.json"), JSON.stringify(scenario)),
  ])
  return executable
}

describe("core probe client", () => {
  test("passes both pane paths and accepts one atomic v1 workspace", async () => {
    const executable = await makeProbe({
      expectedArguments: ["/tmp", "/var"],
      events: [
        { type: "stdout-line", value: workspaceHello },
        { type: "stdout-line", value: workspaceSnapshot },
      ],
    })

    const result = await runCoreProbe({ executable, targetPath: "/tmp", rightPath: "/var" })

    if (!("workspace" in result)) throw new Error("expected v1 workspace")
    expect(result.workspace.right.cwd_display).toBe("/var")
  })

  test("rejects a terminal record whose version differs from hello", async () => {
    const v1Error =
      '{"protocol":"neovifm-core","version":1,"type":"error","sequence":1,"payload":{"code":"denied","message":"denied","retryable":false}}'
    const executable = await makeProbe({ events: [
      { type: "stdout-line", value: hello },
      { type: "stdout-line", value: v1Error },
    ] })

    await expect(runCoreProbe({ executable, targetPath: "/tmp" })).rejects.toMatchObject({
      kind: "protocol",
    } satisfies Partial<CoreClientError>)
  })

  test(
    "drains stdout and a large stderr stream concurrently",
    async () => {
      const executable = await makeProbe({ events: [
        { type: "stdout-line", value: hello },
        { type: "stderr-repeat", count: 6000, prefix: "diagnostic-", suffix: "-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n" },
        { type: "stdout-line", value: snapshot },
      ] })

      const result = await runCoreProbe({ executable, targetPath: "/tmp" })

      expect(result.hello.implementation).toBe("test-probe")
      if (!("snapshot" in result)) throw new Error("expected v0 snapshot")
      expect(result.snapshot.cwd_display).toBe("/tmp")
      expect(result.stderr).toContain("diagnostic-0000")
      expect(result.stderrTruncated).toBe(true)
    },
    10_000,
  )

  test("surfaces a structured core error even when the probe exits non-zero", async () => {
    const errorRecord =
      '{"protocol":"neovifm-core","version":0,"type":"error","sequence":1,"payload":{"code":"scan-denied","message":"permission denied","retryable":false,"os_error":13}}'
    const executable = await makeProbe({
      events: [
        { type: "stdout-line", value: hello },
        { type: "stdout-line", value: errorRecord },
        { type: "stderr", value: "open failed\n" },
      ],
      exitCode: 2,
    })

    try {
      await runCoreProbe({ executable, targetPath: "/private" })
      throw new Error("expected runCoreProbe to reject")
    } catch (error) {
      expect(error).toBeInstanceOf(CoreClientError)
      expect(error).toMatchObject({
        kind: "core",
        coreCode: "scan-denied",
        exitCode: 2,
      })
      expect((error as Error).message).toContain("permission denied")
    }
  })

  test("rejects a non-zero exit after a valid snapshot", async () => {
    const executable = await makeProbe({
      events: [
        { type: "stdout-line", value: hello },
        { type: "stdout-line", value: snapshot },
      ],
      exitCode: 7,
    })

    try {
      await runCoreProbe({ executable, targetPath: "/tmp" })
      throw new Error("expected runCoreProbe to reject")
    } catch (error) {
      expect(error).toBeInstanceOf(CoreClientError)
      expect(error).toMatchObject({ kind: "exit", exitCode: 7 })
    }
  })

  test("reports malformed protocol output without waiting for diagnostics", async () => {
    const executable = await makeProbe({ events: [
      { type: "stdout-line", value: "{broken json}" },
      { type: "stderr-repeat", count: 3000, prefix: "protocol-diagnostic-", suffix: "-xxxxxxxxxxxxxxxxxxxxxxxx\n" },
    ] })

    try {
      await runCoreProbe({ executable, targetPath: "/tmp" })
      throw new Error("expected runCoreProbe to reject")
    } catch (error) {
      expect(error).toBeInstanceOf(CoreClientError)
      expect(error).toMatchObject({ kind: "protocol" })
    }
  })

  test("terminates a malformed protocol stream before its timeout", async () => {
    const executable = await makeProbe({ events: [
      { type: "stdout-line", value: "{broken json}" },
      { type: "sleep", milliseconds: 6000 },
    ] })

    await expect(runCoreProbe({
      executable,
      targetPath: "/tmp",
      timeoutMs: 10_000,
    })).rejects.toMatchObject({ kind: "protocol" } satisfies Partial<CoreClientError>)
  }, 5_000)

  test("does not wait for a killed probe's descendant to close inherited pipes", async () => {
    const executable = await makeProbe({ events: [
      { type: "stdout-line", value: "{broken json}" },
      { type: "spawn-child-and-wait", milliseconds: 6000 },
    ] })

    await expect(runCoreProbe({
      executable,
      targetPath: "/tmp",
      timeoutMs: 10_000,
    })).rejects.toMatchObject({ kind: "protocol" } satisfies Partial<CoreClientError>)
  }, 5_000)

  test("bounds the protocol stream to hello and one terminal record", async () => {
    const executable = await makeProbe({ events: [
      { type: "stdout-line", value: hello },
      { type: "stdout-line", value: snapshot },
      { type: "stdout-line", value: snapshot },
    ] })

    try {
      await runCoreProbe({ executable, targetPath: "/tmp" })
      throw new Error("expected runCoreProbe to reject")
    } catch (error) {
      expect(error).toBeInstanceOf(CoreClientError)
      expect(error).toMatchObject({ kind: "protocol" })
      expect((error as Error).cause).toHaveProperty(
        "message",
        "JSONL stream exceeds maximumRecords of 2 records",
      )
    }
  })

  test("rejects blank lines in the protocol stream", async () => {
    const executable = await makeProbe({ events: [
      { type: "stdout-line", value: hello },
      { type: "stdout-line", value: "" },
      { type: "stdout-line", value: snapshot },
    ] })

    await expect(runCoreProbe({ executable, targetPath: "/tmp" })).rejects.toMatchObject({
      kind: "protocol",
    })
  })

  test("emits validated records to the state boundary in arrival order", async () => {
    const executable = await makeProbe({ events: [
      { type: "stdout-line", value: hello },
      { type: "stdout-line", value: snapshot },
    ] })
    const types: string[] = []

    await runCoreProbe({
      executable,
      targetPath: "/tmp",
      onRecord: (record) => types.push(record.type),
    })

    expect(types).toEqual(["hello", "snapshot"])
  })

  test("rejects caller limits that would disable protocol resource bounds", async () => {
    const executable = await makeProbe({ events: [
      { type: "stdout-line", value: hello },
      { type: "stdout-line", value: snapshot },
    ] })

    await expect(
      runCoreProbe({
        executable,
        targetPath: "/tmp",
        maximumRecordBytes: Number.MAX_SAFE_INTEGER,
      }),
    ).rejects.toThrow("maximumRecordBytes")
  })

  test("wraps executable spawn failures", async () => {
    try {
      await runCoreProbe({
        executable: join(tmpdir(), "missing-neovifm-core-probe"),
        targetPath: "/tmp",
      })
      throw new Error("expected runCoreProbe to reject")
    } catch (error) {
      expect(error).toBeInstanceOf(CoreClientError)
      expect(error).toMatchObject({ kind: "spawn" })
    }
  })

  test("terminates a probe that exceeds its deadline", async () => {
    const executable = await makeProbe({ events: [
      { type: "sleep", milliseconds: 5000 },
    ] })

    try {
      await runCoreProbe({ executable, targetPath: "/tmp", timeoutMs: 25 })
      throw new Error("expected runCoreProbe to reject")
    } catch (error) {
      expect(error).toBeInstanceOf(CoreClientError)
      expect(error).toMatchObject({ kind: "timeout" })
    }
  })

  test("enforces the deadline when a killed probe leaves a descendant holding pipes", async () => {
    const executable = await makeProbe({ events: [
      { type: "spawn-child-and-wait", milliseconds: 1000 },
    ] })

    await expect(runCoreProbe({
      executable,
      targetPath: "/tmp",
      timeoutMs: 50,
    })).rejects.toMatchObject({ kind: "timeout" } satisfies Partial<CoreClientError>)
  })

  test("prioritizes caller cancellation over a partial protocol record", async () => {
    const executable = await makeProbe({ events: [
      { type: "stdout", value: '{"protocol":' },
      { type: "sleep", milliseconds: 6000 },
    ] })
    const controller = new AbortController()
    setTimeout(() => controller.abort(), 500)

    await expect(runCoreProbe({
      executable,
      targetPath: "/tmp",
      signal: controller.signal,
      timeoutMs: 10_000,
    })).rejects.toMatchObject({ kind: "cancelled" } satisfies Partial<CoreClientError>)
  }, 5_000)

  test("honors a caller cancellation signal before the deadline", async () => {
    const executable = await makeProbe({ events: [
      { type: "sleep", milliseconds: 5000 },
    ] })
    const controller = new AbortController()
    setTimeout(() => controller.abort(), 10)

    try {
      await runCoreProbe({
        executable,
        targetPath: "/tmp",
        signal: controller.signal,
        timeoutMs: 250,
      })
      throw new Error("expected runCoreProbe to reject")
    } catch (error) {
      expect(error).toBeInstanceOf(CoreClientError)
      expect(error).toMatchObject({ kind: "cancelled" })
    }
  })
})

test("serializes session writes and reports a closed command pipe", async () => {
  const sessionHello = '{"protocol":"neovifm-core","version":3,"type":"hello","sequence":0,"payload":{"implementation":"test-session","capabilities":["preview-session-v3"]}}'
  const sessionSnapshot = '{"protocol":"neovifm-core","version":3,"type":"workspace-snapshot","sequence":1,"payload":{"command_sequence":0,"trigger":"initial","active_pane":"left","left":{"cwd_display":"/tmp","cwd_bytes_hex":"2f746d70","generated_at_unix_ms":"0","cursor":-1,"entry_count":0,"entries":[]},"right":{"cwd_display":"/var","cwd_bytes_hex":"2f766172","generated_at_unix_ms":"0","cursor":-1,"entry_count":0,"entries":[]}}}'
  const executable = await makeProbe({ events: [
    { type: "stdout-line", value: sessionHello },
    { type: "stdout-line", value: sessionSnapshot },
  ] })
  const errors: CoreClientError[] = []
  let ready = false
  const session = startCoreSession({
    executable,
    leftPath: "/tmp",
    rightPath: "/var",
    onRecord: (record) => { if (record.type === "workspace-snapshot") ready = true },
    onError: (error) => errors.push(error),
  })
  for (let attempt = 0; !ready && attempt < 300; ++attempt) await Bun.sleep(10)
  expect(ready).toBe(true)
  await session.completion
  expect(await session.send({ action: "refresh" })).toBe(false)
  expect(errors).toHaveLength(1)
  expect(errors[0]).toMatchObject({ kind: "protocol" })
  session.close()
}, 8_000)

test("forces session persistence flags from the request over inherited values", async () => {
  const executable = await makeProbe({ events: [
    { type: "write-env", filename: "flags.txt", names: ["NEOVIFM_SESSION_RESUME", "NEOVIFM_SESSION_PERSIST"] },
    { type: "stdout-line", value: workspaceHello },
    { type: "stdout-line", value: workspaceSnapshot },
  ] })
  const flagsFile = join(dirname(executable), "flags.txt")
  const previousResume = process.env.NEOVIFM_SESSION_RESUME
  const previousPersist = process.env.NEOVIFM_SESSION_PERSIST
  process.env.NEOVIFM_SESSION_RESUME = "1"
  process.env.NEOVIFM_SESSION_PERSIST = "1"
  try {
    const session = startCoreSession({
      executable,
      leftPath: "/tmp",
      rightPath: "/var",
      resume: false,
      persist: false,
      onRecord: () => undefined,
      onError: () => undefined,
    })
    await session.completion
    expect(await readFile(flagsFile, "utf8")).toBe("0:0")
  } finally {
    if (previousResume === undefined) delete process.env.NEOVIFM_SESSION_RESUME
    else process.env.NEOVIFM_SESSION_RESUME = previousResume
    if (previousPersist === undefined) delete process.env.NEOVIFM_SESSION_PERSIST
    else process.env.NEOVIFM_SESSION_PERSIST = previousPersist
  }
})
