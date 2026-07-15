import { createEffect, createSignal, For, onCleanup, Show } from "solid-js"
import { useKeyboard, useRenderer, useTerminalDimensions } from "@opentui/solid"
import type { ScrollBoxRenderable } from "@opentui/core"

import type {
  PaneId,
  PaneSortKey,
  ActionTaskPayload,
  PreviewPayload,
  PreviewTaskPayload,
  SnapshotPayload,
  WorkspaceSnapshotPayload,
} from "./protocol.js"
import type { CoreActionTarget, CoreSessionCommand } from "./core-client.js"
import { VifmKeymap, type FunctionAction } from "./keymap.js"
import {
  extensionGroup,
  formatFileSize,
  formatMode,
  formatMtime,
  iconForEntry,
  type IconMode,
} from "./file-style.js"

const COLORS = {
  crust: "#11111b",
  base: "#1e1e2e",
  mantle: "#181825",
  surface0: "#313244",
  surface1: "#45475a",
  text: "#cdd6f4",
  subtext0: "#a6adc8",
  overlay1: "#7f849c",
  red: "#f38ba8",
  peach: "#fab387",
  yellow: "#f9e2af",
  green: "#a6e3a1",
  sapphire: "#74c7ec",
  lavender: "#b4befe",
  mauve: "#cba6f7",
  teal: "#94e2d5",
  blue: "#89b4fa",
  selected: "#363a4f",
} as const

export interface AppProps {
  readonly workspace?: WorkspaceSnapshotPayload
  readonly error?: string
  readonly loading?: boolean
  readonly preview?: PreviewPayload
  readonly tasks?: readonly PreviewTaskPayload[]
  readonly actionTasks?: readonly ActionTaskPayload[]
  readonly commandError?: string
  readonly capabilities?: readonly string[]
  readonly iconMode?: IconMode
  readonly onCancel?: () => void
  readonly onCommand?: (command: CoreSessionCommand) => boolean | Promise<boolean> | void
  readonly onEdit?: (path: string) => Promise<void> | void
}

type DialogState =
  | Readonly<{
      kind: "mkdir"
      pane: PaneId
      cwd_bytes_hex: string
      snapshot_revision: string
      cwd_device: string
      cwd_inode: string
      cwd_ctime_unix_ns: string
    }>
  | Readonly<{ kind: "delete"; target: string; command: CoreSessionCommand }>

function entryColor(entry: SnapshotPayload["entries"][number]): string {
  if (entry.kind === "directory") return COLORS.blue
  if (entry.kind === "executable") return COLORS.green
  if (entry.kind === "symlink") return COLORS.teal
  const group = extensionGroup(entry)
  if (group === "archive") return COLORS.red
  if (group === "code") return COLORS.green
  if (group === "document") return COLORS.yellow
  if (group === "image") return COLORS.mauve
  if (group === "media") return COLORS.peach
  return COLORS.text
}

function sortLabel(snapshot: SnapshotPayload, key: PaneSortKey, label: string): string {
  if (snapshot.sort_key !== key) return label
  return `${label} ${snapshot.sort_descending ? "▼" : "▲"}`
}

function compactMetadataKey(snapshot: SnapshotPayload): "size" | "mtime" | "mode" {
  if (snapshot.sort_key === "mtime" || snapshot.sort_key === "mode") return snapshot.sort_key
  return "size"
}

function metadataLabel(key: "size" | "mtime" | "mode"): string {
  return key === "size" ? "Size" : key === "mtime" ? "Modified" : "Permissions"
}

function metadataWidth(key: "size" | "mtime" | "mode"): number {
  return key === "size" ? 10 : key === "mtime" ? 18 : 14
}

function ColumnHeader(props: {
  readonly id: string
  readonly label: string
  readonly active: boolean
  readonly width?: number
  readonly grow?: boolean
  readonly onClick: () => void
}) {
  return <text
    id={props.id}
    width={props.width}
    flexGrow={props.grow ? 1 : 0}
    fg={props.active ? COLORS.crust : COLORS.subtext0}
    bg={props.active ? COLORS.lavender : COLORS.surface0}
    onMouseDown={(event) => {
      event.preventDefault()
      event.stopPropagation()
      props.onClick()
    }}
  > {props.label}</text>
}

