export type OpenPlatform = "darwin" | "linux" | "win32" | "freebsd" | "openbsd" | "sunos" | "aix"

function validateArgument(argument: string, label: string): void {
  if (argument.length === 0 || argument.includes("\0")) {
    throw new Error(`${label} is invalid`)
  }
}

export function validateOpenPath(path: string): void {
  validateArgument(path, "Open path")
}

/**
 * Builds an argv vector for a Vifm-resolved association.
 *
 * The core owns association matching and macro expansion. The client only
 * appends the already-resolved target as one argument and never invokes a
 * shell or interprets the command string.
 */
export function openCommandForAssociation(
  path: string,
  association: readonly string[],
): readonly string[] {
  validateOpenPath(path)
  if (association.length === 0) throw new Error("Open association is empty")
  for (const argument of association) {
    if (argument.length === 0 || argument.includes("\0")) {
      throw new Error("Open association is invalid")
    }
  }
  return [...association, path]
}

/**
 * Builds the platform fallback used when the core has no explicit Vifm
 * association. The absolute macOS path makes the default predictable while
 * keeping every target in a structured argv item.
 */
export function openCommand(path: string, platform: OpenPlatform = process.platform as OpenPlatform): readonly string[] {
  validateOpenPath(path)
  switch (platform) {
    case "darwin": return ["/usr/bin/open", path]
    case "win32": return ["explorer.exe", path]
    case "linux":
    case "freebsd":
    case "openbsd":
    case "sunos":
    case "aix": return ["xdg-open", path]
    default: throw new Error(`System opener is unsupported on ${platform}`)
  }
}

export interface OpenProcess {
  readonly exited: Promise<number>
}

export interface OpenSpawnOptions {
  readonly cmd: string[]
  readonly stdin: "ignore"
  readonly stdout: "ignore"
  readonly stderr: "pipe"
}

export interface OpenFileDependencies {
  readonly spawn?: (options: OpenSpawnOptions) => OpenProcess
  readonly platform?: OpenPlatform
  readonly association?: readonly string[]
}

async function runOpenCommand(command: readonly string[], dependencies: Pick<OpenFileDependencies, "spawn"> = {}): Promise<void> {
  if (command.length === 0) throw new Error("Open command is empty")
  for (const argument of command) validateArgument(argument, "Open command argument")
  const spawn = dependencies.spawn ?? ((options: OpenSpawnOptions) => Bun.spawn(options))
  let process: OpenProcess
  try {
    process = spawn({ cmd: [...command], stdin: "ignore", stdout: "ignore", stderr: "pipe" })
  } catch (error) {
    throw new Error(`Open failed: ${error instanceof Error ? error.message : String(error)}`)
  }
  const exitCode = await process.exited
  if (exitCode !== 0) throw new Error(`Open exited with status ${exitCode}`)
}

/** Starts a GUI opener without suspending or handing a shell an untrusted path. */
export async function openFile(path: string, dependencies: OpenFileDependencies = {}): Promise<void> {
  const command = dependencies.association === undefined
    ? openCommand(path, dependencies.platform)
    : openCommandForAssociation(path, dependencies.association)
  await runOpenCommand(command, dependencies)
}

/** Executes the argv resolved by the C core; the client never interprets it. */
export async function openResolvedFile(
  command: readonly string[],
  dependencies: Pick<OpenFileDependencies, "spawn"> = {},
): Promise<void> {
  await runOpenCommand(command, dependencies)
}
