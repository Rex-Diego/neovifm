import { createSignal, For } from "solid-js"
import { useKeyboard, useRenderer, useTerminalDimensions } from "@opentui/solid"

import type {
  PaneId,
  PreviewPayload,
  PreviewTaskPayload,
  SnapshotPayload,
  WorkspaceSnapshotPayload,
} from "./protocol.js"
import type { CoreSessionCommand } from "./core-client.js"
import { VifmKeymap } from "./keymap.js"
import { extensionGroup, formatFileSize, formatMode, formatMtime, iconForEntry } from "./file-style.js"

const COLORS = {
  background: "#101418",
  panel: "#171c21",
  border: "#52606d",
  text: "#e7edf3",
  muted: "#9aa7b2",
  accent: "#4fd1a5",
  directory: "#72a7ff",
  executable: "#f3c969",
  symlink: "#56d4dd",
  archive: "#ff7b72",
  code: "#7ee787",
  document: "#e3b341",
  image: "#d2a8ff",
  media: "#ffa657",
  selected: "#243447",
  segmentMode: "#7c3aed",
  segmentPane: "#0891b2",
  segmentPath: "#334155",
  segmentInfo: "#166534",
  functionKey: "#1f6feb",
  disabled: "#30363d",
  error: "#ff6b6b",
} as const

export interface AppProps {
  readonly workspace?: WorkspaceSnapshotPayload
  readonly error?: string
  readonly loading?: boolean
  readonly preview?: PreviewPayload
  readonly tasks?: readonly PreviewTaskPayload[]
  readonly onCancel?: () => void
  readonly onCommand?: (command: CoreSessionCommand) => void
}

function entryColor(entry: SnapshotPayload["entries"][number]): string {
  const kind = entry.kind
  if (kind === "directory") return COLORS.directory
  if (kind === "executable") return COLORS.executable
  if (kind === "symlink") return COLORS.symlink
  const group = extensionGroup(entry)
  if (group === "archive") return COLORS.archive
  if (group === "code") return COLORS.code
  if (group === "document") return COLORS.document
  if (group === "image") return COLORS.image
  if (group === "media") return COLORS.media
  return COLORS.text
}

