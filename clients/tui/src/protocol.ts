export const PROTOCOL_NAME = "neovifm-core" as const
export const PROTOCOL_VERSION = 0 as const

const DECIMAL_PATTERN = /^-?[0-9]+$/
const UNSIGNED_DECIMAL_PATTERN = /^[0-9]+$/
const HEX_PATTERN = /^(?:[0-9a-f]{2})*$/
const OCTAL_PATTERN = /^[0-7]+$/
const ATTRIBUTES_PATTERN = /^[0-9a-f]+$/
const DEFAULT_MAX_RECORD_CHARS = 64 * 1024 * 1024

export type EntryKind =
  | "directory"
  | "file"
  | "symlink"
  | "executable"
  | "fifo"
  | "socket"
  | "char-device"
  | "block-device"
  | "unknown"

export interface StatError {
  readonly code: number
  readonly message: string
}

export interface SnapshotEntry {
  readonly name_display: string
  readonly name_bytes_hex: string
  readonly path_display: string
  readonly path_bytes_hex: string
  readonly kind: EntryKind
  readonly size_bytes: string
  readonly mtime_unix_ms: string
  readonly inode?: string
  readonly mode_octal?: string
  readonly attributes_hex?: string
  readonly selected: boolean
  readonly hidden: boolean
  readonly stat_error?: StatError
}

export interface HelloPayload {
  readonly implementation: string
  readonly capabilities: readonly string[]
}

export interface SnapshotPayload {
  readonly cwd_display: string
  readonly cwd_bytes_hex: string
  readonly generated_at_unix_ms: string
  readonly cursor: number
  readonly entry_count: number
  readonly entries: readonly SnapshotEntry[]
}

export interface ErrorPayload {
  readonly code: string
  readonly message: string
  readonly retryable: boolean
  readonly path_display?: string
  readonly path_bytes_hex?: string
  readonly os_error?: number
}

interface Envelope<Type extends string, Payload> {
  readonly protocol: typeof PROTOCOL_NAME
  readonly version: typeof PROTOCOL_VERSION
  readonly type: Type
  readonly sequence: number
  readonly payload: Payload
}

export type HelloRecord = Envelope<"hello", HelloPayload>
export type SnapshotRecord = Envelope<"snapshot", SnapshotPayload>
export type ErrorRecord = Envelope<"error", ErrorPayload>
export type ProtocolRecord = HelloRecord | SnapshotRecord | ErrorRecord

type UnknownObject = Readonly<Record<string, unknown>>

export class ProtocolValidationError extends Error {
  override readonly name = "ProtocolValidationError"
}

function invalid(path: string, expectation: string): never {
  throw new ProtocolValidationError(`${path} ${expectation}`)
}

function objectValue(value: unknown, path: string): UnknownObject {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    return invalid(path, "must be an object")
  }
  return value as UnknownObject
}

function stringValue(value: unknown, path: string, allowEmpty = true): string {
  if (typeof value !== "string" || (!allowEmpty && value.length === 0)) {
    return invalid(path, allowEmpty ? "must be a string" : "must be a non-empty string")
  }
  return value
}

function patternString(value: unknown, path: string, pattern: RegExp): string {
  const text = stringValue(value, path)
  if (!pattern.test(text)) {
    return invalid(path, "has an invalid format")
  }
  return text
}

function integerValue(value: unknown, path: string, minimum?: number): number {
  if (!Number.isSafeInteger(value) || (minimum !== undefined && (value as number) < minimum)) {
    return invalid(path, `must be a safe integer${minimum === undefined ? "" : ` >= ${minimum}`}`)
  }
  return value as number
}

function booleanValue(value: unknown, path: string): boolean {
  if (typeof value !== "boolean") {
    return invalid(path, "must be a boolean")
  }
  return value
}

function optionalString(
  object: UnknownObject,
  key: string,
  path: string,
  pattern?: RegExp,
): string | undefined {
  const value = object[key]
  if (value === undefined) {
    return undefined
  }
  return pattern === undefined
    ? stringValue(value, `${path}.${key}`)
    : patternString(value, `${path}.${key}`, pattern)
}

