import { afterEach, expect, test } from "bun:test"
import { mkdtemp, rm, writeFile } from "node:fs/promises"
import { constants, tmpdir } from "node:os"
import { resolve } from "node:path"
import { createComponent } from "solid-js"
import { testRender } from "@opentui/solid"

import { App } from "../src/app.js"
import { CoreClientError, runCoreProbe } from "../src/core-client.js"

let directory: string | undefined
let rightDirectory: string | undefined
let setup: Awaited<ReturnType<typeof testRender>> | undefined

afterEach(async () => {
  setup?.renderer.destroy()
  setup = undefined
  if (directory !== undefined) {
    await rm(directory, { recursive: true })
    directory = undefined
  }
  if (rightDirectory !== undefined) {
    await rm(rightDirectory, { recursive: true })
    rightDirectory = undefined
  }
})

test("renders a real C core workspace in wide and compact layouts", async () => {
  const executable = process.env.NEOVIFM_CORE_PROBE
  if (executable === undefined || executable.length === 0) {
    throw new Error("NEOVIFM_CORE_PROBE must point to the built core probe")
  }

  directory = await mkdtemp(resolve(tmpdir(), "neovifm-integration-"))
  await writeFile(resolve(directory, "visible.txt"), "content")

  rightDirectory = await mkdtemp(resolve(tmpdir(), "neovifm-integration-right-"))
  await writeFile(resolve(rightDirectory, "right.txt"), "right")
  const result = await runCoreProbe({ executable, targetPath: directory, rightPath: rightDirectory })
  expect(result.hello.implementation).toBe("neovifm-core-probe")
  if (!("workspace" in result)) throw new Error("expected v1 workspace")
  expect(result.workspace.left.entries.some((entry) => entry.name_display === "visible.txt")).toBe(true)

  setup = await testRender(
    () => createComponent(App, { workspace: result.workspace }),
    { width: 100, height: 20 },
  )
  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("visible.txt")
  expect(setup.captureCharFrame()).toContain("right.txt")

  setup.resize(60, 20)
  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("60x20")
  expect(setup.captureCharFrame()).not.toContain("right.txt")
})

test("surfaces a real core error for a missing directory", async () => {
  const executable = process.env.NEOVIFM_CORE_PROBE
  if (executable === undefined || executable.length === 0) {
    throw new Error("NEOVIFM_CORE_PROBE must point to the built core probe")
  }

  const missingPath = resolve(tmpdir(), "neovifm-missing-core-probe-directory")

  await expect(runCoreProbe({ executable, targetPath: missingPath })).rejects.toMatchObject({
    kind: "core",
    coreCode: "open-directory",
  } satisfies Partial<CoreClientError>)
})

test(
  "emits a bounded snapshot-too-large error before client input limits are exceeded",
  async () => {
    const executable = process.env.NEOVIFM_CORE_PROBE
    if (executable === undefined || executable.length === 0) {
      throw new Error("NEOVIFM_CORE_PROBE must point to the built core probe")
    }

    directory = await mkdtemp(resolve(tmpdir(), "neovifm-oversized-core-probe-"))
    const entryCount = 3500
    const namePrefix = "n".repeat(232)
    for (let start = 0; start < entryCount; start += 100) {
      await Promise.all(Array.from(
        { length: Math.min(100, entryCount - start) },
        (_, offset) => writeFile(
          resolve(directory!, `${namePrefix}-${String(start + offset).padStart(4, "0")}`),
          "",
        ),
      ))
    }

    const probe = Bun.spawn({
      cmd: [executable, directory],
      stdout: "pipe",
      stderr: "pipe",
    })
    const [stdout, exitCode] = await Promise.all([
      new Response(probe.stdout).text(),
      probe.exited,
    ])
    const records = stdout.trimEnd().split("\n").map((line) => JSON.parse(line))

    expect(exitCode).toBe(1)
    expect(records).toHaveLength(2)
    expect(records[1]).toMatchObject({
      type: "error",
      payload: {
        code: "snapshot-too-large",
        os_error: constants.errno.E2BIG,
        retryable: false,
      },
    })
  },
  30_000,
)