function PaneColumns(props: {
  readonly pane: PaneId
  readonly snapshot: SnapshotPayload
  readonly detailed: boolean
  readonly onSort: (pane: PaneId, key: PaneSortKey) => void
}) {
  const header = (key: PaneSortKey, label: string) => sortLabel(props.snapshot, key, label)
  const compactKey = () => compactMetadataKey(props.snapshot)
  return <box width="100%" height={1} flexDirection="row" backgroundColor={COLORS.surface0}>
    <text width={4} bg={COLORS.surface0}> </text>
    <ColumnHeader id={`sort-${props.pane}-name`} label={header("name", "Name")} active={props.snapshot.sort_key === "name"} grow onClick={() => props.onSort(props.pane, "name")} />
    <Show when={props.detailed} fallback={<ColumnHeader
      id={`sort-${props.pane}-${compactKey()}`}
      label={header(compactKey(), metadataLabel(compactKey()))}
      active={props.snapshot.sort_key === compactKey()}
      width={metadataWidth(compactKey())}
      onClick={() => props.onSort(props.pane, compactKey())}
    />}>
      <ColumnHeader id={`sort-${props.pane}-mode`} label={header("mode", "Permissions")} active={props.snapshot.sort_key === "mode"} width={14} onClick={() => props.onSort(props.pane, "mode")} />
      <ColumnHeader id={`sort-${props.pane}-size`} label={header("size", "Size")} active={props.snapshot.sort_key === "size"} width={10} onClick={() => props.onSort(props.pane, "size")} />
      <ColumnHeader id={`sort-${props.pane}-mtime`} label={header("mtime", "Modified")} active={props.snapshot.sort_key === "mtime"} width={18} onClick={() => props.onSort(props.pane, "mtime")} />
    </Show>
  </box>
}

function EntryList(props: {
  readonly pane: PaneId
  readonly snapshot: SnapshotPayload
  readonly detailed: boolean
  readonly iconMode: IconMode
}) {
  const compactKey = () => compactMetadataKey(props.snapshot)
  let list: ScrollBoxRenderable | undefined
  let scrollTimer: ReturnType<typeof setTimeout> | undefined
  createEffect(() => {
    const cursor = props.snapshot.cursor
    if (cursor < 0) return
    if (scrollTimer !== undefined) clearTimeout(scrollTimer)
    scrollTimer = setTimeout(() => list?.scrollChildIntoView(`entry-${props.pane}-${cursor}`), 0)
    onCleanup(() => {
      if (scrollTimer !== undefined) clearTimeout(scrollTimer)
    })
  })
  return <scrollbox id={`entries-${props.pane}`} ref={(value) => { list = value }} flexGrow={1} width="100%" backgroundColor={COLORS.base}>
    <For each={props.snapshot.entries}>
      {(entry, index) => {
        const current = () => index() === props.snapshot.cursor
        return <box id={`entry-${props.pane}-${index()}`} height={1} width="100%" flexDirection="row" backgroundColor={entry.selected || current() ? COLORS.selected : COLORS.base}>
          <text width={1} fg={current() ? COLORS.lavender : COLORS.overlay1}>{current() ? ">" : " "}</text>
          <text width={1} fg={entry.selected ? COLORS.peach : COLORS.overlay1}>{entry.selected ? "*" : " "}</text>
          <text width={2} fg={entryColor(entry)}>{iconForEntry(entry, props.iconMode)}</text>
          <text flexGrow={1} fg={entry.hidden ? COLORS.overlay1 : entryColor(entry)} truncate>{entry.name_display}</text>
          <Show when={props.detailed} fallback={<text width={metadataWidth(compactKey())} fg={COLORS.subtext0}> {compactKey() === "size"
            ? formatFileSize(entry.size_bytes)
            : compactKey() === "mtime"
              ? formatMtime(entry.mtime_unix_ms)
              : formatMode(entry.mode_octal, entry.kind)}</text>}>
            <text width={14} fg={COLORS.subtext0}> {formatMode(entry.mode_octal, entry.kind)}</text>
            <text width={10} fg={COLORS.subtext0}> {formatFileSize(entry.size_bytes)}</text>
            <text width={18} fg={COLORS.subtext0}> {formatMtime(entry.mtime_unix_ms)}</text>
          </Show>
        </box>
      }}
    </For>
  </scrollbox>
}

