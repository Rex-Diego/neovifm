import { For } from "solid-js"
import { useKeyboard, useRenderer, useTerminalDimensions } from "@opentui/solid"

import type { EntryKind, SnapshotEntry, SnapshotPayload } from "./protocol.js"

const COLORS = {
  background: "#101418",
  panel: "#171c21",
  border: "#52606d",
  text: "#e7edf3",
  muted: "#9aa7b2",
  accent: "#4fd1a5",
  directory: "#72a7ff",
  executable: "#f3c969",
  error: "#ff6b6b",
} as const

export interface AppProps {
  readonly snapshot?: SnapshotPayload
  readonly error?: string
}

function entryMarker(kind: EntryKind): string {
  switch (kind) {
    case "directory": return "[D]"
    case "executable": return "[X]"
    case "symlink": return "[L]"
    case "fifo": return "[P]"
    case "socket": return "[S]"
    case "char-device": return "[C]"
    case "block-device": return "[B]"
    case "unknown": return "[?]"
    case "file": return "[F]"
  }
}

function entryColor(kind: EntryKind): string {
  if (kind === "directory") return COLORS.directory
  if (kind === "executable") return COLORS.executable
  return COLORS.text
}

function selectedEntry(snapshot: SnapshotPayload): SnapshotEntry | undefined {
  return snapshot.cursor < 0 ? undefined : snapshot.entries[snapshot.cursor]
}

function EntryList(props: { readonly snapshot: SnapshotPayload; readonly wide: boolean }) {
  return (
    <scrollbox flexGrow={1} width="100%" backgroundColor={COLORS.panel}>
      <For each={props.snapshot.entries}>
        {(entry, index) => (
          <box height={1} width="100%" flexDirection="row">
            <text width={2} fg={index() === props.snapshot.cursor ? COLORS.accent : COLORS.muted}>
              {index() === props.snapshot.cursor ? ">" : " "}
            </text>
            <text width={4} fg={entryColor(entry.kind)}>{entryMarker(entry.kind)}</text>
            <text flexGrow={1} fg={entryColor(entry.kind)} truncate>{entry.name_display}</text>
            {props.wide ? <text width={14} fg={COLORS.muted}>{entry.size_bytes} B</text> : null}
          </box>
        )}
      </For>
    </scrollbox>
  )
}

function Details(props: { readonly entry?: SnapshotEntry }) {
  return (
    <box
      width="34%"
      height="100%"
      flexDirection="column"
      border={true}
      borderStyle="single"
      borderColor={COLORS.border}
      backgroundColor={COLORS.panel}
      paddingX={1}
      title="DETAILS"
      titleColor={COLORS.accent}
    >
      {props.entry === undefined ? (
        <text fg={COLORS.muted}>No active entry</text>
      ) : (
        <box flexDirection="column" gap={1}>
          <text fg={COLORS.text} wrapMode="word">{props.entry.name_display}</text>
          <text fg={COLORS.muted}>Kind  {props.entry.kind}</text>
          <text fg={COLORS.muted}>Size  {props.entry.size_bytes} B</text>
          <text fg={COLORS.muted} wrapMode="word">Path  {props.entry.path_display}</text>
        </box>
      )}
    </box>
  )
}

function Workspace(props: { readonly snapshot: SnapshotPayload; readonly wide: boolean }) {
  const activeEntry = () => selectedEntry(props.snapshot)
  return (
    <box flexGrow={1} width="100%" flexDirection={props.wide ? "row" : "column"} gap={1}>
      <box
        flexGrow={1}
        height="100%"
        flexDirection="column"
        border={true}
        borderStyle="single"
        borderColor={COLORS.border}
        backgroundColor={COLORS.panel}
        paddingX={1}
        title={props.snapshot.cwd_display}
        titleColor={COLORS.directory}
      >
        <EntryList snapshot={props.snapshot} wide={props.wide} />
      </box>
      {props.wide ? <Details entry={activeEntry()} /> : null}
    </box>
  )
}

function ErrorPanel(props: { readonly message: string }) {
  return (
    <box
      flexGrow={1}
      width="100%"
      flexDirection="column"
      justifyContent="center"
      alignItems="center"
      border={true}
      borderStyle="single"
      borderColor={COLORS.error}
      backgroundColor={COLORS.panel}
    >
      <text fg={COLORS.error}>CORE ERROR</text>
      <text fg={COLORS.text} wrapMode="word">{props.message}</text>
    </box>
  )
}

export function App(props: AppProps) {
  const renderer = useRenderer()
  const dimensions = useTerminalDimensions()
  const wide = () => dimensions().width >= 80

  useKeyboard((key) => {
    if (key.name === "q" || (key.ctrl && key.name === "c")) {
      renderer.destroy()
    }
  })

  return (
    <box
      width="100%"
      height="100%"
      flexDirection="column"
      backgroundColor={COLORS.background}
      padding={1}
      gap={1}
    >
      <box width="100%" height={1} flexDirection="row">
        <text flexGrow={1} fg={COLORS.accent}>NeoVifm</text>
        <text fg={COLORS.muted}>{dimensions().width}x{dimensions().height}</text>
      </box>
      {props.error !== undefined ? (
        <ErrorPanel message={props.error} />
      ) : props.snapshot !== undefined ? (
        <Workspace snapshot={props.snapshot} wide={wide()} />
      ) : (
        <ErrorPanel message="Core returned no snapshot" />
      )}
      <box width="100%" height={1} flexDirection="row">
        <text flexGrow={1} fg={COLORS.muted}>READ ONLY</text>
        <text fg={COLORS.muted}>{props.snapshot?.entry_count ?? 0} entries</text>
      </box>
    </box>
  )
}
