import type {
  ErrorPayload,
  HelloPayload,
  ProtocolRecord,
  SnapshotPayload,
  WorkspaceSnapshotPayload,
} from "./protocol.js"

export type ProbeState =
  | Readonly<{ phase: "awaiting-hello" }>
  | Readonly<{ phase: "awaiting-terminal"; hello: HelloPayload; version: 0 | 1 | 2; sequence: number }>
  | Readonly<{ phase: "ready"; hello: HelloPayload; snapshot: SnapshotPayload; sequence: number }>
  | Readonly<{ phase: "ready"; hello: HelloPayload; workspace: WorkspaceSnapshotPayload; sequence: number }>
  | Readonly<{
      phase: "ready"; hello: HelloPayload; workspace: WorkspaceSnapshotPayload; sequence: number
      session: true; commandSequence: number; commandError?: ErrorPayload
    }>
  | Readonly<{ phase: "failed"; hello: HelloPayload; error: ErrorPayload; sequence: number }>

export class ProbeStateError extends Error { override readonly name = "ProbeStateError" }
const frozen = <State extends object>(state: State): State => Object.freeze(state) as State
export const initialProbeState = (): ProbeState => frozen({ phase: "awaiting-hello" })
const increasing = (previous: number, next: number) => {
  if (next <= previous) throw new ProbeStateError("Protocol record sequence must increase")
}
const capabilityFor = (version: 0 | 1 | 2): string => version === 0 ? "snapshot-v0" : version === 1 ? "workspace-v1" : "workspace-session-v2"

export function reduceProbeState(state: ProbeState, record: ProtocolRecord): ProbeState {
  if (state.phase === "awaiting-hello") {
    if (record.type !== "hello") throw new ProbeStateError("Expected hello before a terminal protocol record")
    if (!record.payload.capabilities.includes(capabilityFor(record.version))) throw new ProbeStateError(`Hello record does not advertise ${capabilityFor(record.version)}`)
    return frozen({ phase: "awaiting-terminal", hello: record.payload, version: record.version, sequence: record.sequence })
  }
  if (state.phase === "awaiting-terminal") {
    increasing(state.sequence, record.sequence)
    if (record.version !== state.version) throw new ProbeStateError("Protocol record version changed within a stream")
    if (record.type === "error") return frozen({ phase: "failed", hello: state.hello, error: record.payload, sequence: record.sequence })
    if (state.version === 0 && record.type === "snapshot") return frozen({ phase: "ready", hello: state.hello, snapshot: record.payload, sequence: record.sequence })
    if (state.version === 1 && record.type === "workspace-snapshot") return frozen({ phase: "ready", hello: state.hello, workspace: record.payload, sequence: record.sequence })
    if (state.version === 2 && record.version === 2 && record.type === "workspace-snapshot" && record.payload.command_sequence === 0 && record.payload.trigger === "initial") {
      return frozen({ phase: "ready", hello: state.hello, workspace: record.payload, sequence: record.sequence, session: true, commandSequence: 0 })
    }
    throw new ProbeStateError("Expected the version-specific terminal record after hello")
  }
  if (state.phase === "ready" && "session" in state) {
    increasing(state.sequence, record.sequence)
    if (record.version !== 2) throw new ProbeStateError("Protocol record version changed within a session")
    if (record.type === "workspace-snapshot") {
      const isWatchRefresh = record.payload.trigger === "watch" && record.payload.command_sequence === state.commandSequence
      if (!isWatchRefresh && record.payload.command_sequence <= state.commandSequence) throw new ProbeStateError("Session command sequence must increase")
      if (record.payload.trigger !== "watch" && record.payload.command_sequence === state.commandSequence) throw new ProbeStateError("Only watch snapshots can retain a command sequence")
      return frozen({ ...state, workspace: record.payload, sequence: record.sequence, commandSequence: record.payload.command_sequence, commandError: undefined })
    }
    if (record.type === "command-error") {
      if (record.payload.command_sequence <= state.commandSequence) throw new ProbeStateError("Session command error sequence is stale")
      return frozen({ ...state, sequence: record.sequence, commandSequence: record.payload.command_sequence, commandError: record.payload })
    }
    if (record.type === "error") return frozen({ phase: "failed", hello: state.hello, error: record.payload, sequence: record.sequence })
    throw new ProbeStateError("Unexpected record in session")
  }
  throw new ProbeStateError("Protocol stream already has a terminal record")
}