function parseStringArray(value: unknown, path: string): readonly string[] {
  if (!Array.isArray(value)) {
    return invalid(path, "must be an array")
  }
  const result = value.map((item, index) => stringValue(item, `${path}[${index}]`, false))
  if (new Set(result).size !== result.length) {
    return invalid(path, "must contain unique values")
  }
  return result
}

const ENTRY_KINDS: ReadonlySet<string> = new Set<EntryKind>([
  "directory",
  "file",
  "symlink",
  "executable",
  "fifo",
  "socket",
  "char-device",
  "block-device",
  "unknown",
])

function parseEntryKind(value: unknown, path: string): EntryKind {
  const kind = stringValue(value, path)
  if (!ENTRY_KINDS.has(kind)) {
    return invalid(path, "is not a supported entry kind")
  }
  return kind as EntryKind
}

function parseStatError(value: unknown, path: string): StatError | undefined {
  if (value === undefined) {
    return undefined
  }
  const object = objectValue(value, path)
  return {
    code: integerValue(object.code, `${path}.code`),
    message: stringValue(object.message, `${path}.message`),
  }
}

function parseEntry(value: unknown, index: number): SnapshotEntry {
  const path = `payload.entries[${index}]`
  const object = objectValue(value, path)
  const inode = optionalString(object, "inode", path, DECIMAL_PATTERN)
  const mode = optionalString(object, "mode_octal", path, OCTAL_PATTERN)
  const attributes = optionalString(object, "attributes_hex", path, ATTRIBUTES_PATTERN)
  const statError = parseStatError(object.stat_error, `${path}.stat_error`)

  return {
    name_display: stringValue(object.name_display, `${path}.name_display`),
    name_bytes_hex: patternString(object.name_bytes_hex, `${path}.name_bytes_hex`, HEX_PATTERN),
    path_display: stringValue(object.path_display, `${path}.path_display`),
    path_bytes_hex: patternString(object.path_bytes_hex, `${path}.path_bytes_hex`, HEX_PATTERN),
    kind: parseEntryKind(object.kind, `${path}.kind`),
    size_bytes: patternString(
      object.size_bytes,
      `${path}.size_bytes`,
      UNSIGNED_DECIMAL_PATTERN,
    ),
    mtime_unix_ms: patternString(object.mtime_unix_ms, `${path}.mtime_unix_ms`, DECIMAL_PATTERN),
    ...(inode === undefined ? {} : { inode }),
    ...(mode === undefined ? {} : { mode_octal: mode }),
    ...(attributes === undefined ? {} : { attributes_hex: attributes }),
    selected: booleanValue(object.selected, `${path}.selected`),
    hidden: booleanValue(object.hidden, `${path}.hidden`),
    ...(statError === undefined ? {} : { stat_error: statError }),
  }
}

function parseHelloPayload(value: unknown): HelloPayload {
  const payload = objectValue(value, "payload")
  return {
    implementation: stringValue(payload.implementation, "payload.implementation", false),
    capabilities: parseStringArray(payload.capabilities, "payload.capabilities"),
  }
}

function parseSnapshotPayload(value: unknown): SnapshotPayload {
  const payload = objectValue(value, "payload")
  if (!Array.isArray(payload.entries)) {
    return invalid("payload.entries", "must be an array")
  }
  const entries = payload.entries.map(parseEntry)
  const entryCount = integerValue(payload.entry_count, "payload.entry_count", 0)
  if (entryCount !== entries.length) {
    return invalid("payload.entry_count", "must match payload.entries.length")
  }

  const cursor = integerValue(payload.cursor, "payload.cursor", -1)
  if (
    (entryCount === 0 && cursor !== -1)
    || (entryCount !== 0 && (cursor < 0 || cursor >= entryCount))
  ) {
    return invalid("payload.cursor", "must identify an entry or be -1 for an empty snapshot")
  }

  return {
    cwd_display: stringValue(payload.cwd_display, "payload.cwd_display"),
    cwd_bytes_hex: patternString(payload.cwd_bytes_hex, "payload.cwd_bytes_hex", HEX_PATTERN),
    generated_at_unix_ms: patternString(
      payload.generated_at_unix_ms,
      "payload.generated_at_unix_ms",
      DECIMAL_PATTERN,
    ),
    cursor,
    entry_count: entryCount,
    entries,
  }
}

