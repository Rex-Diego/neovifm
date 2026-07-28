import { createEffect, createSignal, For, onCleanup, Show } from "solid-js"
import { useKeyboard, useRenderer, useTerminalDimensions } from "@opentui/solid"
import { MouseButton, type ScrollBoxRenderable } from "@opentui/core"

import type {
  PaneId,
  PaneSortKey,
  ActionTaskPayload,
  PreviewPayload,
  PreviewTaskPayload,
  SnapshotPayload,
  WorkspaceSnapshotPayload,
  PaneTabPayload,
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
import { formatStatusPath, type StatusPathMode } from "./status-path.js"

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
  readonly homeDirectory?: string
  readonly onCopyText?: (text: string) => Promise<void> | void
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

function compactMetadataKey(snapshot: SnapshotPayload): "size" | "ctime" | "mtime" | "mode" {
  if (snapshot.sort_key === "ctime" || snapshot.sort_key === "mtime" || snapshot.sort_key === "mode") return snapshot.sort_key
  return "size"
}

function metadataLabel(key: "size" | "ctime" | "mtime" | "mode"): string {
  return key === "size" ? "Size" : key === "ctime" ? "Created" : key === "mtime" ? "Modified" : "Permissions"
}

function metadataWidth(key: "size" | "ctime" | "mtime" | "mode"): number {
  return key === "size" ? 10 : key === "mode" ? 14 : 18
}

function ColumnHeader(props: {
  readonly id: string
  readonly label: string
  readonly active: boolean
  readonly width?: number
  readonly grow?: boolean
  readonly onToggle: () => void
  readonly onCycle: () => void
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
      if (event.button === MouseButton.RIGHT) props.onCycle()
      else if (event.button === MouseButton.LEFT) props.onToggle()
    }}
  > {props.label}</text>
}

function PaneColumns(props: {
  readonly pane: PaneId
  readonly snapshot: SnapshotPayload
  readonly detailed: boolean
  readonly onSortDirection: (pane: PaneId, key: PaneSortKey) => void
  readonly onSortCycle: (pane: PaneId, delta: -1 | 1) => void
}) {
  const header = (key: PaneSortKey, label: string) => sortLabel(props.snapshot, key, label)
  const compactKey = () => compactMetadataKey(props.snapshot)
  return <box width="100%" height={1} flexDirection="row" backgroundColor={COLORS.surface0}>
    <text width={4} bg={COLORS.surface0}> </text>
    <ColumnHeader id={`sort-${props.pane}-name`} label={header("name", "Name")} active={props.snapshot.sort_key === "name"} grow onToggle={() => props.onSortDirection(props.pane, props.snapshot.sort_key)} onCycle={() => props.onSortCycle(props.pane, 1)} />
    <Show when={props.detailed} fallback={<ColumnHeader
      id={`sort-${props.pane}-${compactKey()}`}
      label={header(compactKey(), metadataLabel(compactKey()))}
      active={props.snapshot.sort_key === compactKey()}
      width={metadataWidth(compactKey())}
      onToggle={() => props.onSortDirection(props.pane, props.snapshot.sort_key)}
      onCycle={() => props.onSortCycle(props.pane, 1)}
    />}>
      <ColumnHeader id={`sort-${props.pane}-mode`} label={header("mode", "Permissions")} active={props.snapshot.sort_key === "mode"} width={14} onToggle={() => props.onSortDirection(props.pane, props.snapshot.sort_key)} onCycle={() => props.onSortCycle(props.pane, 1)} />
      <ColumnHeader id={`sort-${props.pane}-size`} label={header("size", "Size")} active={props.snapshot.sort_key === "size"} width={10} onToggle={() => props.onSortDirection(props.pane, props.snapshot.sort_key)} onCycle={() => props.onSortCycle(props.pane, 1)} />
      <ColumnHeader id={`sort-${props.pane}-ctime`} label={header("ctime", "Created")} active={props.snapshot.sort_key === "ctime"} width={18} onToggle={() => props.onSortDirection(props.pane, props.snapshot.sort_key)} onCycle={() => props.onSortCycle(props.pane, 1)} />
      <ColumnHeader id={`sort-${props.pane}-mtime`} label={header("mtime", "Modified")} active={props.snapshot.sort_key === "mtime"} width={18} onToggle={() => props.onSortDirection(props.pane, props.snapshot.sort_key)} onCycle={() => props.onSortCycle(props.pane, 1)} />
    </Show>
  </box>
}