function EntryList(props: { readonly snapshot: SnapshotPayload; readonly detailed: boolean }) {
  return (
    <scrollbox flexGrow={1} width="100%" backgroundColor={COLORS.panel}>
      <For each={props.snapshot.entries}>
        {(entry, index) => (
          <box height={1} width="100%" flexDirection="row" backgroundColor={entry.selected ? COLORS.selected : COLORS.panel}>
            <text width={2} fg={index() === props.snapshot.cursor ? COLORS.accent : COLORS.muted}>
              {index() === props.snapshot.cursor ? ">" : " "}
            </text>
            <text width={2} fg={entry.selected ? COLORS.accent : COLORS.muted}>{entry.selected ? "*" : " "}</text>
            {props.detailed ? <text width={11} fg={COLORS.muted}>{formatMode(entry.mode_octal, entry.kind)}</text> : null}
            <text width={2} fg={entryColor(entry)}>{iconForEntry(entry.kind)}</text>
            <text flexGrow={1} fg={entry.hidden ? COLORS.muted : entryColor(entry)} truncate>{entry.name_display}</text>
            <text width={9} fg={COLORS.muted}>{formatFileSize(entry.size_bytes)}</text>
            {props.detailed ? <text width={17} fg={COLORS.muted}>{formatMtime(entry.mtime_unix_ms)}</text> : null}
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
  readonly detailed: boolean
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
      <EntryList snapshot={props.snapshot} detailed={props.detailed} />
    </box>
  )
}

function Workspace(props: { readonly workspace: WorkspaceSnapshotPayload; readonly wide: boolean; readonly detailed: boolean }) {
  return (
    <box flexGrow={1} width="100%" flexDirection="row" gap={1}>
      {props.wide ? (
        <>
          <Pane pane="left" snapshot={props.workspace.left} active={props.workspace.active_pane === "left"} detailed={props.detailed} />
          <Pane pane="right" snapshot={props.workspace.right} active={props.workspace.active_pane === "right"} detailed={props.detailed} />
        </>
      ) : (
        props.workspace.active_pane === "left"
          ? <Pane pane="left" snapshot={props.workspace.left} active={true} detailed={false} />
          : <Pane pane="right" snapshot={props.workspace.right} active={true} detailed={false} />
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
      <text fg={COLORS.muted}>Press F10, ZZ, or ZQ to cancel</text>
    </box>
  )
}

function Viewer(props: { readonly preview: PreviewPayload }) {
  return <box flexGrow={1} width="100%" flexDirection="column" border={true} borderStyle="double" borderColor={COLORS.accent} backgroundColor={COLORS.panel} paddingX={1} title="F3 VIEW" titleColor={COLORS.accent}>
    <text height={1} fg={COLORS.muted}>{props.preview.kind} · {props.preview.state} · generation {props.preview.generation}</text>
    <scrollbox flexGrow={1} width="100%" backgroundColor={COLORS.panel}>
      <text fg={COLORS.text} wrapMode="word">{props.preview.content ?? props.preview.error_code ?? "Preview unavailable"}</text>
    </scrollbox>
    <text height={1} fg={COLORS.muted}>F3 or Esc to close</text>
  </box>
}

function StatusSegment(props: { readonly text: string; readonly background: string; readonly foreground?: string }) {
  return <text bg={props.background} fg={props.foreground ?? COLORS.text}> {props.text} </text>
}

function FunctionKey(props: { readonly keyName: string; readonly label: string; readonly enabled?: boolean }) {
  return <box flexGrow={1} height={1} flexDirection="row">
    <text bg={props.enabled ? COLORS.functionKey : COLORS.disabled} fg={COLORS.text}> {props.keyName} </text>
    <text flexGrow={1} bg={COLORS.panel} fg={props.enabled ? COLORS.text : COLORS.muted}> {props.label} </text>
  </box>
}

function BottomBars(props: { readonly workspace?: WorkspaceSnapshotPayload; readonly tasks?: readonly PreviewTaskPayload[]; readonly compact: boolean; readonly notice?: string; readonly canView: boolean }) {
  const active = () => props.workspace?.active_pane ?? "left"
  const snapshot = () => active() === "left" ? props.workspace?.left : props.workspace?.right
  const latestTask = () => props.tasks?.at(-1)
  return <box width="100%" height={2} flexDirection="column">
    <box width="100%" height={1} flexDirection="row">
      <StatusSegment text="NORMAL" background={COLORS.segmentMode} />
      <text fg={COLORS.muted}>|</text>
      <StatusSegment text={active().toUpperCase()} background={COLORS.segmentPane} />
      <text fg={COLORS.muted}>|</text>
      <text flexGrow={1} bg={COLORS.segmentPath} fg={COLORS.text} truncate> {snapshot()?.cwd_display ?? "connecting"} </text>
      <text fg={COLORS.muted}>|</text>
      <StatusSegment text={`${snapshot()?.entry_count ?? 0} items`} background={COLORS.segmentInfo} />
      {props.compact ? null : <StatusSegment text={`${snapshot()?.selection_count ?? 0} selected`} background={COLORS.segmentInfo} />}
      {props.notice !== undefined ? <StatusSegment text={props.notice} background={COLORS.segmentPath} /> : latestTask() === undefined ? null : <StatusSegment text={`task:${latestTask()!.state}`} background={COLORS.segmentPath} />}
    </box>
    <box width="100%" height={1} flexDirection="row">
      <FunctionKey keyName="F3" label="View" enabled={props.canView} />
      <FunctionKey keyName="F4" label="Edit" />
      {props.compact ? null : <><FunctionKey keyName="F5" label="Copy" /><FunctionKey keyName="F6" label="Move" /><FunctionKey keyName="F7" label="MkDir" /><FunctionKey keyName="F8" label="Delete" /></>}
      <FunctionKey keyName="F10" label="Quit" enabled />
    </box>
  </box>
}

export function App(props: AppProps) {
  const renderer = useRenderer()
  const dimensions = useTerminalDimensions()
  const wide = () => dimensions().width >= 80
  const detailed = () => dimensions().width >= 120
  const keymap = new VifmKeymap()
  const [viewerOpen, setViewerOpen] = createSignal(false)
  const [notice, setNotice] = createSignal<string | undefined>()

  useKeyboard((key) => {
    if (viewerOpen() && key.name === "escape") {
      key.preventDefault()
      key.stopPropagation()
      setViewerOpen(false)
      return
    }
    const result = keymap.handle(key)
    if (result.kind === "unhandled") return
    key.preventDefault()
    key.stopPropagation()
    if (result.kind === "pending") return
    if (result.kind === "cancel") {
      props.onCancel?.()
      renderer.destroy()
      return
    }
    if (result.kind === "function") {
      if (result.action === "view") {
        if (props.preview?.content !== undefined) {
          setViewerOpen((open) => !open)
          setNotice(undefined)
        } else {
          setNotice("View unavailable")
        }
      } else {
        setNotice(`${result.action} disabled: action service not connected`)
      }
      return
    }
    props.onCommand?.(result.command)
  })

  return (
    <box width="100%" height="100%" flexDirection="column" backgroundColor={COLORS.background} padding={1} gap={1}>
      <box width="100%" height={1} flexDirection="row">
        <text flexGrow={1} fg={COLORS.accent}>NeoVifm</text>
        <text fg={COLORS.muted}>{dimensions().width}x{dimensions().height}</text>
      </box>
      {props.error !== undefined ? <ErrorPanel message={props.error} /> : viewerOpen() && props.preview !== undefined ? <Viewer preview={props.preview} /> : props.workspace !== undefined ? (
        <Workspace workspace={props.workspace} wide={wide()} detailed={detailed()} />
      ) : props.loading ? <LoadingPanel /> : <ErrorPanel message="Core returned no workspace" />}
      <BottomBars workspace={props.workspace} tasks={props.tasks} compact={dimensions().width < 90} notice={notice()} canView={props.preview?.content !== undefined} />
    </box>
  )
}