function Pane(props: {
  readonly pane: PaneId
  readonly snapshot: SnapshotPayload
  readonly active: boolean
  readonly detailed: boolean
  readonly iconMode: IconMode
  readonly onSort: (pane: PaneId, key: PaneSortKey) => void
}) {
  const label = props.pane.toUpperCase()
  return <box
    flexGrow={1}
    height="100%"
    flexDirection="column"
    border
    borderStyle="single"
    borderColor={props.active ? COLORS.lavender : COLORS.surface1}
    backgroundColor={COLORS.base}
    title={`${label}${props.active ? " ACTIVE" : ""} • ${props.snapshot.cwd_display}`}
    titleColor={props.active ? COLORS.lavender : COLORS.sapphire}
  >
    <PaneColumns pane={props.pane} snapshot={props.snapshot} detailed={props.detailed} onSort={props.onSort} />
    <EntryList pane={props.pane} snapshot={props.snapshot} detailed={props.detailed} iconMode={props.iconMode} />
  </box>
}

function Workspace(props: {
  readonly workspace: WorkspaceSnapshotPayload
  readonly wide: boolean
  readonly detailed: boolean
  readonly iconMode: IconMode
  readonly onSort: (pane: PaneId, key: PaneSortKey) => void
}) {
  return <box flexGrow={1} width="100%" flexDirection="row" gap={1}>
    <Show when={props.wide} fallback={props.workspace.active_pane === "left"
      ? <Pane pane="left" snapshot={props.workspace.left} active detailed={false} iconMode={props.iconMode} onSort={props.onSort} />
      : <Pane pane="right" snapshot={props.workspace.right} active detailed={false} iconMode={props.iconMode} onSort={props.onSort} />}>
      <Pane pane="left" snapshot={props.workspace.left} active={props.workspace.active_pane === "left"} detailed={props.detailed} iconMode={props.iconMode} onSort={props.onSort} />
      <Pane pane="right" snapshot={props.workspace.right} active={props.workspace.active_pane === "right"} detailed={props.detailed} iconMode={props.iconMode} onSort={props.onSort} />
    </Show>
  </box>
}

function ErrorPanel(props: { readonly message: string }) {
  return <box flexGrow={1} width="100%" flexDirection="column" justifyContent="center" alignItems="center" border borderStyle="single" borderColor={COLORS.red} backgroundColor={COLORS.base}>
    <text fg={COLORS.red}>CORE ERROR</text>
    <text fg={COLORS.text} wrapMode="word">{props.message}</text>
  </box>
}

function LoadingPanel() {
  return <box flexGrow={1} width="100%" flexDirection="column" justifyContent="center" alignItems="center" border borderStyle="single" borderColor={COLORS.surface1} backgroundColor={COLORS.base}>
    <text fg={COLORS.lavender}>CONNECTING TO CORE</text>
    <text fg={COLORS.subtext0}>Press F10, ZZ, or ZQ to cancel</text>
  </box>
}

function Viewer(props: { readonly preview: PreviewPayload }) {
  return <box flexGrow={1} width="100%" flexDirection="column" border borderStyle="double" borderColor={COLORS.lavender} backgroundColor={COLORS.base} paddingX={1} title="F3 VIEW" titleColor={COLORS.lavender}>
    <text height={1} fg={COLORS.subtext0}>{props.preview.kind} · {props.preview.state} · generation {props.preview.generation}</text>
    <scrollbox flexGrow={1} width="100%" backgroundColor={COLORS.base}>
      <text fg={COLORS.text} wrapMode="word">{props.preview.content ?? props.preview.error_code ?? "Preview unavailable"}</text>
    </scrollbox>
    <text height={1} fg={COLORS.subtext0}>F3 or Esc to close</text>
  </box>
}

function ActionDialog(props: {
  readonly state: DialogState
  readonly onSubmit: (value?: string) => void
  readonly onCancel: () => void
}) {
  return <box flexGrow={1} width="100%" flexDirection="column" justifyContent="center" alignItems="center" backgroundColor={COLORS.mantle}>
    <box width="70%" flexDirection="column" border borderStyle="double" borderColor={props.state.kind === "delete" ? COLORS.red : COLORS.lavender} backgroundColor={COLORS.base} padding={1} title={props.state.kind === "mkdir" ? "F7 MKDIR" : "F8 DELETE"}>
      <Show when={props.state.kind === "mkdir"} fallback={<>
        <text fg={COLORS.text}>Delete {props.state.kind === "delete" ? props.state.target : ""}?</text>
        <text fg={COLORS.subtext0}>Enter/Y confirms · Esc/N cancels</text>
      </>}>
        <text fg={COLORS.text}>Directory name</text>
        <input id="mkdir-input" focused placeholder="new-directory" maxLength={255} onSubmit={(value) => props.onSubmit(typeof value === "string" ? value : undefined)} />
        <text fg={COLORS.subtext0}>Enter creates · Esc cancels</text>
      </Show>
    </box>
  </box>
}

