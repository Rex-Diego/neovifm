import type { CoreSessionCommand } from "./core-client.js"

export interface KeyLike {
  readonly name: string
  readonly sequence: string
  readonly ctrl: boolean
  readonly shift: boolean
  readonly meta: boolean
}

export type FunctionAction = "view" | "edit" | "copy" | "move" | "mkdir" | "delete" | "quit"

export type KeymapResult =
  | Readonly<{ kind: "command"; command: CoreSessionCommand }>
  | Readonly<{ kind: "cancel" }>
  | Readonly<{ kind: "function"; action: FunctionAction }>
  | Readonly<{ kind: "pending" }>
  | Readonly<{ kind: "unhandled" }>

type Prefix = "g" | "q" | "ctrl-w" | "shift-z"

const command = (value: CoreSessionCommand): KeymapResult => ({ kind: "command", command: value })

function normalizedName(key: KeyLike): string {
  if (key.name.length !== 0) return key.name.toLowerCase()
  if (key.sequence === "\t") return "tab"
  if (key.sequence === " ") return "space"
  return key.sequence.length === 1 ? key.sequence.toLowerCase() : ""
}

export class VifmKeymap {
  #prefix: Prefix | undefined

  handle(key: KeyLike): KeymapResult {
    const name = normalizedName(key)
    const prefix = this.#prefix
    this.#prefix = undefined

    if (prefix === "g") {
      if (key.shift) return { kind: "unhandled" }
      if (name === "g") return command({ action: "move-to", target: "first" })
      if (name === "h") return command({ action: "parent" })
      if (name === "j") return command({ action: "move", delta: 1 })
      if (name === "k") return command({ action: "move", delta: -1 })
      if (name === "l") return command({ action: "enter" })
      return { kind: "unhandled" }
    }

    if (prefix === "q") return { kind: "unhandled" }

    if (prefix === "shift-z") {
      if (key.shift && (name === "q" || name === "z")) return { kind: "cancel" }
      return { kind: "unhandled" }
    }

    if (prefix === "ctrl-w") {
      if (key.shift) return { kind: "unhandled" }
      if (name === "w" || name === "p") return command({ action: "focus-next" })
      if (name === "h") return command({ action: "focus", pane: "left" })
      if (name === "l") return command({ action: "focus", pane: "right" })
      return { kind: "unhandled" }
    }

    if (key.ctrl && name === "w") {
      this.#prefix = "ctrl-w"
      return { kind: "pending" }
    }
    if (key.ctrl && name === "l") return command({ action: "refresh" })
    if (key.ctrl && name === "n") return command({ action: "move", delta: 1 })
    if (key.ctrl && name === "p") return command({ action: "move", delta: -1 })
    if (key.ctrl) return { kind: "unhandled" }
    if (key.meta) return { kind: "unhandled" }

    if (name === "f10") return { kind: "function", action: "quit" }
    if (name === "f3") return { kind: "function", action: "view" }
    if (name === "f4") return { kind: "function", action: "edit" }
    if (name === "f5") return { kind: "function", action: "copy" }
    if (name === "f6") return { kind: "function", action: "move" }
    if (name === "f7") return { kind: "function", action: "mkdir" }
    if (name === "f8") return { kind: "function", action: "delete" }
    if (name === "z" && key.shift) {
      this.#prefix = "shift-z"
      return { kind: "pending" }
    }
    if (name === "g" && key.shift) return command({ action: "move-to", target: "last" })
    if (name === "home") return command({ action: "move-to", target: "first" })
    if (name === "end") return command({ action: "move-to", target: "last" })
    if (key.shift) return { kind: "unhandled" }
    if (name === "q") {
      this.#prefix = "q"
      return { kind: "pending" }
    }
    if (name === "g") {
      this.#prefix = "g"
      return { kind: "pending" }
    }
    if (name === "h" || name === "backspace") return command({ action: "parent" })
    if (name === "j" || name === "down") return command({ action: "move", delta: 1 })
    if (name === "k" || name === "up") return command({ action: "move", delta: -1 })
    if (name === "l" || name === "return") return command({ action: "enter" })
    if (name === "left") return command({ action: "sort-cycle", delta: -1 })
    if (name === "right") return command({ action: "sort-cycle", delta: 1 })
    if (name === "space" || name === "tab") return command({ action: "focus-next" })
    if (name === "t") return command({ action: "toggle-selection" })
    return { kind: "unhandled" }
  }
}
