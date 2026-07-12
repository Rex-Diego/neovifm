import { afterEach, expect, test } from "bun:test"
import { mkdtemp, rm, writeFile } from "node:fs/promises"
import { tmpdir } from "node:os"
import { resolve } from "node:path"
import { createComponent } from "solid-js"
import { testRender } from "@opentui/solid"

import { App } from "../src/app.js"
import { runCoreProbe } from "../src/core-client.js"

let directory: string | undefined
let setup: Awaited<ReturnType<typeof testRender>> | undefined

afterEach(async () => {
  setup?.renderer.destroy()
  setup = undefined
  if (directory !== undefined) {
    await rm(directory, { recursive: true })
    directory = undefined
  }
})

test("renders a real C core snapshot in wide and compact layouts", async () => {
  const executable = process.env.NEOVIFM_CORE_PROBE
  if (executable === undefined || executable.length === 0) {
    throw new Error("NEOVIFM_CORE_PROBE must point to the built core probe")
  }

  directory = await mkdtemp(resolve(tmpdir(), "neovifm-integration-"))
  await writeFile(resolve(directory, "visible.txt"), "content")

  const result = await runCoreProbe({ executable, targetPath: directory })
  expect(result.hello.implementation).toBe("neovifm-core-probe")
  expect(result.snapshot.entries.some((entry) => entry.name_display === "visible.txt")).toBe(true)

  setup = await testRender(
    () => createComponent(App, { snapshot: result.snapshot }),
    { width: 100, height: 20 },
  )
  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("DETAILS")
  expect(setup.captureCharFrame()).toContain("visible.txt")

  setup.resize(60, 20)
  await setup.renderOnce()
  expect(setup.captureCharFrame()).toContain("60x20")
  expect(setup.captureCharFrame()).not.toContain("DETAILS")
})
