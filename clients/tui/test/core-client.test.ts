import { afterEach, describe, expect, test } from "bun:test"
import { chmod, mkdtemp, rm, writeFile } from "node:fs/promises"
import { tmpdir } from "node:os"
import { join } from "node:path"

import { CoreClientError, runCoreProbe } from "../src/core-client.js"

const hello =
  '{"protocol":"neovifm-core","version":0,"type":"hello","sequence":0,"payload":{"implementation":"test-probe","capabilities":["snapshot-v0"]}}'
const snapshot =
  '{"protocol":"neovifm-core","version":0,"type":"snapshot","sequence":1,"payload":{"cwd_display":"/tmp","cwd_bytes_hex":"2f746d70","generated_at_unix_ms":"0","cursor":-1,"entry_count":0,"entries":[]}}'
const workspaceHello =
  '{"protocol":"neovifm-core","version":1,"type":"hello","sequence":0,"payload":{"implementation":"test-workspace","capabilities":["workspace-v1"]}}'
const workspaceSnapshot =
  '{"protocol":"neovifm-core","version":1,"type":"workspace-snapshot","sequence":1,"payload":{"active_pane":"left","left":{"cwd_display":"/tmp","cwd_bytes_hex":"2f746d70","generated_at_unix_ms":"0","cursor":-1,"entry_count":0,"entries":[]},"right":{"cwd_display":"/var","cwd_bytes_hex":"2f766172","generated_at_unix_ms":"0","cursor":-1,"entry_count":0,"entries":[]}}}'

const temporaryDirectories: string[] = []

afterEach(async () => {
  await Promise.all(
    temporaryDirectories.splice(0).map((path) => rm(path, { recursive: true })),
  )
})

async function makeProbe(body: string): Promise<string> {
  const directory = await mkdtemp(join(tmpdir(), "neovifm-core-client-"))
  temporaryDirectories.push(directory)
  const executable = join(directory, "probe.sh")
  await writeFile(executable, `#!/bin/sh\n${body}\n`)
  await chmod(executable, 0o700)
  return executable
}

