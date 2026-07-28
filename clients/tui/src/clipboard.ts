export const MAX_CLIPBOARD_TEXT_BYTES = 1024 * 1024

export type ClipboardErrorCode =
  | "invalid-limit"
  | "text-too-large"
  | "unsupported-platform"
  | "provider-unavailable"
  | "provider-lookup-failed"
  | "spawn-failed"
  | "write-failed"
  | "provider-failed"

export class ClipboardError extends Error {
  readonly code: ClipboardErrorCode
  readonly provider?: string
  readonly exitCode?: number

  constructor(
    message: string,
    details: {
      readonly code: ClipboardErrorCode
      readonly provider?: string
      readonly exitCode?: number
      readonly cause?: unknown
    },
  ) {
    super(message, details.cause === undefined ? undefined : { cause: details.cause })
    this.name = "ClipboardError"
    this.code = details.code
    this.provider = details.provider
    this.exitCode = details.exitCode
  }
}

export interface ClipboardStdin {
  readonly write: (text: string) => number | Promise<number>
  readonly end: () => void | number | Promise<void | number>
}

export interface ClipboardProcess {
  readonly stdin: ClipboardStdin
  readonly exited: Promise<number>
}

export interface ClipboardSpawnOptions {
  readonly cmd: readonly string[]
  readonly stdin: "pipe"
  readonly stdout: "ignore"
  readonly stderr: "ignore"
}

export interface ClipboardDependencies {
  readonly platform?: string
  readonly maximumBytes?: number
  readonly which?: (
    executable: string,
  ) => string | null | undefined | Promise<string | null | undefined>
  readonly spawn?: (options: ClipboardSpawnOptions) => ClipboardProcess
}

export interface ClipboardCopyResult {
  readonly provider: string
  readonly byteLength: number
}

interface ClipboardProvider {
  readonly name: string
  readonly args: readonly string[]
}

interface ProviderFailure {
  readonly error: ClipboardError
}

const PROVIDERS: Readonly<Record<string, readonly ClipboardProvider[]>> = Object.freeze({
  darwin: Object.freeze([{ name: "pbcopy", args: Object.freeze([]) }]),
  linux: Object.freeze([
    { name: "wl-copy", args: Object.freeze([]) },
    { name: "xclip", args: Object.freeze(["-selection", "clipboard"]) },
    { name: "xsel", args: Object.freeze(["--clipboard", "--input"]) },
  ]),
  win32: Object.freeze([{ name: "clip.exe", args: Object.freeze([]) }]),
})

function defaultSpawn(options: ClipboardSpawnOptions): ClipboardProcess {
  return Bun.spawn({
    cmd: [...options.cmd],
    stdin: options.stdin,
    stdout: options.stdout,
    stderr: options.stderr,
  })
}

function byteLimit(value: number | undefined): number {
  const limit = value ?? MAX_CLIPBOARD_TEXT_BYTES
  if (!Number.isSafeInteger(limit) || limit <= 0) {
    throw new ClipboardError("Clipboard byte limit must be a positive safe integer", {
      code: "invalid-limit",
    })
  }
  return limit
}

function providerError(
  provider: ClipboardProvider,
  code: "provider-lookup-failed" | "spawn-failed" | "write-failed",
  message: string,
  cause: unknown,
): ProviderFailure {
  return {
    error: new ClipboardError(message, {
      code,
      provider: provider.name,
      cause,
    }),
  }
}

async function closeFailedStdin(process: ClipboardProcess): Promise<void> {
  try {
    await process.stdin.end()
  } catch {
    // The original stdin failure is the actionable error.
  }
  void process.exited.catch(() => undefined)
}

export async function copyTextToClipboard(
  text: string,
  dependencies: ClipboardDependencies = {},
): Promise<ClipboardCopyResult> {
  const limit = byteLimit(dependencies.maximumBytes)
  const byteLength = new TextEncoder().encode(text).byteLength
  if (byteLength > limit) {
    throw new ClipboardError(
      `Clipboard text is ${byteLength} bytes; limit is ${limit} bytes`,
      { code: "text-too-large" },
    )
  }

  const platform = dependencies.platform ?? process.platform
  const providers = PROVIDERS[platform]
  if (providers === undefined) {
    throw new ClipboardError(`Clipboard is unsupported on platform: ${platform}`, {
      code: "unsupported-platform",
    })
  }

  const which = dependencies.which ?? Bun.which
  const spawn = dependencies.spawn ?? defaultSpawn
  let availableProviders = 0
  let lastFailure: ProviderFailure | undefined

  for (const provider of providers) {
    let executable: string | null | undefined
    try {
      executable = await which(provider.name)
    } catch (cause) {
      lastFailure = providerError(
        provider,
        "provider-lookup-failed",
        `Unable to locate clipboard provider: ${provider.name}`,
        cause,
      )
      continue
    }
    if (executable === null || executable === undefined || executable.length === 0) continue
    availableProviders += 1

    let child: ClipboardProcess
    try {
      child = spawn({
        cmd: [executable, ...provider.args],
        stdin: "pipe",
        stdout: "ignore",
        stderr: "ignore",
      })
    } catch (cause) {
      lastFailure = providerError(
        provider,
        "spawn-failed",
        `Unable to start clipboard provider: ${provider.name}`,
        cause,
      )
      continue
    }

    try {
      await child.stdin.write(text)
      await child.stdin.end()
    } catch (cause) {
      await closeFailedStdin(child)
      lastFailure = providerError(
        provider,
        "write-failed",
        `Unable to write to clipboard provider: ${provider.name}`,
        cause,
      )
      continue
    }

    let exitCode: number
    try {
      exitCode = await child.exited
    } catch (cause) {
      lastFailure = {
        error: new ClipboardError(`Clipboard provider failed: ${provider.name}`, {
          code: "provider-failed",
          provider: provider.name,
          cause,
        }),
      }
      continue
    }
    if (exitCode !== 0) {
      lastFailure = {
        error: new ClipboardError(
          `Clipboard provider ${provider.name} exited with status ${exitCode}`,
          {
            code: "provider-failed",
            provider: provider.name,
            exitCode,
          },
        ),
      }
      continue
    }

    return { provider: provider.name, byteLength }
  }

  if (lastFailure !== undefined) throw lastFailure.error
  if (availableProviders === 0) {
    throw new ClipboardError("No supported clipboard provider is installed", {
      code: "provider-unavailable",
    })
  }
  throw new ClipboardError("All available clipboard providers failed", {
    code: "provider-failed",
  })
}