function EntryList(props: {
  readonly pane: PaneId
  readonly snapshot: SnapshotPayload
  readonly detailed: boolean
  readonly iconMode: IconMode
  readonly onSelect: (pane: PaneId, index: number, toggle: boolean) => void
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
        return <box
          id={`entry-${props.pane}-${index()}`}
          height={1}
          width="100%"
          flexDirection="row"
          backgroundColor={entry.selected || current() ? COLORS.selected : COLORS.base}
          onMouseDown={(event) => {
            if (event.button !== MouseButton.LEFT && event.button !== MouseButton.RIGHT) return
            event.preventDefault()
            event.stopPropagation()
            props.onSelect(props.pane, index(), event.button === MouseButton.RIGHT)
          }}
        >
          <text width={1} fg={current() ? COLORS.lavender : COLORS.overlay1}>{current() ? ">" : " "}</text>
          <text width={1} fg={entry.selected ? COLORS.peach : COLORS.overlay1}>{entry.selected ? "*" : " "}</text>
          <text width={2} fg={entryColor(entry)}>{iconForEntry(entry, props.iconMode)}</text>
          <text flexGrow={1} fg={entry.hidden ? COLORS.overlay1 : entryColor(entry)} truncate>{entry.name_display}</text>
          <Show when={props.detailed} fallback={<text width={metadataWidth(compactKey())} fg={COLORS.subtext0}> {compactKey() === "size"
            ? formatFileSize(entry.size_bytes)
            : compactKey() === "ctime"
              ? formatMtime((BigInt(entry.ctime_unix_ns ?? "0") / 1_000_000n).toString())
              : compactKey() === "mtime"
                ? formatMtime(entry.mtime_unix_ms)
              : formatMode(entry.mode_octal, entry.kind)}</text>}>
            <text width={14} fg={COLORS.subtext0}> {formatMode(entry.mode_octal, entry.kind)}</text>
            <text width={10} fg={COLORS.subtext0}> {formatFileSize(entry.size_bytes)}</text>
            <text width={18} fg={COLORS.subtext0}> {formatMtime((BigInt(entry.ctime_unix_ns ?? "0") / 1_000_000n).toString())}</text>
            <text width={18} fg={COLORS.subtext0}> {formatMtime(entry.mtime_unix_ms)}</text>
          </Show>
        </box>
      }}
    </For>
  </scrollbox>
}

function tabLabel(path: string): string {
  const trimmed = path.length > 1 ? path.replace(/[\\/]+$/, "") : path
  const separator = Math.max(trimmed.lastIndexOf("/"), trimmed.lastIndexOf("\\"))
  const label = trimmed.slice(separator + 1)
  return label.length === 0 ? trimmed : label
}

function compactTabLabel(label: string, maximum: number, iconMode: IconMode): string {
  const characters = Array.from(label)
  if (characters.length <= maximum) return label
  return `${characters.slice(0, maximum - 1).join("")}${iconMode === "ascii" ? "~" : "…"}`
}