function parseErrorPayload(value: unknown): ErrorPayload {
  const payload = objectValue(value, "payload")
  const pathDisplay = optionalString(payload, "path_display", "payload")
  const pathBytes = optionalString(payload, "path_bytes_hex", "payload", HEX_PATTERN)
  const osError = payload.os_error === undefined
    ? undefined
    : integerValue(payload.os_error, "payload.os_error")

  return {
    code: stringValue(payload.code, "payload.code", false),
    message: stringValue(payload.message, "payload.message"),
    retryable: booleanValue(payload.retryable, "payload.retryable"),
    ...(pathDisplay === undefined ? {} : { path_display: pathDisplay }),
    ...(pathBytes === undefined ? {} : { path_bytes_hex: pathBytes }),
    ...(osError === undefined ? {} : { os_error: osError }),
  }
}

export function parseProtocolRecord(value: unknown): ProtocolRecord {
  const envelope = objectValue(value, "record")
  if (envelope.protocol !== PROTOCOL_NAME) {
    return invalid("record.protocol", `must equal ${PROTOCOL_NAME}`)
  }
  if (envelope.version !== PROTOCOL_VERSION) {
    throw new ProtocolValidationError(
      `Unsupported NeoVifm protocol version: ${String(envelope.version)}`,
    )
  }

  const sequence = integerValue(envelope.sequence, "record.sequence", 0)
  const type = stringValue(envelope.type, "record.type")
  switch (type) {
    case "hello":
      return { protocol: PROTOCOL_NAME, version: PROTOCOL_VERSION, type, sequence, payload: parseHelloPayload(envelope.payload) }
    case "snapshot":
      return { protocol: PROTOCOL_NAME, version: PROTOCOL_VERSION, type, sequence, payload: parseSnapshotPayload(envelope.payload) }
    case "error":
      return { protocol: PROTOCOL_NAME, version: PROTOCOL_VERSION, type, sequence, payload: parseErrorPayload(envelope.payload) }
    default:
      return invalid("record.type", `is unsupported: ${type}`)
  }
}

export class JsonlDecoder {
  readonly #decoder = new TextDecoder("utf-8", { fatal: true })
  readonly #maximumRecordChars: number
  #buffer = ""

  constructor(maximumRecordChars = DEFAULT_MAX_RECORD_CHARS) {
    if (!Number.isSafeInteger(maximumRecordChars) || maximumRecordChars <= 0) {
      throw new RangeError("maximumRecordChars must be a positive safe integer")
    }
    this.#maximumRecordChars = maximumRecordChars
  }

  push(chunk: string | Uint8Array): readonly ProtocolRecord[] {
    const text = typeof chunk === "string" ? chunk : this.#decoder.decode(chunk, { stream: true })
    const lines = `${this.#buffer}${text}`.split("\n")
    this.#buffer = lines.pop() ?? ""
    this.#assertRecordSize(this.#buffer)
    return lines.flatMap((line) => this.#parseLine(line))
  }

  flush(): readonly ProtocolRecord[] {
    const tail = this.#decoder.decode()
    this.#buffer = `${this.#buffer}${tail}`
    if (this.#buffer.trim().length !== 0) {
      throw new ProtocolValidationError("Incomplete JSONL record at end of stream")
    }
    this.#buffer = ""
    return []
  }

  #parseLine(rawLine: string): readonly ProtocolRecord[] {
    const line = rawLine.endsWith("\r") ? rawLine.slice(0, -1) : rawLine
    if (line.trim().length === 0) {
      return []
    }
    this.#assertRecordSize(line)
    try {
      return [parseProtocolRecord(JSON.parse(line) as unknown)]
    } catch (error) {
      if (error instanceof ProtocolValidationError) {
        throw error
      }
      throw new ProtocolValidationError(
        `Invalid JSONL record: ${error instanceof Error ? error.message : String(error)}`,
        { cause: error },
      )
    }
  }

  #assertRecordSize(value: string): void {
    if (value.length > this.#maximumRecordChars) {
      throw new ProtocolValidationError(
        `JSONL record exceeds ${this.#maximumRecordChars} characters`,
      )
    }
  }
}