function StatusBar(props: {
  readonly workspace?: WorkspaceSnapshotPayload
  readonly tasks?: readonly PreviewTaskPayload[]
  readonly actionTasks?: readonly ActionTaskPayload[]
  readonly compact: boolean
  readonly notice?: string
  readonly iconMode: IconMode
}) {
  const active = () => props.workspace?.active_pane ?? "left"
  const snapshot = () => active() === "left" ? props.workspace?.left : props.workspace?.right
  const latestTask = () => props.tasks?.at(-1)
  const latestAction = () => props.actionTasks?.at(-1)
  const detail = () => latestAction() !== undefined
    ? `${latestAction()!.action} ${latestAction()!.state} ${latestAction()!.completed_count}/${latestAction()!.total_count}${latestAction()!.partial ? " partial" : ""}${latestAction()!.error_code === undefined ? "" : ` ${latestAction()!.error_code}`}`
    : props.notice ?? (latestTask() === undefined ? `${snapshot()?.selection_count ?? 0} selected` : `task ${latestTask()!.state}`)
  if (props.iconMode === "ascii") {
    return <box width="100%" height={1} flexDirection="row" backgroundColor={COLORS.surface0}>
      <text bg={COLORS.mauve} fg={COLORS.crust}> NORMAL </text>
      <text bg={COLORS.sapphire} fg={COLORS.crust}> {active().toUpperCase()} </text>
      <text flexGrow={1} bg={COLORS.surface0} fg={COLORS.text} truncate> {snapshot()?.cwd_display ?? "connecting"} </text>
      <text bg={COLORS.green} fg={COLORS.crust}> {snapshot()?.entry_count ?? 0} items </text>
      <Show when={!props.compact || latestAction()?.state === "failed" || latestAction()?.state === "cancelled" || latestAction()?.partial}>
        <text bg={COLORS.surface1} fg={COLORS.text}> {detail()} </text>
      </Show>
    </box>
  }
  return <box width="100%" height={1} flexDirection="row" backgroundColor={COLORS.crust}>
    <text fg={COLORS.mauve} bg={COLORS.crust}></text>
    <text bg={COLORS.mauve} fg={COLORS.crust}> NORMAL </text>
    <text fg={COLORS.mauve} bg={COLORS.sapphire}></text>
    <text bg={COLORS.sapphire} fg={COLORS.crust}> {active().toUpperCase()} </text>
    <text fg={COLORS.sapphire} bg={COLORS.surface0}></text>
    <text flexGrow={1} bg={COLORS.surface0} fg={COLORS.text} truncate> {snapshot()?.cwd_display ?? "connecting"} </text>
    <text fg={COLORS.surface0} bg={COLORS.green}></text>
    <text bg={COLORS.green} fg={COLORS.crust}> {snapshot()?.entry_count ?? 0} items </text>
    <Show when={!props.compact || latestAction()?.state === "failed" || latestAction()?.state === "cancelled" || latestAction()?.partial}>
      <text fg={COLORS.green} bg={COLORS.surface1}></text>
      <text bg={COLORS.surface1} fg={COLORS.text}> {detail()} </text>
      <text fg={COLORS.surface1} bg={COLORS.crust}></text>
    </Show>
    <Show when={props.compact}><text fg={COLORS.green} bg={COLORS.crust}></text></Show>
  </box>
}

function FunctionKey(props: {
  readonly action: FunctionAction
  readonly keyName: string
  readonly label: string
  readonly enabled?: boolean
  readonly compact: boolean
  readonly onAction: (action: FunctionAction) => void
}) {
  const enabled = () => props.enabled !== false
  return <box
    id={`function-${props.action}`}
    flexGrow={1}
    height={1}
    flexDirection="row"
    onMouseDown={(event) => {
      event.preventDefault()
      event.stopPropagation()
      if (enabled()) props.onAction(props.action)
    }}
  >
    <text bg={enabled() ? COLORS.sapphire : COLORS.surface1} fg={enabled() ? COLORS.crust : COLORS.overlay1}> {props.keyName} </text>
    <Show when={!props.compact}><text flexGrow={1} bg={COLORS.base} fg={enabled() ? COLORS.text : COLORS.overlay1}> {props.label} </text></Show>
  </box>
}