describe("core probe client", () => {
  test("passes both pane paths and accepts one atomic v1 workspace", async () => {
    const executable = await makeProbe(`
test "$1" = /tmp && test "$2" = /var || exit 9
printf '%s\\n' '${workspaceHello}'
printf '%s\\n' '${workspaceSnapshot}'
`)

    const result = await runCoreProbe({ executable, targetPath: "/tmp", rightPath: "/var" })

    if (!("workspace" in result)) throw new Error("expected v1 workspace")
    expect(result.workspace.right.cwd_display).toBe("/var")
  })

  test("rejects a terminal record whose version differs from hello", async () => {
    const v1Error =
      '{"protocol":"neovifm-core","version":1,"type":"error","sequence":1,"payload":{"code":"denied","message":"denied","retryable":false}}'
    const executable = await makeProbe(`
printf '%s\\n' '${hello}'
printf '%s\\n' '${v1Error}'
`)

    await expect(runCoreProbe({ executable, targetPath: "/tmp" })).rejects.toMatchObject({
      kind: "protocol",
    } satisfies Partial<CoreClientError>)
  })

  test(
    "drains stdout and a large stderr stream concurrently",
    async () => {
      const executable = await makeProbe(`
printf '%s\\n' '${hello}'
i=0
while [ "$i" -lt 6000 ]; do
  printf 'diagnostic-%04d-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\\n' "$i" >&2
  i=$((i + 1))
done
printf '%s\\n' '${snapshot}'
`)

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
    const executable = await makeProbe(`
printf '%s\\n' '${hello}'
printf '%s\\n' '${errorRecord}'
printf 'open failed\\n' >&2
exit 2
`)

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
    const executable = await makeProbe(`
printf '%s\\n' '${hello}'
printf '%s\\n' '${snapshot}'
exit 7
`)

    try {
      await runCoreProbe({ executable, targetPath: "/tmp" })
      throw new Error("expected runCoreProbe to reject")
    } catch (error) {
      expect(error).toBeInstanceOf(CoreClientError)
      expect(error).toMatchObject({ kind: "exit", exitCode: 7 })
    }
  })

  test("reports malformed protocol output without waiting for diagnostics", async () => {
    const executable = await makeProbe(`
printf '{broken json}\\n'
i=0
while [ "$i" -lt 3000 ]; do
  printf 'protocol-diagnostic-%04d-xxxxxxxxxxxxxxxxxxxxxxxx\\n' "$i" >&2
  i=$((i + 1))
done
`)

    try {
      await runCoreProbe({ executable, targetPath: "/tmp" })
      throw new Error("expected runCoreProbe to reject")
    } catch (error) {
      expect(error).toBeInstanceOf(CoreClientError)
      expect(error).toMatchObject({ kind: "protocol" })
    }
  })

  test("terminates a malformed protocol stream before its timeout", async () => {
    const executable = await makeProbe(`
printf '{broken json}\\n'
exec sleep 6
`)

    await expect(runCoreProbe({
      executable,
      targetPath: "/tmp",
      timeoutMs: 10_000,
    })).rejects.toMatchObject({ kind: "protocol" } satisfies Partial<CoreClientError>)
  }, 5_000)

  test("does not wait for a killed probe's descendant to close inherited pipes", async () => {
    const executable = await makeProbe(`
sleep 6 &
printf '{broken json}\\n'
wait
`)

    await expect(runCoreProbe({
      executable,
      targetPath: "/tmp",
      timeoutMs: 10_000,
    })).rejects.toMatchObject({ kind: "protocol" } satisfies Partial<CoreClientError>)
  }, 5_000)

  test("bounds the protocol stream to hello and one terminal record", async () => {
    const executable = await makeProbe(`
printf '%s\\n' '${hello}'
printf '%s\\n' '${snapshot}'
printf '%s\\n' '${snapshot}'
`)

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
    const executable = await makeProbe(`
printf '%s\\n' '${hello}'
printf '\\n'
printf '%s\\n' '${snapshot}'
`)

    await expect(runCoreProbe({ executable, targetPath: "/tmp" })).rejects.toMatchObject({
      kind: "protocol",
    })
  })

  test("emits validated records to the state boundary in arrival order", async () => {
    const executable = await makeProbe(`
printf '%s\\n' '${hello}'
printf '%s\\n' '${snapshot}'
`)
    const types: string[] = []

    await runCoreProbe({
      executable,
      targetPath: "/tmp",
      onRecord: (record) => types.push(record.type),
    })

    expect(types).toEqual(["hello", "snapshot"])
  })

  test("rejects caller limits that would disable protocol resource bounds", async () => {
    const executable = await makeProbe(`
printf '%s\\n' '${hello}'
printf '%s\\n' '${snapshot}'
`)

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
    const executable = await makeProbe("sleep 5")

    try {
      await runCoreProbe({ executable, targetPath: "/tmp", timeoutMs: 25 })
      throw new Error("expected runCoreProbe to reject")
    } catch (error) {
      expect(error).toBeInstanceOf(CoreClientError)
      expect(error).toMatchObject({ kind: "timeout" })
    }
  })

  test("enforces the deadline when a killed probe leaves a descendant holding pipes", async () => {
    const executable = await makeProbe(`
sleep 1 &
wait
`)

    await expect(runCoreProbe({
      executable,
      targetPath: "/tmp",
      timeoutMs: 50,
    })).rejects.toMatchObject({ kind: "timeout" } satisfies Partial<CoreClientError>)
  })

  test("prioritizes caller cancellation over a partial protocol record", async () => {
    const executable = await makeProbe(`
printf '{"protocol":'
exec sleep 6
`)
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
    const executable = await makeProbe("sleep 5")
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
