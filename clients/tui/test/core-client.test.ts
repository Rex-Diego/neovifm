import { afterEach, describe, expect, test } from "bun:test"
import { chmod, mkdtemp, rm, writeFile } from "node:fs/promises"
import { tmpdir } from "node:os"
import { join } from "node:path"

import { CoreClientError, runCoreProbe } from "../src/core-client.js"

const hello =
  '{"protocol":"neovifm-core","version":0,"type":"hello","sequence":0,"payload":{"implementation":"test-probe","capabilities":["snapshot-v0"]}}'
const snapshot =
  '{"protocol":"neovifm-core","version":0,"type":"snapshot","sequence":1,"payload":{"cwd_display":"/tmp","cwd_bytes_hex":"2f746d70","generated_at_unix_ms":"0","cursor":-1,"entry_count":0,"entries":[]}}'

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

  test("drains the process before reporting malformed protocol output", async () => {
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
      expect(error).toMatchObject({ kind: "protocol", exitCode: 0 })
      expect((error as CoreClientError).stderr).toContain("protocol-diagnostic-0000")
    }
  })

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
        "Protocol stream exceeds 2 records",
      )
    }
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
})
