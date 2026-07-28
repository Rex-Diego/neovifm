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

  test("validates immutable pane selection, filter, and sort metadata", () => {
    const pane = {
      cwd_display: "/tmp", cwd_bytes_hex: "2f746d70", generated_at_unix_ms: "0",
      cursor: -1, entry_count: 0, selection_count: 0, filtered_count: 2,
      sort_key: "ctime", sort_descending: true, filter_active: true, entries: [],
    }
    const record = parseProtocolRecord({
      protocol: "neovifm-core", version: 0, type: "snapshot", sequence: 1, payload: pane,
    })
    if (record.type !== "snapshot") throw new Error("expected snapshot")
    expect(record.payload).toMatchObject({ filtered_count: 2, sort_key: "ctime", sort_descending: true, filter_active: true })
    expect(() => parseProtocolRecord({
      protocol: "neovifm-core", version: 0, type: "snapshot", sequence: 1,
      payload: { ...pane, selection_count: 1 },
    })).toThrow("selection_count")
  })

  test("accepts additive owner and group display fields", () => {
    const record = parseProtocolRecord({
      protocol: "neovifm-core", version: 0, type: "snapshot", sequence: 1,
      payload: {
        cwd_display: "/tmp", cwd_bytes_hex: "2f746d70", generated_at_unix_ms: "0",
        cursor: 0, entry_count: 1, entries: [{
          name_display: "note", name_bytes_hex: "6e6f7465", path_display: "/tmp/note", path_bytes_hex: "2f746d702f6e6f7465",
          kind: "file", size_bytes: "1", mtime_unix_ms: "0", owner_display: "rex", group_display: "staff",
          selected: false, hidden: false,
        }],
      },
    })
    if (record.type !== "snapshot") throw new Error("expected snapshot")
    expect(record.payload.entries[0]).toMatchObject({ owner_display: "rex", group_display: "staff" })
    expect(Object.isFrozen(record.payload.entries[0])).toBe(true)

    expect(() => parseProtocolRecord({
      protocol: "neovifm-core", version: 0, type: "snapshot", sequence: 1,
      payload: {
        cwd_display: "/tmp", cwd_bytes_hex: "2f746d70", generated_at_unix_ms: "0",
        cursor: 0, entry_count: 1, entries: [{
          name_display: "note", name_bytes_hex: "6e6f7465", path_display: "/tmp/note", path_bytes_hex: "2f746d702f6e6f7465",
          kind: "file", size_bytes: "1", mtime_unix_ms: "0", owner_display: "x".repeat(257),
          selected: false, hidden: false,
        }],
      },
    })).toThrow("owner_display")
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

  test("accepts bounded per-pane tabs and falls back for older snapshots", () => {
    const pane = {
      cwd_display: "/tmp", cwd_bytes_hex: "2f746d70", generated_at_unix_ms: "0",
      cursor: -1, entry_count: 0, entries: [],
    }
    const withTabs = parseProtocolRecord({
      protocol: "neovifm-core", version: 3, type: "workspace-snapshot", sequence: 2,
      payload: {
        command_sequence: 1, trigger: "command", active_pane: "left", left: pane, right: pane,
        left_tabs: [
          { id: "1", cwd_display: "/tmp", active: true },
          { id: "2", cwd_display: "/var", active: false },
        ],
        right_tabs: [{ id: "3", cwd_display: "/tmp", active: true }],
      },
    })
    if (withTabs.type !== "workspace-snapshot") throw new Error("expected workspace")
    expect(withTabs.payload.left_tabs).toEqual([
      { id: "1", cwd_display: "/tmp", active: true },
      { id: "2", cwd_display: "/var", active: false },
    ])
    expect(Object.isFrozen(withTabs.payload.left_tabs)).toBe(true)

    const legacy = parseProtocolRecord({
      protocol: "neovifm-core", version: 2, type: "workspace-snapshot", sequence: 2,
      payload: { command_sequence: 1, trigger: "command", active_pane: "left", left: pane, right: pane },
    })
    if (legacy.type !== "workspace-snapshot") throw new Error("expected workspace")
    expect(legacy.payload.left_tabs).toEqual([{ id: "0", cwd_display: "/tmp", active: true }])
    expect(legacy.payload.right_tabs).toEqual([{ id: "0", cwd_display: "/tmp", active: true }])

    expect(() => parseProtocolRecord({
      protocol: "neovifm-core", version: 3, type: "workspace-snapshot", sequence: 2,
      payload: {
        command_sequence: 1, trigger: "command", active_pane: "left", left: pane, right: pane,
        left_tabs: [{ id: "0", cwd_display: "/tmp", active: true }],
        right_tabs: [{ id: "3", cwd_display: "/tmp", active: true }],
      },
    })).toThrow("left_tabs")

    expect(() => parseProtocolRecord({
      protocol: "neovifm-core", version: 3, type: "workspace-snapshot", sequence: 2,
      payload: {
        command_sequence: 1, trigger: "command", active_pane: "left", left: pane, right: pane,
        left_tabs: [
          { id: "1", cwd_display: "/tmp", active: true },
          { id: "1", cwd_display: "/var", active: false },
        ],
        right_tabs: [{ id: "3", cwd_display: "/tmp", active: true }],
      },
    })).toThrow("left_tabs")
  })

  test("accepts bounded v3 task lifecycle and immutable preview records", () => {
    const task = parseProtocolRecord({
      protocol: "neovifm-core", version: 3, type: "task", sequence: 2,
      payload: {
        task_id: "42", generation: "7", pane: "left", target_pane: "right", kind: "text", state: "running",
        cwd_bytes_hex: "2f746d70", path_bytes_hex: "2f746d702f6e6f7465",
      },
    })
    const preview = parseProtocolRecord({
      protocol: "neovifm-core", version: 3, type: "preview", sequence: 3,
      payload: {
        task_id: "42", generation: "7", pane: "left", target_pane: "right", kind: "text", state: "done",
        cwd_bytes_hex: "2f746d70", path_bytes_hex: "2f746d702f6e6f7465",
        content: "note", truncated: false,
      },
    })
    const markdown = parseProtocolRecord({
      protocol: "neovifm-core", version: 3, type: "preview", sequence: 5,
      payload: {
        task_id: "43", generation: "8", pane: "left", target_pane: "left", kind: "markdown", state: "done",
        cwd_bytes_hex: "2f746d70", path_bytes_hex: "2f746d702f726561646d652e6d64",
        content: "# heading", truncated: false,
      },
    })
    const pdf = parseProtocolRecord({
      protocol: "neovifm-core", version: 3, type: "preview", sequence: 6,
      payload: {
        task_id: "44", generation: "9", pane: "left", target_pane: "left", kind: "pdf", state: "done",
        cwd_bytes_hex: "2f746d70", path_bytes_hex: "2f746d702f6e6f74652e706466",
        content: "PDF page", truncated: false,
      },
    })
    const actionTask = parseProtocolRecord({
      protocol: "neovifm-core", version: 3, type: "action-task", sequence: 4,
      payload: {
        task_id: "9", command_sequence: 3, pane: "left", action: "copy",
        state: "failed", completed_count: 1, total_count: 2, failed_index: 1,
        partial: true, retryable: true, error_code: "destination-exists", os_error: 17,
      },
    })

    expect(task).toMatchObject({ version: 3, type: "task", payload: { generation: "7", target_pane: "right", state: "running" } })
    expect(preview).toMatchObject({ version: 3, type: "preview", payload: { content: "note", target_pane: "right", truncated: false } })
    expect(markdown).toMatchObject({ type: "preview", payload: { kind: "markdown", content: "# heading" } })
    expect(pdf).toMatchObject({ type: "preview", payload: { kind: "pdf", content: "PDF page" } })
    expect(Object.isFrozen(preview)).toBe(true)
    expect(actionTask).toMatchObject({ type: "action-task", payload: { action: "copy", partial: true, retryable: true } })
    expect(() => parseProtocolRecord({
      protocol: "neovifm-core", version: 3, type: "preview", sequence: 3,
      payload: { task_id: "42", generation: "7", pane: "left", kind: "text", state: "queued", cwd_bytes_hex: "2f", path_bytes_hex: "2f", content: "", truncated: false },
    })).toThrow("terminal")
  })

  test("accepts a v3 core-owned open resolution with structured argv", () => {
    const record = parseProtocolRecord({
      protocol: "neovifm-core", version: 3, type: "open", sequence: 5,
      payload: {
        command_sequence: 4, intent: "open", source: "association", state: "resolved",
        path_bytes_hex: "2f746d702f6e6f74652e6d64", argv: ["viewer", "--wait", "/tmp/note.md"],
      },
    })
    expect(record).toMatchObject({
      version: 3,
      type: "open",
      payload: { command_sequence: 4, intent: "open", source: "association", state: "resolved" },
    })
    if (record.type !== "open") throw new Error("expected open result")
    expect(record.payload.argv).toEqual(["viewer", "--wait", "/tmp/note.md"])
    expect(Object.isFrozen(record.payload)).toBe(true)
    expect(() => parseProtocolRecord({
      protocol: "neovifm-core", version: 3, type: "open", sequence: 5,
      payload: { ...record.payload, argv: [] },
    })).toThrow("payload.argv")
    expect(() => parseProtocolRecord({
      protocol: "neovifm-core", version: 3, type: "open", sequence: 5,
      payload: { ...record.payload, path_bytes_hex: "" },
    })).toThrow("payload.path_bytes_hex")
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
        version: 4,
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