function PaneTabs(props: {
  readonly pane: PaneId
  readonly tabs: readonly PaneTabPayload[]
  readonly activePane: boolean
  readonly iconMode: IconMode
  readonly enabled: boolean
  readonly onActivate: (pane: PaneId, tabId: string) => void
  readonly onClose: (pane: PaneId, tabId: string) => void
  readonly onNew: (pane: PaneId) => void
}) {
  const marker = () => props.activePane ? (props.iconMode === "ascii" ? "*" : "●") : " "
  return <box width="100%" height={1} flexDirection="row" backgroundColor={COLORS.crust}>
    <text width={2} fg={COLORS.lavender}> {marker()}</text>
    <For each={props.tabs}>
      {(tab, index) => {
        const color = () => tab.active ? COLORS.lavender : COLORS.surface1
        const body = () => {
          const number = String(index() + 1)
          if (props.tabs.length >= 3 && !tab.active) return number
          const maximum = props.tabs.length === 1 ? 20 : props.tabs.length === 2 ? 9 : 6
          return `${number} ${compactTabLabel(tabLabel(tab.cwd_display), maximum, props.iconMode)}`
        }
        return <box
          id={`tab-${props.pane}-${tab.id}`}
          height={1}
          flexDirection="row"
          onMouseDown={(event) => {
            if (!props.enabled || (event.button !== MouseButton.LEFT && event.button !== MouseButton.RIGHT)) return
            event.preventDefault()
            event.stopPropagation()
            if (event.button === MouseButton.RIGHT) props.onClose(props.pane, tab.id)
            else props.onActivate(props.pane, tab.id)
          }}
        >
          <Show when={props.iconMode !== "ascii"} fallback={<text fg={tab.active ? COLORS.crust : COLORS.text} bg={color()}>[{body()}]</text>}>
            <text fg={color()} bg={COLORS.crust}></text>
            <text fg={tab.active ? COLORS.crust : COLORS.text} bg={color()}>{body()}</text>
            <text fg={color()} bg={COLORS.crust}></text>
          </Show>
        </box>
      }}
    </For>
    <Show when={props.enabled}>
      <box id={`tab-${props.pane}-new`} height={1} onMouseDown={(event) => {
        if (event.button !== MouseButton.LEFT) return
        event.preventDefault()
        event.stopPropagation()
        props.onNew(props.pane)
      }}>
        <Show when={props.iconMode !== "ascii"} fallback={<text fg={COLORS.green}>[+]</text>}>
          <text fg={COLORS.green}></text><text fg={COLORS.crust} bg={COLORS.green}>+</text><text fg={COLORS.green}></text>
        </Show>
      </box>
    </Show>
  </box>
}

function Pane(props: {
  readonly pane: PaneId
  readonly snapshot: SnapshotPayload
  readonly active: boolean
  readonly detailed: boolean
  readonly iconMode: IconMode
  readonly tabs: readonly PaneTabPayload[]
  readonly tabsEnabled: boolean
  readonly onSortDirection: (pane: PaneId, key: PaneSortKey) => void
  readonly onSortCycle: (pane: PaneId, delta: -1 | 1) => void
  readonly onSelect: (pane: PaneId, index: number, toggle: boolean) => void
  readonly onActivateTab: (pane: PaneId, tabId: string) => void
  readonly onCloseTab: (pane: PaneId, tabId: string) => void
  readonly onNewTab: (pane: PaneId) => void
}) {
  return <box
    flexGrow={1}
    height="100%"
    flexDirection="column"
    border
    borderStyle="single"
    borderColor={props.active ? COLORS.lavender : COLORS.surface1}
    backgroundColor={COLORS.base}
  >
    <PaneTabs pane={props.pane} tabs={props.tabs} activePane={props.active} iconMode={props.iconMode} enabled={props.tabsEnabled} onActivate={props.onActivateTab} onClose={props.onCloseTab} onNew={props.onNewTab} />
    <PaneColumns pane={props.pane} snapshot={props.snapshot} detailed={props.detailed} onSortDirection={props.onSortDirection} onSortCycle={props.onSortCycle} />
    <EntryList pane={props.pane} snapshot={props.snapshot} detailed={props.detailed} iconMode={props.iconMode} onSelect={props.onSelect} />
  </box>
}