function BottomBars(props: {
  readonly workspace?: WorkspaceSnapshotPayload
  readonly tasks?: readonly PreviewTaskPayload[]
  readonly actionTasks?: readonly ActionTaskPayload[]
  readonly compact: boolean
  readonly notice?: string
  readonly canView: boolean
  readonly canFileActions: boolean
  readonly iconMode: IconMode
  readonly onAction: (action: FunctionAction) => void
}) {
  return <box width="100%" height={2} flexDirection="column">
    <StatusBar workspace={props.workspace} tasks={props.tasks} actionTasks={props.actionTasks} compact={props.compact} notice={props.notice} iconMode={props.iconMode} />
    <box width="100%" height={1} flexDirection="row">
      <FunctionKey action="view" keyName="F3" label="View" compact={props.compact} enabled={props.canView} onAction={props.onAction} />
      <FunctionKey action="edit" keyName="F4" label="Edit" compact={props.compact} onAction={props.onAction} />
      <FunctionKey action="copy" keyName="F5" label="Copy" compact={props.compact} enabled={props.canFileActions} onAction={props.onAction} />
      <FunctionKey action="move" keyName="F6" label="Move" compact={props.compact} enabled={props.canFileActions} onAction={props.onAction} />
      <FunctionKey action="mkdir" keyName="F7" label="MkDir" compact={props.compact} enabled={props.canFileActions} onAction={props.onAction} />
      <FunctionKey action="delete" keyName="F8" label="Delete" compact={props.compact} enabled={props.canFileActions} onAction={props.onAction} />
      <FunctionKey action="quit" keyName="F10" label="Quit" compact={props.compact} onAction={props.onAction} />
    </box>
  </box>
}

