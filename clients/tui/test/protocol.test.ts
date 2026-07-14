import { describe, expect, test } from "bun:test"

import { JsonlDecoder, parseProtocolRecord } from "../src/protocol.js"

const hello =
  '{"protocol":"neovifm-core","version":0,"type":"hello","sequence":0,"payload":{"implementation":"probe","capabilities":[]}}'

describe("JSONL protocol", () => {
  test("accepts only complete v1 workspaces with two independent panes", () => {
    const pane = {
      cwd_display: "/tmp",
      cwd_bytes_hex: "2f746d70",
      generated_at_unix_ms: "0",
      cursor: -1,
      entry_count: 0,
      entries: [],
    }
    const record = parseProtocolRecord({
      protocol: "neovifm-core",
      version: 1,
      type: "workspace-snapshot",
      sequence: 1,
      payload: { active_pane: "left", left: pane, right: { ...pane, cwd_display: "/var" } },
    })

    expect(record).toMatchObject({
      version: 1,
      type: "workspace-snapshot",
      payload: { active_pane: "left", left: { cwd_display: "/tmp" }, right: { cwd_display: "/var" } },
    })
    expect(Object.isFrozen(record)).toBe(true)
    if (record.type !== "workspace-snapshot") throw new Error("expected workspace")
    expect(Object.isFrozen(record.payload.left)).toBe(true)

    expect(() => parseProtocolRecord({
      protocol: "neovifm-core",
      version: 1,
      type: "workspace-snapshot",
      sequence: 1,
      payload: { active_pane: "center", left: pane, right: pane },
    })).toThrow("payload.active_pane")
  })

  test("requires an explicit v2 snapshot trigger and preserves watch updates", () => {
    const pane = {
      cwd_display: "/tmp", cwd_bytes_hex: "2f746d70", generated_at_unix_ms: "0",
      cursor: -1, entry_count: 0, entries: [],
    }
    const record = parseProtocolRecord({
      protocol: "neovifm-core", version: 2, type: "workspace-snapshot", sequence: 2,
      payload: { command_sequence: 1, trigger: "watch", active_pane: "left", left: pane, right: pane },
    })

    expect(record).toMatchObject({ version: 2, payload: { command_sequence: 1, trigger: "watch" } })
    expect(() => parseProtocolRecord({
      protocol: "neovifm-core", version: 2, type: "workspace-snapshot", sequence: 2,
      payload: { command_sequence: 1, active_pane: "left", left: pane, right: pane },
    })).toThrow("payload.trigger")
  })

  test("decodes records split across arbitrary chunks", () => {
    const decoder = new JsonlDecoder()
    expect(decoder.push('{"protocol":"neovifm-core",')).toEqual([])

    const records = decoder.push(
      '"version":0,"type":"hello","sequence":0,"payload":{"implementation":"probe","capabilities":[]}}\n',
    )

    expect(records).toHaveLength(1)
    expect(records[0]?.type).toBe("hello")
  })

  test("decodes CRLF, multiple records, and split UTF-8 bytes", () => {
    const decoder = new JsonlDecoder()
    const encoded = new TextEncoder().encode(
      `${hello}\r\n${hello.replace("probe", "探针")}\n`,
    )
    const splitAt = encoded.indexOf(0xe6) + 1

    expect(decoder.push(encoded.slice(0, splitAt))).toHaveLength(1)
    const records = decoder.push(encoded.slice(splitAt))

    expect(records).toHaveLength(1)
    expect(records[0]?.type).toBe("hello")
    if (records[0]?.type === "hello") {
      expect(records[0].payload.implementation).toBe("探针")
    }
    expect(decoder.flush()).toEqual([])
  })

  test("rejects unsupported protocol versions", () => {
    expect(() =>
      parseProtocolRecord({
        protocol: "neovifm-core",
        version: 3,
        type: "hello",
        sequence: 0,
        payload: { implementation: "probe", capabilities: [] },
      }),
    ).toThrow("Unsupported NeoVifm protocol version")
  })

  test("flush rejects an incomplete final record", () => {
    const decoder = new JsonlDecoder()
    decoder.push('{"protocol":')
    expect(() => decoder.flush()).toThrow("Incomplete JSONL record")
  })

  test("rejects blank records and trailing whitespace", () => {
    const decoder = new JsonlDecoder()

    expect(() => decoder.push(`${hello}\n \t\r\n`)).toThrow("Empty JSONL record")

    const trailingWhitespaceDecoder = new JsonlDecoder()
    trailingWhitespaceDecoder.push(`${hello}\n `)
    expect(() => trailingWhitespaceDecoder.flush()).toThrow("Incomplete JSONL record")
  })

  test("enforces byte limits before decoding an oversized chunk", () => {
    const decoder = new JsonlDecoder({ maximumRecordBytes: 16, maximumTotalBytes: 17 })

    expect(() => decoder.push(new Uint8Array(17))).toThrow("maximumRecordBytes")
  })

  test("rejects a third record before decoding it", () => {
    const decoder = new JsonlDecoder()

    expect(() => decoder.push(`${hello}\n${hello}\n${hello}\n`)).toThrow("maximumRecords")
  })


  test("validates snapshot payload fields at runtime", () => {
    expect(() =>
      parseProtocolRecord({
        protocol: "neovifm-core",
        version: 0,
        type: "snapshot",
        sequence: 1,
        payload: {
          cwd_display: "/tmp",
          cwd_bytes_hex: "not-hex",
          generated_at_unix_ms: "0",
          cursor: -1,
          entry_count: 0,
          entries: [],
        },
      }),
    ).toThrow("payload.cwd_bytes_hex")
  })

  test("rejects negative sizes and cursors outside the entry array", () => {
    const entry = {
      name_display: "file",
      name_bytes_hex: "66696c65",
      path_display: "/tmp/file",
      path_bytes_hex: "2f746d702f66696c65",
      kind: "file",
      size_bytes: "-1",
      mtime_unix_ms: "0",
      selected: false,
      hidden: false,
    }
    const record = {
      protocol: "neovifm-core",
      version: 0,
      type: "snapshot",
      sequence: 1,
      payload: {
        cwd_display: "/tmp",
        cwd_bytes_hex: "2f746d70",
        generated_at_unix_ms: "0",
        cursor: 1,
        entry_count: 1,
        entries: [entry],
      },
    }

    expect(() => parseProtocolRecord(record)).toThrow("size_bytes")
    entry.size_bytes = "1"
    expect(() => parseProtocolRecord(record)).toThrow("payload.cursor")
  })

  test("bounds snapshot entry counts before constructing renderer DTOs", () => {
    const entry = {
      name_display: "file",
      name_bytes_hex: "66696c65",
      path_display: "/tmp/file",
      path_bytes_hex: "2f746d702f66696c65",
      kind: "file",
      size_bytes: "1",
      mtime_unix_ms: "0",
      selected: false,
      hidden: false,
    }

    expect(() =>
      parseProtocolRecord({
        protocol: "neovifm-core",
        version: 0,
        type: "snapshot",
        sequence: 1,
        payload: {
          cwd_display: "/tmp",
          cwd_bytes_hex: "2f746d70",
          generated_at_unix_ms: "0",
          cursor: 0,
          entry_count: 4097,
          entries: Array.from({ length: 4097 }, () => ({ ...entry })),
        },
      }),
    ).toThrow("payload.entries")
  })

  test("returns a detached validated record and ignores unknown fields", () => {
    const input = {
      protocol: "neovifm-core",
      version: 0,
      type: "hello",
      sequence: 0,
      payload: {
        implementation: "probe",
        capabilities: ["snapshot-v0"],
        future_payload_field: true,
      },
      future_envelope_field: true,
    }

    const record = parseProtocolRecord(input)
    input.payload.capabilities[0] = "changed"

    expect(record).toEqual({
      protocol: "neovifm-core",
      version: 0,
      type: "hello",
      sequence: 0,
      payload: {
        implementation: "probe",
        capabilities: ["snapshot-v0"],
      },
    })
    expect(Object.isFrozen(record)).toBe(true)
    expect(Object.isFrozen(record.payload)).toBe(true)
    if (record.type !== "hello") {
      throw new Error("expected a hello record")
    }
    expect(Object.isFrozen(record.payload.capabilities)).toBe(true)
  })

  test("sanitizes unsafe display text while preserving protocol identity fields", () => {
    const record = parseProtocolRecord({
      protocol: "neovifm-core",
      version: 0,
      type: "error",
      sequence: 1,
      payload: {
        code: "scan-failed",
        message: "unsafe\u001bmessage\u202e",
        retryable: false,
        path_display: "/tmp/unsafe\u001bname",
        path_bytes_hex: "2f746d702f756e736166651b6e616d65",
      },
    })

    expect(record).toMatchObject({
      payload: {
        message: "unsafe�message�",
        path_display: "/tmp/unsafe�name",
        path_bytes_hex: "2f746d702f756e736166651b6e616d65",
      },
    })
  })

  test("rejects display text whose safety replacements exceed the byte budget", () => {
    const expandedBySanitization = "\u001b".repeat(16 * 1024)

    expect(() => parseProtocolRecord({
      protocol: "neovifm-core",
      version: 0,
      type: "error",
      sequence: 1,
      payload: {
        code: "scan-failed",
        message: expandedBySanitization,
        retryable: false,
      },
    })).toThrow("payload.message must not exceed 16384 UTF-8 bytes after sanitization")
  })
})
