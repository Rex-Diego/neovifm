import { expect, test } from "bun:test"
import { resolve } from "node:path"

import { MAX_SNAPSHOT_ENTRIES, MAX_WORKSPACE_ENTRIES, parseProtocolRecord } from "../src/protocol.js"

function objectValue(value: unknown): Readonly<Record<string, unknown>> {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    throw new Error("expected schema object")
  }
  return value as Readonly<Record<string, unknown>>
}

const schemaPath = resolve(import.meta.dir, "../../../protocol/neovifm-core-v0.schema.json")
const schema = objectValue(JSON.parse(await Bun.file(schemaPath).text()))
const definitions = objectValue(schema.$defs)
const base = objectValue(definitions.base)
const baseProperties = objectValue(base.properties)
const snapshot = objectValue(definitions.snapshot)
const snapshotParts = snapshot.allOf
if (!Array.isArray(snapshotParts)) {
  throw new Error("expected snapshot allOf")
}
const snapshotVariant = objectValue(snapshotParts[1])
const snapshotProperties = objectValue(snapshotVariant.properties)
const snapshotPayload = objectValue(snapshotProperties.payload)
const payloadProperties = objectValue(snapshotPayload.properties)
const workspaceSchemaPath = resolve(import.meta.dir, "../../../protocol/neovifm-core-v1.schema.json")
const workspaceSchema = objectValue(JSON.parse(await Bun.file(workspaceSchemaPath).text()))
const workspaceDefinitions = objectValue(workspaceSchema.$defs)
const workspaceSnapshot = objectValue(workspaceDefinitions.workspaceSnapshot)
const workspaceParts = workspaceSnapshot.allOf
if (!Array.isArray(workspaceParts)) throw new Error("expected workspace allOf")
const workspaceVariant = objectValue(workspaceParts[1])
const workspaceProperties = objectValue(workspaceVariant.properties)
const workspacePayload = objectValue(workspaceProperties.payload)
const workspacePayloadProperties = objectValue(workspacePayload.properties)
const sessionSchemaPath = resolve(import.meta.dir, "../../../protocol/neovifm-core-v2.schema.json")
const sessionSchema = objectValue(JSON.parse(await Bun.file(sessionSchemaPath).text()))
const sessionDefinitions = objectValue(sessionSchema.$defs)
const sessionWorkspace = objectValue(sessionDefinitions.workspaceSnapshot)
const sessionParts = sessionWorkspace.allOf
if (!Array.isArray(sessionParts)) throw new Error("expected session workspace allOf")
const sessionVariant = objectValue(sessionParts[1])
const sessionProperties = objectValue(sessionVariant.properties)
const sessionPayload = objectValue(sessionProperties.payload)
const sessionPayloadProperties = objectValue(sessionPayload.properties)

test("schema resource bounds match the TypeScript protocol boundary", () => {
  expect(objectValue(baseProperties.sequence).maximum).toBe(Number.MAX_SAFE_INTEGER)
  expect(objectValue(payloadProperties.entry_count).maximum).toBe(MAX_SNAPSHOT_ENTRIES)
  expect(objectValue(payloadProperties.entries).maxItems).toBe(MAX_SNAPSHOT_ENTRIES)
})

test("schema declares the cursor invariant enforced at the runtime boundary", () => {
  expect(snapshot.$comment).toBe(
    "cursor is -1 exactly when entry_count is 0; otherwise 0 <= cursor < entry_count",
  )

  expect(() => parseProtocolRecord({
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
      entries: [{
        name_display: "file",
        name_bytes_hex: "66696c65",
        path_display: "/tmp/file",
        path_bytes_hex: "2f746d702f66696c65",
        kind: "file",
        size_bytes: "0",
        mtime_unix_ms: "0",
        selected: false,
        hidden: false,
      }],
    },
  })).toThrow("payload.cursor")
})

test("v1 schema requires both panes and documents the combined entry bound", () => {
  expect(workspacePayload.required).toEqual(["active_pane", "left", "right"])
  expect(objectValue(workspacePayloadProperties.active_pane).enum).toEqual(["left", "right"])
  expect(workspacePayload.$comment).toContain(String(MAX_WORKSPACE_ENTRIES))
})

test("v2 schema requires an acknowledged command sequence and refresh trigger", () => {
  expect(sessionPayload.required).toEqual(["command_sequence", "trigger", "active_pane", "left", "right"])
  expect(objectValue(sessionPayloadProperties.trigger).enum).toEqual(["initial", "command", "watch"])
  const commandError = objectValue(sessionDefinitions.commandError)
  expect(commandError.allOf).toBeDefined()
})
