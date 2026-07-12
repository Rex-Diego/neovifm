import { describe, expect, test } from "bun:test"

import { JsonlDecoder, parseProtocolRecord } from "../src/protocol.js"

const hello =
  '{"protocol":"neovifm-core","version":0,"type":"hello","sequence":0,"payload":{"implementation":"probe","capabilities":[]}}'

describe("JSONL protocol", () => {
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
        version: 1,
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
  })
})