function Workspace(props: {
  readonly workspace: WorkspaceSnapshotPayload
  readonly wide: boolean
  readonly detailed: boolean
  readonly iconMode: IconMode
  readonly tabsEnabled: boolean
  readonly onSortDirection: (pane: PaneId, key: PaneSortKey) => void
  readonly onSortCycle: (pane: PaneId, delta: -1 | 1) => void
  readonly onSelect: (pane: PaneId, index: number, toggle: boolean) => void
  readonly onActivateTab: (pane: PaneId, tabId: string) => void
  readonly onCloseTab: (pane: PaneId, tabId: string) => void
  readonly onNewTab: (pane: PaneId) => void
}) {
  const tabs = (pane: PaneId): readonly PaneTabPayload[] => props.workspace[`${pane}_tabs`] ?? [{
    id: "0",
    cwd_display: props.workspace[pane].cwd_display,
    active: true,
  }]
  const paneProps = (pane: PaneId) => ({
    pane,
    snapshot: props.workspace[pane],
    active: props.workspace.active_pane === pane,
    tabs: tabs(pane),
    tabsEnabled: props.tabsEnabled,
    iconMode: props.iconMode,
    onSortDirection: props.onSortDirection,
    onSortCycle: props.onSortCycle,
    onSelect: props.onSelect,
    onActivateTab: props.onActivateTab,
    onCloseTab: props.onCloseTab,
    onNewTab: props.onNewTab,
  })
  return <box flexGrow={1} width="100%" flexDirection="row" gap={1}>
    <Show when={props.wide} fallback={props.workspace.active_pane === "left"
      ? <Pane {...paneProps("left")} detailed={false} />
      : <Pane {...paneProps("right")} detailed={false} />}>
      <Pane {...paneProps("left")} detailed={props.detailed} />
      <Pane {...paneProps("right")} detailed={props.detailed} />
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

function Viewer(props: { readonly preview?: PreviewPayload }) {
  const preview = () => props.preview
  return <box flexGrow={1} width="100%" flexDirection="column" border borderStyle="double" borderColor={COLORS.lavender} backgroundColor={COLORS.base} paddingX={1} title="F3 VIEW" titleColor={COLORS.lavender}>
    <text height={1} fg={COLORS.subtext0}>{preview() === undefined ? "loading preview" : `${preview()!.kind} · ${preview()!.state} · generation ${preview()!.generation}`}</text>
    <scrollbox flexGrow={1} width="100%" backgroundColor={COLORS.base}>
      <text fg={COLORS.text} wrapMode="word">{preview() === undefined ? "Loading preview..." : preview()!.content ?? preview()!.error_code ?? "Preview unavailable"}</text>
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

function actionTaskLabel(task: ActionTaskPayload): string {
  return `${task.action} #${task.task_id} ${task.state} ${task.completed_count}/${task.total_count}${task.partial ? " partial" : ""}${task.error_code === undefined ? "" : ` ${task.error_code}`}`
}

function TaskCenter(props: {
  readonly actionTasks?: readonly ActionTaskPayload[]
  readonly onClose: () => void
}) {
  const queue = () => (props.actionTasks ?? []).filter((task) => task.state === "queued" || task.state === "running")
  const history = () => (props.actionTasks ?? []).filter((task) => task.state !== "queued" && task.state !== "running")
  const taskRows = (tasks: readonly ActionTaskPayload[], empty: string) => <Show when={tasks.length !== 0} fallback={<text fg={COLORS.overlay1}>{empty}</text>}>
    <For each={tasks}>{(task) => <text fg={task.state === "failed" || task.state === "cancelled" ? COLORS.red : COLORS.text}>{actionTaskLabel(task)}</text>}</For>
  </Show>
  return <box width="100%" height="100%" flexDirection="column" border borderStyle="double" borderColor={COLORS.lavender} backgroundColor={COLORS.base} padding={1} title="TASK CENTER" titleColor={COLORS.lavender} onKeyDown={(key) => {
    if (key.name.toLowerCase() === "escape" || key.sequence === "\u001b") {
      key.preventDefault()
      key.stopPropagation()
      props.onClose()
    }
  }}>
    <text fg={COLORS.yellow}>QUEUE ({queue().length})</text>
    <scrollbox flexGrow={1} width="100%">
      {taskRows(queue(), "No queued or running file actions")}
      <text height={1} fg={COLORS.yellow}>HISTORY ({history().length})</text>
      {taskRows(history(), "No completed actions")}
    </scrollbox>
    <text height={1} fg={COLORS.subtext0}>Click Tasks or press Esc to close</text>
  </box>
}

function StatusBar(props: {
  readonly workspace?: WorkspaceSnapshotPayload
  readonly tasks?: readonly PreviewTaskPayload[]
  readonly actionTasks?: readonly ActionTaskPayload[]
  readonly compact: boolean
  readonly notice?: string
  readonly iconMode: IconMode
  readonly pathMode: StatusPathMode
  readonly homeDirectory?: string
  readonly onTogglePath: () => void
  readonly onCopyPath: (path: string) => void
  readonly onOpenTasks: () => void
}) {
  const active = () => props.workspace?.active_pane ?? "left"
  const snapshot = () => active() === "left" ? props.workspace?.left : props.workspace?.right
  const displayedPath = () => formatStatusPath(snapshot()?.cwd_display ?? "connecting", props.homeDirectory, props.pathMode)
  const pathMouseDown = (event: { button: number; preventDefault(): void; stopPropagation(): void }) => {
    if (event.button !== MouseButton.LEFT && event.button !== MouseButton.RIGHT) return
    event.preventDefault()
    event.stopPropagation()
    if (event.button === MouseButton.RIGHT) props.onCopyPath(displayedPath())
    else props.onTogglePath()
  }
  const latestTask = () => props.tasks?.at(-1)
  const latestAction = () => props.actionTasks?.at(-1)
  const queuedActions = () => props.actionTasks?.filter((task) => task.state === "queued" || task.state === "running").length ?? 0
  const taskCount = () => props.actionTasks?.length ?? 0
  const taskMouseDown = (event: { button: number; preventDefault(): void; stopPropagation(): void }) => {
    if (event.button !== MouseButton.LEFT) return
    event.preventDefault()
    event.stopPropagation()
    props.onOpenTasks()
  }
  const detail = () => props.notice ?? (latestAction() !== undefined
    ? `${latestAction()!.action} ${latestAction()!.state} ${latestAction()!.completed_count}/${latestAction()!.total_count}${latestAction()!.partial ? " partial" : ""}${latestAction()!.error_code === undefined ? "" : ` ${latestAction()!.error_code}`}`
    : latestTask() === undefined ? `${snapshot()?.selection_count ?? 0} selected` : `task ${latestTask()!.state}`)
  const showDetail = () => props.notice !== undefined || !props.compact
    || latestAction()?.state === "failed" || latestAction()?.state === "cancelled" || latestAction()?.partial === true
  if (props.iconMode === "ascii") {
    return <box width="100%" height={1} flexDirection="row" backgroundColor={COLORS.surface0}>
      <text bg={COLORS.mauve} fg={COLORS.crust}> NORMAL </text>
      <text id="status-path" flexGrow={1} bg={COLORS.surface0} fg={COLORS.text} truncate onMouseDown={pathMouseDown}> {displayedPath()} </text>
      <text bg={COLORS.green} fg={COLORS.crust}> {snapshot()?.entry_count ?? 0} items </text>
      <text id="tasks-entry" bg={queuedActions() !== 0 ? COLORS.yellow : COLORS.surface1} fg={COLORS.crust} onMouseDown={taskMouseDown}> Tasks {queuedActions()}/{taskCount()} </text>
      <Show when={showDetail()}>
        <text bg={COLORS.surface1} fg={COLORS.text}> {detail()} </text>
      </Show>
    </box>
  }
  return <box width="100%" height={1} flexDirection="row" backgroundColor={COLORS.crust}>
    <text fg={COLORS.mauve} bg={COLORS.crust}></text>
    <text bg={COLORS.mauve} fg={COLORS.crust}> NORMAL </text>
    <text fg={COLORS.mauve} bg={COLORS.surface0}></text>
    <text id="status-path" flexGrow={1} bg={COLORS.surface0} fg={COLORS.text} truncate onMouseDown={pathMouseDown}> {displayedPath()} </text>
    <text fg={COLORS.surface0} bg={COLORS.green}></text>
    <text bg={COLORS.green} fg={COLORS.crust}> {snapshot()?.entry_count ?? 0} items </text>
    <text id="tasks-entry" fg={COLORS.crust} bg={queuedActions() !== 0 ? COLORS.yellow : COLORS.surface1} onMouseDown={taskMouseDown}> Tasks {queuedActions()}/{taskCount()} </text>
    <Show when={showDetail()}>
      <text fg={COLORS.green} bg={COLORS.surface1}></text>
      <text bg={COLORS.surface1} fg={COLORS.text}> {detail()} </text>
      <text fg={COLORS.surface1} bg={COLORS.crust}></text>
    </Show>
    <Show when={props.compact && !showDetail()}><text fg={COLORS.green} bg={COLORS.crust}></text></Show>
  </box>
}

function FunctionKey(props: {
  readonly action: FunctionAction
  readonly keyName: string
  readonly label: string
  readonly enabled?: boolean
  readonly iconMode: IconMode
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
      if (event.button === MouseButton.LEFT && enabled()) props.onAction(props.action)
    }}
  >
    <Show when={props.iconMode !== "ascii"} fallback={<text fg={enabled() ? COLORS.sapphire : COLORS.overlay1}>[{props.keyName} {props.label}]</text>}>
      <text fg={enabled() ? COLORS.sapphire : COLORS.surface1} bg={COLORS.crust}></text>
      <text bg={enabled() ? COLORS.sapphire : COLORS.surface1} fg={enabled() ? COLORS.crust : COLORS.overlay1}>{props.keyName} {props.label}</text>
      <text fg={enabled() ? COLORS.sapphire : COLORS.surface1} bg={COLORS.crust}></text>
    </Show>
  </box>
}

const FUNCTION_KEYS: readonly Readonly<{ action: FunctionAction; keyName: string; label: string }>[] = [
  { action: "view", keyName: "F3", label: "View" },
  { action: "edit", keyName: "F4", label: "Edit" },
  { action: "copy", keyName: "F5", label: "Copy" },
  { action: "move", keyName: "F6", label: "Move" },
  { action: "mkdir", keyName: "F7", label: "MkDir" },
  { action: "delete", keyName: "F8", label: "Delete" },
  { action: "quit", keyName: "F10", label: "Quit" },
]

function FunctionRow(props: {
  readonly keys: typeof FUNCTION_KEYS
  readonly canView: boolean
  readonly canFileActions: boolean
  readonly iconMode: IconMode
  readonly onAction: (action: FunctionAction) => void
}) {
  const enabled = (action: FunctionAction) => action === "view"
    ? props.canView
    : action === "copy" || action === "move" || action === "mkdir" || action === "delete"
      ? props.canFileActions
      : true
  return <box width="100%" height={1} flexDirection="row">
    <For each={props.keys}>{(item) => <FunctionKey {...item} enabled={enabled(item.action)} iconMode={props.iconMode} onAction={props.onAction} />}</For>
  </box>
}

function BottomBars(props: {
  readonly workspace?: WorkspaceSnapshotPayload
  readonly tasks?: readonly PreviewTaskPayload[]
  readonly actionTasks?: readonly ActionTaskPayload[]
  readonly compact: boolean
  readonly stacked: boolean
  readonly notice?: string
  readonly canView: boolean
  readonly canFileActions: boolean
  readonly iconMode: IconMode
  readonly onAction: (action: FunctionAction) => void
  readonly pathMode: StatusPathMode
  readonly homeDirectory?: string
  readonly onTogglePath: () => void
  readonly onCopyPath: (path: string) => void
  readonly onOpenTasks: () => void
}) {
  return <box width="100%" height={props.stacked ? 3 : 2} flexDirection="column">
    <StatusBar workspace={props.workspace} tasks={props.tasks} actionTasks={props.actionTasks} compact={props.compact} notice={props.notice} iconMode={props.iconMode} pathMode={props.pathMode} homeDirectory={props.homeDirectory} onTogglePath={props.onTogglePath} onCopyPath={props.onCopyPath} onOpenTasks={props.onOpenTasks} />
    <Show when={props.stacked} fallback={<FunctionRow keys={FUNCTION_KEYS} canView={props.canView} canFileActions={props.canFileActions} iconMode={props.iconMode} onAction={props.onAction} />}>
      <FunctionRow keys={FUNCTION_KEYS.slice(0, 4)} canView={props.canView} canFileActions={props.canFileActions} iconMode={props.iconMode} onAction={props.onAction} />
      <FunctionRow keys={FUNCTION_KEYS.slice(4)} canView={props.canView} canFileActions={props.canFileActions} iconMode={props.iconMode} onAction={props.onAction} />
    </Show>
  </box>
}

export function App(props: AppProps) {
  const renderer = useRenderer()
  const dimensions = useTerminalDimensions()
  const wide = () => dimensions().width >= 80
  const detailed = () => dimensions().width >= 160
  const iconMode = (): IconMode => props.iconMode ?? (process.env.NEOVIFM_ICONS === "ascii" ? "ascii" : "fancy")
  const canSort = () => props.capabilities?.includes("workspace-sort-v1") === true
  const canTabs = () => props.capabilities?.includes("pane-tabs-v1") === true
  const canFileActions = () => props.capabilities?.includes("file-actions-v1") === true
  const keymap = new VifmKeymap()
  const [viewerOpen, setViewerOpen] = createSignal(false)
  const [taskCenterOpen, setTaskCenterOpen] = createSignal(false)
  const [notice, setNotice] = createSignal<string | undefined>()
  const [dialog, setDialog] = createSignal<DialogState | undefined>()
  const [pathMode, setPathMode] = createSignal<StatusPathMode>("absolute")

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
    if ((command.action === "tab-cycle" || command.action === "new-tab" || command.action === "activate-tab" || command.action === "close-tab") && !canTabs()) {
      setNotice("Core pane tabs are unavailable")
      return false
    }
    if (command.action === "select-entry" && !canTabs()) {
      setNotice("Core mouse selection is unavailable")
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
  const copyStatusPath = (path: string) => {
    if (props.onCopyText === undefined) {
      setNotice("Clipboard is unavailable")
      return
    }
    void Promise.resolve().then(() => props.onCopyText!(path)).then(
      () => setNotice(`Copied ${path}`),
      (error) => setNotice(`Copy failed: ${error instanceof Error ? error.message : String(error)}`),
    )
  }
  const dispatchFunction = (action: FunctionAction) => {
    if (dialog() !== undefined) return
    if (action === "quit") {
      quit()
      return
    }
    if (action === "view") {
      setViewerOpen((open) => !open)
      setNotice(undefined)
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
    const escape = key.name.toLowerCase() === "escape" || key.sequence === "\u001b"
    const openDialog = dialog()
    if (openDialog !== undefined) {
      if (escape || (openDialog.kind === "delete" && key.name.toLowerCase() === "n")) {
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
    if (taskCenterOpen()) {
      if (escape) {
        key.preventDefault()
        key.stopPropagation()
        setTaskCenterOpen(false)
      }
      return
    }
    if (viewerOpen() && escape) {
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
    if (result.kind === "tab-index") {
      const workspace = props.workspace
      if (workspace === undefined || !canTabs()) {
        setNotice("Core pane tabs are unavailable")
        return
      }
      const pane = workspace.active_pane
      const tabs = workspace[`${pane}_tabs`] ?? []
      const tab = tabs[result.index]
      if (tab === undefined) {
        setNotice(`Tab ${result.index + 1} does not exist`)
        return
      }
      sendCommand({ action: "activate-tab", pane, tab_id: tab.id })
      return
    }
    if (result.command.action === "enter" && currentEntry()?.kind !== "directory") {
      dispatchFunction("view")
      return
    }
    sendCommand(result.command)
  })

  return <box width="100%" height="100%" flexDirection="column" backgroundColor={COLORS.crust} padding={1} gap={1}>
    <Show when={dialog()} fallback={
      <Show when={taskCenterOpen()} fallback={props.error !== undefined
        ? <ErrorPanel message={props.error} />
        : viewerOpen()
          ? <Viewer preview={matchingPreview()} />
          : props.workspace !== undefined
            ? <Workspace
                workspace={props.workspace}
                wide={wide()}
                detailed={detailed()}
                iconMode={iconMode()}
                tabsEnabled={canTabs()}
                onSortDirection={(pane, key) => { sendCommand({ action: "sort-by", pane, key }) }}
                onSortCycle={(pane, delta) => { sendCommand({ action: "sort-cycle", pane, delta }) }}
                onSelect={(pane, index, toggle) => { sendCommand({ action: "select-entry", pane, index, toggle }) }}
                onActivateTab={(pane, tabId) => { sendCommand({ action: "activate-tab", pane, tab_id: tabId }) }}
                onCloseTab={(pane, tabId) => { sendCommand({ action: "close-tab", pane, tab_id: tabId }) }}
                onNewTab={(pane) => { sendCommand({ action: "new-tab", pane }) }}
              />
            : props.loading
              ? <LoadingPanel />
              : <ErrorPanel message="Core returned no workspace" />}>
        <TaskCenter actionTasks={props.actionTasks} onClose={() => setTaskCenterOpen(false)} />
      </Show>
    }>
      {(state: () => DialogState) => <ActionDialog state={state()} onSubmit={submitDialog} onCancel={closeDialog} />}
    </Show>
    <BottomBars workspace={props.workspace} tasks={props.tasks} actionTasks={props.actionTasks} compact={dimensions().width < 90} stacked={dimensions().width < 72} notice={props.commandError ?? notice()} canView={matchingPreview()?.content !== undefined} canFileActions={canFileActions()} iconMode={iconMode()} onAction={dispatchFunction} pathMode={pathMode()} homeDirectory={props.homeDirectory ?? process.env.HOME ?? process.env.USERPROFILE} onTogglePath={() => setPathMode((mode) => mode === "absolute" ? "home" : "absolute")} onCopyPath={copyStatusPath} onOpenTasks={() => setTaskCenterOpen((open) => !open)} />
  </box>
}
