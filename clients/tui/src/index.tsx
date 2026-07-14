import { resolve } from "node:path"
import { createSignal } from "solid-js"
import { render } from "@opentui/solid"

import { App, type AppProps } from "./app.js"
import { CoreClientError, startCoreSession } from "./core-client.js"
import { initialProbeState, reduceProbeState, type ProbeState } from "./probe-state.js"
import { sanitizeDisplayText } from "./protocol.js"

export function defaultCoreProbePath(): string {
	return resolve(import.meta.dir, "../../../src/neovifm-core-session")
}

export interface MainDependencies {
  readonly defaultCoreProbePath: () => string
  readonly renderApp: (props: () => AppProps) => Promise<void>
  readonly startCoreSession: typeof startCoreSession
}

const DEFAULT_MAIN_DEPENDENCIES: MainDependencies = {
  defaultCoreProbePath,
  renderApp: async (props) => render(() => <App {...props()} />),
  startCoreSession,
}

export function toUiErrorMessage(error: unknown): string {
  if (error instanceof CoreClientError) {
    const details: string[] = [error.kind]
    if (error.coreCode !== undefined) {
      details.push(error.coreCode)
    }
    if (error.exitCode !== undefined) {
      details.push(`exit ${error.exitCode}`)
    }
    if (error.stderrTruncated) {
      details.push("diagnostics truncated")
    }
    return sanitizeDisplayText(`[${details.join(" · ")}] ${error.message}`)
  }
  return sanitizeDisplayText(error instanceof Error ? error.message : String(error))
}

export function exitCodeFor(error: unknown): number {
  if (error instanceof CoreClientError) {
    if (error.kind === "cancelled") {
      return 130
    }
    if (error.exitCode !== undefined && error.exitCode !== 0) {
      return error.exitCode
    }
  }
  return 1
}

export function appPropsFor(state: ProbeState, clientError?: string): AppProps {
  return {
    loading: state.phase === "awaiting-hello" || state.phase === "awaiting-terminal",
    workspace: state.phase === "ready" && "workspace" in state ? state.workspace : undefined,
    error: clientError ?? (state.phase === "failed" ? state.error.message : undefined),
  }
}

export async function main(
  args: readonly string[] = process.argv.slice(2),
  dependencies: MainDependencies = DEFAULT_MAIN_DEPENDENCIES,
): Promise<void> {
  const executable = process.env.NEOVIFM_CORE_PROBE?.trim() || dependencies.defaultCoreProbePath()
  const targetPath = args[0] ?? process.cwd()
  const rightPath = args[1] ?? targetPath
  const controller = new AbortController()
  const [probeState, setProbeState] = createSignal<ProbeState>(initialProbeState())
  const [clientError, setClientError] = createSignal<string | undefined>()

  const session = dependencies.startCoreSession({
    executable,
    leftPath: targetPath,
    rightPath,
    signal: controller.signal,
    onRecord: (record) => setProbeState((state) => reduceProbeState(state, record)),
    onError: (error) => {
      process.exitCode = exitCodeFor(error)
      setClientError(toUiErrorMessage(error))
    },
  })

  try {
    await dependencies.renderApp(() => {
      const state = probeState()
      return (
        {
          ...appPropsFor(state, clientError()),
          onCancel: () => { session.close(); controller.abort() },
          onCommand: (command) => session.send(command),
        }
      )
    })
  } catch (error) {
    controller.abort()
    session.close()
    await session.completion
    process.exitCode = exitCodeFor(error)
    throw error
  }
  session.close()
  await session.completion
}

if (import.meta.main) {
  await main()
}
