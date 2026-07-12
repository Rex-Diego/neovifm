import {
  JsonlDecoder,
  ProtocolValidationError,
  type ErrorRecord,
  type HelloPayload,
  type ProtocolRecord,
  type SnapshotPayload,
} from "./protocol.js"

const DEFAULT_STDERR_LIMIT = 64 * 1024
const DEFAULT_TIMEOUT_MS = 30_000
const MAX_PROTOCOL_RECORDS = 2

export type CoreClientErrorKind = "spawn" | "protocol" | "exit" | "core" | "timeout"

interface CoreClientErrorDetails {
  readonly kind: CoreClientErrorKind
  readonly cause?: unknown
  readonly exitCode?: number
  readonly coreCode?: string
  readonly stderr?: string
  readonly stderrTruncated?: boolean
}

export class CoreClientError extends Error {
  override readonly name = "CoreClientError"
  readonly kind: CoreClientErrorKind
  readonly exitCode?: number
  readonly coreCode?: string
  readonly stderr: string
  readonly stderrTruncated: boolean

  constructor(message: string, details: CoreClientErrorDetails) {
    super(message, details.cause === undefined ? undefined : { cause: details.cause })
    this.kind = details.kind
    this.exitCode = details.exitCode
    this.coreCode = details.coreCode
    this.stderr = details.stderr ?? ""
    this.stderrTruncated = details.stderrTruncated ?? false
  }
}

export interface CoreProbeRequest {
  readonly executable: string
  readonly targetPath: string
  readonly cwd?: string
  readonly maximumRecordChars?: number
  readonly maximumStderrChars?: number
  readonly timeoutMs?: number
}

export interface CoreProbeResult {
  readonly hello: HelloPayload
  readonly snapshot: SnapshotPayload
  readonly stderr: string
  readonly stderrTruncated: boolean
}

interface ProtocolConsumption {
  readonly records: readonly ProtocolRecord[]
  readonly error?: unknown
}

interface DiagnosticConsumption {
  readonly text: string
  readonly truncated: boolean
}

function validateRequest(request: CoreProbeRequest): void {
  if (request.executable.length === 0 || request.executable.includes("\0")) {
    throw new CoreClientError("Core probe executable is invalid", { kind: "spawn" })
  }
  if (request.targetPath.length === 0 || request.targetPath.includes("\0")) {
    throw new CoreClientError("Target path is invalid", { kind: "spawn" })
  }
  if (request.cwd?.includes("\0")) {
    throw new CoreClientError("Core probe working directory is invalid", { kind: "spawn" })
  }
  if (
    request.maximumRecordChars !== undefined
    && (!Number.isSafeInteger(request.maximumRecordChars) || request.maximumRecordChars <= 0)
  ) {
    throw new RangeError("maximumRecordChars must be a positive safe integer")
  }
  if (
    request.timeoutMs !== undefined
    && (!Number.isSafeInteger(request.timeoutMs) || request.timeoutMs <= 0)
  ) {
    throw new RangeError("timeoutMs must be a positive safe integer")
  }
}

async function consumeProtocolStream(
  stream: ReadableStream<Uint8Array>,
  maximumRecordChars?: number,
): Promise<ProtocolConsumption> {
  const decoder = new JsonlDecoder(maximumRecordChars)
  const reader = stream.getReader()
  let records: readonly ProtocolRecord[] = []
  let failure: unknown

  try {
    for (;;) {
      const result = await reader.read()
      if (result.done) {
        break
      }
      if (failure === undefined) {
        try {
          const decoded = decoder.push(result.value)
          if (records.length + decoded.length > MAX_PROTOCOL_RECORDS) {
            failure = new ProtocolValidationError(
              `Protocol stream exceeds ${MAX_PROTOCOL_RECORDS} records`,
            )
          } else {
            records = [...records, ...decoded]
          }
        } catch (error) {
          failure = error
        }
      }
    }
    if (failure === undefined) {
      records = [...records, ...decoder.flush()]
    }
  } catch (error) {
    failure = failure ?? error
  } finally {
    reader.releaseLock()
  }

  return failure === undefined ? { records } : { records, error: failure }
}

function appendDiagnostic(
  current: DiagnosticConsumption,
  value: string,
  maximumChars: number,
): DiagnosticConsumption {
  const remaining = Math.max(0, maximumChars - current.text.length)
  return {
    text: `${current.text}${value.slice(0, remaining)}`,
    truncated: current.truncated || value.length > remaining,
  }
}