export function App(props: AppProps) {
  const renderer = useRenderer()
  const dimensions = useTerminalDimensions()
  const wide = () => dimensions().width >= 80
  const detailed = () => dimensions().width >= 120
  const iconMode = (): IconMode => props.iconMode ?? (process.env.NEOVIFM_ICONS === "ascii" ? "ascii" : "fancy")
  const canSort = () => props.capabilities?.includes("workspace-sort-v1") === true
  const actionBusy = () => props.actionTasks?.some((task) => task.state === "queued" || task.state === "running") === true
  const canFileActions = () => props.capabilities?.includes("file-actions-v1") === true && !actionBusy()
  const keymap = new VifmKeymap()
  const [viewerOpen, setViewerOpen] = createSignal(false)
  const [notice, setNotice] = createSignal<string | undefined>()
  const [dialog, setDialog] = createSignal<DialogState | undefined>()

  const activeSnapshot = () => props.workspace?.active_pane === "right" ? props.workspace.right : props.workspace?.left
  const currentEntry = () => {
    const snapshot = activeSnapshot()
    return snapshot === undefined || snapshot.cursor < 0 ? undefined : snapshot.entries[snapshot.cursor]
  }
  const matchingPreview = () => {
    const preview = props.preview
    const workspace = props.workspace
    const entry = currentEntry()
    if (preview === undefined || workspace === undefined || entry === undefined) return undefined
    return preview.pane === workspace.active_pane && preview.path_bytes_hex === entry.path_bytes_hex
      ? preview
      : undefined
  }
  const actionTargets = (snapshot: SnapshotPayload): readonly CoreActionTarget[] | undefined => {
    const entries = snapshot.selection_count !== 0
      ? snapshot.entries.filter((entry) => entry.selected)
      : snapshot.cursor < 0 ? [] : [snapshot.entries[snapshot.cursor]!]
    if (entries.some((entry) => entry.device === undefined || entry.inode === undefined || entry.ctime_unix_ns === undefined)) return undefined
    return entries.map((entry) => ({
      path_bytes_hex: entry.path_bytes_hex,
      device: entry.device!,
      inode: entry.inode!,
      ctime_unix_ns: entry.ctime_unix_ns!,
      kind: entry.kind,
    }))
  }
  const actionContext = (snapshot: SnapshotPayload) => snapshot.cwd_device === undefined || snapshot.cwd_inode === undefined || snapshot.cwd_ctime_unix_ns === undefined || snapshot.snapshot_revision === "0"
    ? undefined
    : {
        cwd_bytes_hex: snapshot.cwd_bytes_hex,
        snapshot_revision: snapshot.snapshot_revision,
        cwd_device: snapshot.cwd_device,
        cwd_inode: snapshot.cwd_inode,
        cwd_ctime_unix_ns: snapshot.cwd_ctime_unix_ns,
      }
  const pathFromIdentity = (pathBytesHex: string): string | undefined => {
    if (pathBytesHex.length === 0 || pathBytesHex.length % 2 !== 0 || !/^[0-9a-f]+$/i.test(pathBytesHex)) return undefined
    const pairs = pathBytesHex.match(/../g)
    if (pairs === null) return undefined
    const bytes = Uint8Array.from(pairs, (pair) => Number.parseInt(pair, 16))
    try {
      const path = new TextDecoder("utf-8", { fatal: true }).decode(bytes)
      return path.includes("\0") ? undefined : path
    } catch {
      return undefined
    }
  }
  const sendCommand = (command: CoreSessionCommand, successNotice?: string) => {
    if ((command.action === "sort-by" || command.action === "sort-cycle") && !canSort()) {
      setNotice("Core sorting is unavailable")
      return false
    }
    const sent = props.onCommand?.(command)
    if (sent instanceof Promise) {
      void sent.then((accepted) => {
        if (!accepted) setNotice("Core command channel is unavailable")
        else if (successNotice !== undefined) setNotice(successNotice)
      })
      return true
    }
    if (sent === false) {
      setNotice("Core command channel is unavailable")
      return false
    }
    if (successNotice !== undefined) setNotice(successNotice)
    return true
  }
  const quit = () => {
    props.onCancel?.()
    renderer.destroy()
  }
  const dispatchFunction = (action: FunctionAction) => {
    if (dialog() !== undefined) return
    if (action === "quit") {
      quit()
      return
    }
    if (action === "view") {
      if (matchingPreview()?.content === undefined) {
        setNotice("View unavailable")
      } else {
        setViewerOpen((open) => !open)
        setNotice(undefined)
      }
      return
    }
    if (action === "edit") {
      const entry = currentEntry()
      if (entry === undefined || entry.kind === "directory") {
        setNotice("Edit requires a file")
      } else if (props.onEdit === undefined) {
        setNotice("Editor unavailable")
      } else {
        const path = pathFromIdentity(entry.path_bytes_hex)
        if (path === undefined) {
          setNotice("Editor unavailable for a non-UTF-8 path")
          return
        }
        renderer.suspend()
        void Promise.resolve().then(() => props.onEdit!(path)).then(
          () => setNotice(`Edited ${entry.name_display}`),
          (error) => setNotice(`Edit failed: ${error instanceof Error ? error.message : String(error)}`),
        ).finally(() => renderer.resume())
      }
      return
    }
    if (!canFileActions()) {
      setNotice("Core file actions are unavailable")
      return
    }
    if (action === "mkdir") {
      const workspace = props.workspace
      if (workspace === undefined) {
        setNotice("MkDir requires a workspace")
        return
      }
      const pane = workspace.active_pane
      const snapshot = pane === "left" ? workspace.left : workspace.right
      const context = actionContext(snapshot)
      if (context === undefined) {
        setNotice("MkDir requires a stable core snapshot")
        return
      }
      setDialog({ kind: "mkdir", pane, ...context })
      return
    }
    if (action === "delete") {
      const entry = currentEntry()
      if (entry === undefined) setNotice("Delete requires a file")
      else {
        const workspace = props.workspace!
        const pane = workspace.active_pane
        const snapshot = pane === "left" ? workspace.left : workspace.right
        const context = actionContext(snapshot)
        const targets = actionTargets(snapshot)
        if (context === undefined || targets === undefined) {
          setNotice("Delete requires a stable core snapshot")
          return
        }
        const selectionCount = snapshot.selection_count
        setDialog({
          kind: "delete",
          target: selectionCount === 0 ? entry.name_display : `${selectionCount} selected items`,
          command: {
            action: "delete",
            pane,
            ...context,
            targets,
          },
        })
      }
      return
    }
    const workspace = props.workspace
    if (workspace === undefined) {
      setNotice(`${action === "move" ? "Move" : "Copy"} requires a workspace`)
      return
    }
    const pane = workspace.active_pane
    const snapshot = pane === "left" ? workspace.left : workspace.right
    const destination = pane === "left" ? workspace.right : workspace.left
    const context = actionContext(snapshot)
    const destinationContext = actionContext(destination)
    const targets = actionTargets(snapshot)
    if (targets?.length === 0) {
      setNotice(`${action === "move" ? "Move" : "Copy"} requires a file`)
      return
    }
    if (context === undefined || destinationContext === undefined || targets === undefined) {
      setNotice(`${action === "move" ? "Move" : "Copy"} requires stable core snapshots`)
      return
    }
    sendCommand({
      action: action === "move" ? "move-files" : "copy",
      pane,
      ...context,
      destination_cwd_bytes_hex: destination.cwd_bytes_hex,
      destination_snapshot_revision: destinationContext.snapshot_revision,
      destination_cwd_device: destinationContext.cwd_device,
      destination_cwd_inode: destinationContext.cwd_inode,
      destination_cwd_ctime_unix_ns: destinationContext.cwd_ctime_unix_ns,
      targets,
    }, `${action === "move" ? "Move" : "Copy"} requested`)
  }
  const closeDialog = () => setDialog(undefined)
  const submitDialog = (value?: string) => {
    const state = dialog()
    if (state?.kind === "mkdir") {
      const name = value?.trim() ?? ""
      if (name.length === 0 || new TextEncoder().encode(name).byteLength > 255 ||
        name === "." || name === ".." || name.includes("/") || name.includes("\\") || name.includes("\0")) {
        setNotice("Invalid directory name")
        return
      }
      sendCommand({
        action: "mkdir",
        pane: state.pane,
        cwd_bytes_hex: state.cwd_bytes_hex,
        snapshot_revision: state.snapshot_revision,
        cwd_device: state.cwd_device,
        cwd_inode: state.cwd_inode,
        cwd_ctime_unix_ns: state.cwd_ctime_unix_ns,
        name,
      }, `Create ${name} requested`)
    } else if (state?.kind === "delete") {
      sendCommand(state.command, `Delete ${state.target} requested`)
    }
    closeDialog()
  }

  useKeyboard((key) => {
    const openDialog = dialog()
    if (openDialog !== undefined) {
      if (key.name === "escape" || (openDialog.kind === "delete" && key.name.toLowerCase() === "n")) {
        key.preventDefault()
        key.stopPropagation()
        closeDialog()
      } else if (openDialog.kind === "delete" && (key.name === "return" || key.name.toLowerCase() === "y")) {
        key.preventDefault()
        key.stopPropagation()
        submitDialog()
      }
      return
    }
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
      quit()
      return
    }
    if (result.kind === "function") {
      dispatchFunction(result.action)
      return
    }
    sendCommand(result.command)
  })

  return <box width="100%" height="100%" flexDirection="column" backgroundColor={COLORS.crust} padding={1} gap={1}>
    <box width="100%" height={1} flexDirection="row">
      <text flexGrow={1} fg={COLORS.lavender}>NeoVifm</text>
      <text fg={COLORS.overlay1}>{dimensions().width}x{dimensions().height}</text>
    </box>
    <Show when={dialog()} fallback={props.error !== undefined
      ? <ErrorPanel message={props.error} />
      : viewerOpen() && matchingPreview() !== undefined
        ? <Viewer preview={matchingPreview()!} />
        : props.workspace !== undefined
          ? <Workspace workspace={props.workspace} wide={wide()} detailed={detailed()} iconMode={iconMode()} onSort={(pane, key) => { sendCommand({ action: "sort-by", pane, key }) }} />
          : props.loading
            ? <LoadingPanel />
            : <ErrorPanel message="Core returned no workspace" />}>
      {(state: () => DialogState) => <ActionDialog state={state()} onSubmit={submitDialog} onCancel={closeDialog} />}
    </Show>
    <BottomBars workspace={props.workspace} tasks={props.tasks} actionTasks={props.actionTasks} compact={dimensions().width < 90} notice={props.commandError ?? notice()} canView={matchingPreview()?.content !== undefined} canFileActions={canFileActions()} iconMode={iconMode()} onAction={dispatchFunction} />
  </box>
}
