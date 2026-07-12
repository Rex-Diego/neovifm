import { resolve } from "node:path"
import { render } from "@opentui/solid"

import { App } from "./app.js"
import { runCoreProbe } from "./core-client.js"

function defaultCoreProbePath(): string {
  return resolve(import.meta.dir, "../../../src/neovifm-core-probe")
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error)
}

export async function main(args: readonly string[] = process.argv.slice(2)): Promise<void> {
  const executable = process.env.NEOVIFM_CORE_PROBE?.trim() || defaultCoreProbePath()
  const targetPath = args[0] ?? process.cwd()

  try {
    const result = await runCoreProbe({ executable, targetPath })
    await render(() => <App snapshot={result.snapshot} />)
  } catch (error) {
    await render(() => <App error={errorMessage(error)} />)
  }
}

if (import.meta.main) {
  await main()
}