async function consumeDiagnosticStream(
  stream: ReadableStream<Uint8Array>,
  maximumChars: number,
): Promise<DiagnosticConsumption> {
  const decoder = new TextDecoder("utf-8", { fatal: false })
  const reader = stream.getReader()
  let result: DiagnosticConsumption = { text: "", truncated: false }

  try {
    for (;;) {
      const chunk = await reader.read()
      if (chunk.done) {
        break
      }
      result = appendDiagnostic(result, decoder.decode(chunk.value, { stream: true }), maximumChars)
    }
    return appendDiagnostic(result, decoder.decode(), maximumChars)
  } finally {
    reader.releaseLock()
  }
}

function protocolFailure(
  message: string,
  cause: unknown,
  exitCode: number,
  diagnostic: DiagnosticConsumption,
): CoreClientError {
  return new CoreClientError(message, {
    kind: "protocol",
    cause,
    exitCode,
    stderr: diagnostic.text,
    stderrTruncated: diagnostic.truncated,
  })
}

function coreFailure(
  record: ErrorRecord,
  exitCode: number,
  diagnostic: DiagnosticConsumption,
): CoreClientError {
  return new CoreClientError(`Core probe failed: ${record.payload.message}`, {
    kind: "core",
    exitCode,
    coreCode: record.payload.code,
    stderr: diagnostic.text,
    stderrTruncated: diagnostic.truncated,
  })
}

function interpretResult(
  protocol: ProtocolConsumption,
  diagnostic: DiagnosticConsumption,
  exitCode: number,
): CoreProbeResult {
  if (protocol.error !== undefined) {
    throw protocolFailure("Core probe emitted an invalid protocol stream", protocol.error, exitCode, diagnostic)
  }
  const [hello, terminal, ...extra] = protocol.records
  if (hello?.type !== "hello" || terminal === undefined || extra.length !== 0) {
    throw protocolFailure("Core probe must emit hello followed by one terminal record", undefined, exitCode, diagnostic)
  }
  if (!hello.payload.capabilities.includes("snapshot-v0")) {
    throw protocolFailure("Core probe does not advertise snapshot-v0", undefined, exitCode, diagnostic)
  }
  if (terminal.sequence <= hello.sequence) {
    throw protocolFailure("Core probe record sequence is not increasing", undefined, exitCode, diagnostic)
  }
  if (terminal.type === "error") {
    throw coreFailure(terminal, exitCode, diagnostic)
  }
  if (terminal.type !== "snapshot") {
    throw protocolFailure("Core probe terminal record must be snapshot or error", undefined, exitCode, diagnostic)
  }
  if (exitCode !== 0) {
    throw new CoreClientError(`Core probe exited with status ${exitCode}`, {
      kind: "exit",
      exitCode,
      stderr: diagnostic.text,
      stderrTruncated: diagnostic.truncated,
    })
  }
  return {
    hello: hello.payload,
    snapshot: terminal.payload,
    stderr: diagnostic.text,
    stderrTruncated: diagnostic.truncated,
  }
}

export async function runCoreProbe(request: CoreProbeRequest): Promise<CoreProbeResult> {
  validateRequest(request)
  const stderrLimit = request.maximumStderrChars ?? DEFAULT_STDERR_LIMIT
  const timeoutMs = request.timeoutMs ?? DEFAULT_TIMEOUT_MS
  const timeoutSignal = AbortSignal.timeout(timeoutMs)
  if (!Number.isSafeInteger(stderrLimit) || stderrLimit < 0) {
    throw new RangeError("maximumStderrChars must be a non-negative safe integer")
  }

  let process: Bun.Subprocess<"ignore", "pipe", "pipe">
  try {
    process = Bun.spawn({
      cmd: [request.executable, request.targetPath],
      stdin: "ignore",
      stdout: "pipe",
      stderr: "pipe",
      signal: timeoutSignal,
      killSignal: "SIGKILL",
      ...(request.cwd === undefined ? {} : { cwd: request.cwd }),
    })
  } catch (cause) {
    throw new CoreClientError(`Unable to start core probe: ${request.executable}`, {
      kind: "spawn",
      cause,
    })
  }

  try {
    const [protocol, diagnostic, exitCode] = await Promise.all([
      consumeProtocolStream(process.stdout, request.maximumRecordChars),
      consumeDiagnosticStream(process.stderr, stderrLimit),
      process.exited,
    ])
    if (timeoutSignal.aborted) {
      throw new CoreClientError(`Core probe exceeded ${timeoutMs} ms`, {
        kind: "timeout",
        exitCode,
        stderr: diagnostic.text,
        stderrTruncated: diagnostic.truncated,
      })
    }
    return interpretResult(protocol, diagnostic, exitCode)
  } catch (cause) {
    if (cause instanceof CoreClientError) {
      throw cause
    }
    throw new CoreClientError("Core probe process failed", { kind: "exit", cause })
  }
}
