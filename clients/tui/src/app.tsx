import { For } from "solid-js"
import { useKeyboard, useRenderer, useTerminalDimensions } from "@opentui/solid"

import type {
  EntryKind,
  PaneId,
  SnapshotPayload,
  WorkspaceSnapshotPayload,
} from "./protocol.js"
import type { CoreSessionCommand } from "./core-client.js"

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
  readonly workspace?: WorkspaceSnapshotPayload
  readonly error?: string
  readonly loading?: boolean
  readonly onCancel?: () => void
  readonly onCommand?: (command: CoreSessionCommand) => void
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

function selectedCount(snapshot: SnapshotPayload): number {
  return snapshot.entries.filter((entry) => entry.selected).length
}

function EntryList(props: { readonly snapshot: SnapshotPayload; readonly compact: boolean }) {
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
            {props.compact ? null : <text width={12} fg={COLORS.muted}>{entry.size_bytes} B</text>}
          </box>
        )}
      </For>
    </scrollbox>
  )
}

function Pane(props: {
  readonly pane: PaneId
  readonly snapshot: SnapshotPayload
  readonly active: boolean
  readonly compact: boolean
}) {
  const label = props.pane.toUpperCase()
  return (
    <box
      flexGrow={1}
      height="100%"
      flexDirection="column"
      border={true}
      borderStyle="single"
      borderColor={props.active ? COLORS.accent : COLORS.border}
      backgroundColor={COLORS.panel}
      paddingX={1}
      title={`${label}${props.active ? " ACTIVE" : ""} ${props.snapshot.cwd_display}`}
      titleColor={props.active ? COLORS.accent : COLORS.directory}
    >
      <EntryList snapshot={props.snapshot} compact={props.compact} />
    </box>
  )
}

function Workspace(props: { readonly workspace: WorkspaceSnapshotPayload; readonly wide: boolean }) {
  return (
    <box flexGrow={1} width="100%" flexDirection="row" gap={1}>
      {props.wide ? (
        <>
          <Pane pane="left" snapshot={props.workspace.left} active={props.workspace.active_pane === "left"} compact={false} />
          <Pane pane="right" snapshot={props.workspace.right} active={props.workspace.active_pane === "right"} compact={false} />
        </>
      ) : (
        props.workspace.active_pane === "left"
          ? <Pane pane="left" snapshot={props.workspace.left} active={true} compact={true} />
          : <Pane pane="right" snapshot={props.workspace.right} active={true} compact={true} />
      )}
    </box>
  )
}

function ErrorPanel(props: { readonly message: string }) {
  return (
    <box flexGrow={1} width="100%" flexDirection="column" justifyContent="center" alignItems="center" border={true} borderStyle="single" borderColor={COLORS.error} backgroundColor={COLORS.panel}>
      <text fg={COLORS.error}>CORE ERROR</text>
      <text fg={COLORS.text} wrapMode="word">{props.message}</text>
    </box>
  )
}

function LoadingPanel() {
  return (
    <box flexGrow={1} width="100%" flexDirection="column" justifyContent="center" alignItems="center" border={true} borderStyle="single" borderColor={COLORS.border} backgroundColor={COLORS.panel}>
      <text fg={COLORS.accent}>CONNECTING TO CORE</text>
      <text fg={COLORS.muted}>Press q to cancel</text>
    </box>
  )
}

export function App(props: AppProps) {
  const renderer = useRenderer()
  const dimensions = useTerminalDimensions()
  const wide = () => dimensions().width >= 80

  useKeyboard((key) => {
    if (key.name === "q" || (key.ctrl && key.name === "c")) {
      props.onCancel?.()
      renderer.destroy()
      return
    }
    if (props.workspace === undefined || props.onCommand === undefined) return
    const active = props.workspace.active_pane
    if (key.name === "tab") props.onCommand({ action: "focus", pane: active === "left" ? "right" : "left" })
    else if (key.name === "up" || key.name === "k") props.onCommand({ action: "move", delta: -1 })
    else if (key.name === "down" || key.name === "j") props.onCommand({ action: "move", delta: 1 })
    else if (key.name === "return" || key.name === "l") props.onCommand({ action: "enter" })
    else if (key.name === "backspace" || key.name === "h") props.onCommand({ action: "parent" })
    else if (key.name === "space") props.onCommand({ action: "toggle-selection" })
    else if (key.name === "r") props.onCommand({ action: "refresh" })
  })

  const visibleEntries = () => {
    if (props.workspace === undefined) return 0
    return props.workspace.left.entry_count + props.workspace.right.entry_count
  }
  const visibleSelections = () => {
    if (props.workspace === undefined) return 0
    return selectedCount(props.workspace.left) + selectedCount(props.workspace.right)
  }

  return (
    <box width="100%" height="100%" flexDirection="column" backgroundColor={COLORS.background} padding={1} gap={1}>
      <box width="100%" height={1} flexDirection="row">
        <text flexGrow={1} fg={COLORS.accent}>NeoVifm</text>
        <text fg={COLORS.muted}>{dimensions().width}x{dimensions().height}</text>
      </box>
      {props.error !== undefined ? <ErrorPanel message={props.error} /> : props.workspace !== undefined ? (
        <Workspace workspace={props.workspace} wide={wide()} />
      ) : props.loading ? <LoadingPanel /> : <ErrorPanel message="Core returned no workspace" />}
      <box width="100%" height={1} flexDirection="row">
        <text flexGrow={1} fg={COLORS.muted}>READ ONLY · Tab pane · r refresh · q quit</text>
        <text fg={COLORS.muted}>{props.workspace?.active_pane.toUpperCase() ?? "LEFT"} {visibleEntries()} entries · {visibleSelections()} selected</text>
      </box>
    </box>
  )
}
