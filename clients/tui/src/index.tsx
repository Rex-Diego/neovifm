import { resolve } from "node:path"
import { createSignal } from "solid-js"
import { render } from "@opentui/solid"
import type { CliRendererConfig } from "@opentui/core"

import { App, type AppProps } from "./app.js"
import { CoreClientError, startCoreSession } from "./core-client.js"
import { initialProbeState, reduceProbeState, type ProbeState } from "./probe-state.js"
import { sanitizeDisplayText } from "./protocol.js"
import { copyTextToClipboard } from "./clipboard.js"
import { openFile as launchOpenFile, openResolvedFile as launchResolvedOpenFile } from "./open-file.js"
import type { OpenFileDependencies } from "./open-file.js"

export function defaultCoreProbePath(): string {
	const executable = process.platform === "win32"
		? "neovifm-core-session.exe"
		: "neovifm-core-session"
	return resolve(import.meta.dir, "../../../src", executable)
}

export interface MainDependencies {
  readonly defaultCoreProbePath: () => string
  readonly renderApp: (props: () => AppProps) => Promise<void>
  readonly startCoreSession: typeof startCoreSession
  readonly openEditor?: (path: string) => Promise<void>
  readonly openFile?: (path: string) => Promise<void>
  readonly openResolved?: (argv: readonly string[]) => Promise<void>
  readonly copyText?: (text: string) => Promise<void> | void
}

export type RenderMount = (
  node: Parameters<typeof render>[0],
  config: CliRendererConfig,
) => Promise<void>

export function renderUntilDestroyed(
  props: () => AppProps,
  mount: RenderMount = render,
): Promise<void> {
  return new Promise<void>((resolve, reject) => {
    void mount(() => <App {...props()} />, { onDestroy: resolve }).catch(reject)
  })
}

const DEFAULT_MAIN_DEPENDENCIES: MainDependencies = {
  defaultCoreProbePath,
  renderApp: renderUntilDestroyed,
  startCoreSession,
  copyText,
}

function splitEditorCommand(command: string): readonly string[] {
  const parts = command.match(/(?:[^\s"']+|"[^"]*"|'[^']*')+/g) ?? []
  return parts.map((part) => {
    const quote = part[0]
    return (quote === '"' || quote === "'") && part.at(-1) === quote
      ? part.slice(1, -1)
      : part
  }).filter((part) => part.length !== 0)
}

export function editorCommand(
  path: string,
  environment: Readonly<Record<string, string | undefined>> = process.env,
): readonly string[] {
  if (path.length === 0 || path.includes("\0")) throw new Error("Editor path is invalid")
  const configured = environment.VISUAL?.trim() || environment.EDITOR?.trim() || "vi"
  const command = splitEditorCommand(configured)
  if (command.length === 0) throw new Error("Editor command is empty")
  return [...command, "--", path]
}

export async function openEditor(path: string): Promise<void> {
  const process = Bun.spawn({
    cmd: [...editorCommand(path)],
    stdin: "inherit",
    stdout: "inherit",
    stderr: "inherit",
  })
  const exitCode = await process.exited
  if (exitCode !== 0) throw new Error(`Editor exited with status ${exitCode}`)
}

export function openFile(path: string, dependencies?: OpenFileDependencies): Promise<void> {
  return launchOpenFile(path, dependencies)
}

export function openResolvedFile(argv: readonly string[]): Promise<void> {
  return launchResolvedOpenFile(argv)
}

export async function copyText(text: string): Promise<void> {
  await copyTextToClipboard(text)
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
	const session = state.phase === "ready" && "session" in state ? state : undefined
  return {
    loading: state.phase === "awaiting-hello" || state.phase === "awaiting-terminal",
    workspace: state.phase === "ready" && "workspace" in state ? state.workspace : undefined,
    error: clientError ?? (state.phase === "failed" ? state.error.message : undefined),
    preview: session?.preview,
    tasks: session?.tasks,
		actionTasks: session?.actionTasks,
		resourceTasks: session?.resourceTasks,
    open: session?.open,
    commandError: session?.commandError?.message,
    capabilities: state.phase === "ready" ? state.hello.capabilities : undefined,
  }
}

export async function main(
  args: readonly string[] = process.argv.slice(2),
  dependencies: MainDependencies = DEFAULT_MAIN_DEPENDENCIES,
): Promise<void> {
  const executable = process.env.NEOVIFM_CORE_PROBE?.trim() || dependencies.defaultCoreProbePath()
  const resume = args.length === 0
  const targetPath = args[0] ?? process.cwd()
  const rightPath = args[1] ?? targetPath
  const controller = new AbortController()
  const [probeState, setProbeState] = createSignal<ProbeState>(initialProbeState())
  const [clientError, setClientError] = createSignal<string | undefined>()

  const session = dependencies.startCoreSession({
    executable,
    leftPath: targetPath,
    rightPath,
    resume,
    persist: true,
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
          onCancel: () => { session.close() },
          onCommand: (command) => session.send(command),
          onEdit: dependencies.openEditor ?? openEditor,
          onOpen: dependencies.openFile ?? openFile,
          onOpenResolved: dependencies.openResolved ?? openResolvedFile,
          onCopyText: dependencies.copyText ?? copyText,
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
