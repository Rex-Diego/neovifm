import { afterEach, expect, test } from "bun:test"
import { chmod, mkdir, mkdtemp, rm, writeFile } from "node:fs/promises"
import { tmpdir } from "node:os"
import { resolve } from "node:path"

import { startCoreSession } from "../src/core-client.js"
import { initialProbeState, reduceProbeState, type ProbeState } from "../src/probe-state.js"

let left: string | undefined
let right: string | undefined

afterEach(async () => {
  if (left !== undefined) await rm(left, { recursive: true })
  if (right !== undefined) await rm(right, { recursive: true })
  left = undefined
  right = undefined
})

async function waitFor(predicate: () => boolean, timeoutMs = 15_000): Promise<void> {
  const deadline = Date.now() + timeoutMs
  while (!predicate()) {
    if (Date.now() >= deadline) throw new Error("timed out waiting for core session")
    await Bun.sleep(10)
  }
}

test("real v3 session publishes cancellable preview lifecycle beside core-owned panes", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-right-"))
  await writeFile(resolve(left, "left-a"), "a")
  await writeFile(resolve(left, "left-b"), "b")
  await writeFile(resolve(right, "right-a"), "a")

  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected ready session")
    expect(state.workspace.active_pane).toBe("left")
    expect(state.workspace.right.entries.some((entry) => entry.name_display === "right-a")).toBe(true)
		await waitFor(() => state.phase === "ready" && "session" in state && state.preview?.content === "a")
		if (state.phase !== "ready" || !("session" in state)) throw new Error("expected preview session")
		expect(state.version).toBe(3)
		expect(state.tasks?.some((task) => task.state === "done" && task.kind === "text")).toBe(true)

    expect(await session.send({ action: "focus", pane: "right" })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.workspace.active_pane === "right")
    const beforeMove = state
    expect(await session.send({ action: "move", delta: 1 })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === 2)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected updated session")
    expect(state.workspace.left).toEqual(beforeMove.workspace.left)

    expect(await session.send({ action: "focus", pane: "left" })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.workspace.active_pane === "left")
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected focused session")
    const source = state.workspace.left
    const entry = source.entries[source.cursor]
    if (entry === undefined || source.cwd_device === undefined || source.cwd_inode === undefined || source.cwd_ctime_unix_ns === undefined) {
      throw new Error("expected source identity for explicit preview")
    }
    const expectedPreviewContent = await Bun.file(entry.path_display).text()
    const rightBeforePreview = state.workspace.right
    const previewCommandSequence = state.commandSequence + 1
    expect(await session.send({
      action: "preview",
      pane: "left",
      target_pane: "right",
      cwd_bytes_hex: source.cwd_bytes_hex,
      snapshot_revision: source.snapshot_revision,
      cwd_device: source.cwd_device,
      cwd_inode: source.cwd_inode,
      cwd_ctime_unix_ns: source.cwd_ctime_unix_ns,
      path_bytes_hex: entry.path_bytes_hex,
      device: entry.device!,
      inode: entry.inode!,
      ctime_unix_ns: entry.ctime_unix_ns!,
    })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === previewCommandSequence && state.preview?.target_pane === "right" && state.preview.content === expectedPreviewContent)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected explicit preview session")
    expect(state.workspace.right).toEqual(rightBeforePreview)

    await writeFile(resolve(left, "left-refreshed"), "refresh")
    expect(await session.send({ action: "refresh" })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.workspace.left.entries.some((candidate) => candidate.name_display === "left-refreshed"))
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 30000 })

test("real v3 session preserves Vifm directional name search state", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-search-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-search-right-"))
  await writeFile(resolve(left, "alpha.txt"), "a")
  await writeFile(resolve(left, "beta.txt"), "b")
  await writeFile(resolve(left, "ALPHA-two.txt"), "c")
  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected ready search session")
    const firstSequence = state.commandSequence + 1
    expect(await session.send({ action: "search", query: "alpha", direction: 1 })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === firstSequence && state.workspace.left.entries[state.workspace.left.cursor]?.name_display === "alpha.txt")
    const nextSequence = state.commandSequence + 1
    expect(await session.send({ action: "search-next", direction: 1 })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === nextSequence && state.workspace.left.entries[state.workspace.left.cursor]?.name_display === "ALPHA-two.txt")
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 30000 })

test("real v3 session applies MYVIFMRC fileviewer argv before builtin preview", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-fileviewer-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-fileviewer-right-"))
  const file = resolve(left, "read me.txt")
  const helper = resolve(left, "viewer.sh")
  const config = resolve(left, "vifmrc")
  const launcher = resolve(left, "zz-core-wrapper.sh")
  await writeFile(file, "builtin content")
  await writeFile(helper, "#!/bin/sh\nprintf '%s:%s\\n' \"$1\" \"$2\"\n")
  await chmod(helper, 0o700)
  await writeFile(config, "setl previewprg=viewer.sh --preview %f\nfileviewer *.txt viewer.sh --fileviewer %f\n")
  const shellQuote = (value: string): string => "'" + value.replaceAll("'", "'\"'\"'") + "'"
  const path = `${left}:${process.env.PATH ?? ""}`
  await writeFile(launcher, `#!/bin/sh\nexec env MYVIFMRC=${shellQuote(config)} PATH=${shellQuote(path)} ${shellQuote(executable)} \"$@\"\n`)
  await chmod(launcher, 0o700)
  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable: launcher,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state && state.preview?.content?.startsWith(`--preview:${file}`) === true)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected fileviewer session")
    expect(state.preview).toMatchObject({ kind: "text", state: "done", truncated: false })
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 30000 })

test("real v3 session falls back to fileviewer when previewprg is unsafe", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-previewprg-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-previewprg-right-"))
  const file = resolve(left, "read me.txt")
  const helper = resolve(left, "viewer.sh")
  const config = resolve(left, "vifmrc")
  const launcher = resolve(left, "zz-core-wrapper.sh")
  await writeFile(file, "builtin content")
  await writeFile(helper, "#!/bin/sh\nprintf '%s:%s\\n' \"$1\" \"$2\"\n")
  await chmod(helper, 0o700)
  await writeFile(config, "setl previewprg=viewer.sh --unsafe %f | cat\nfileviewer *.txt viewer.sh --fileviewer %f\n")
  const shellQuote = (value: string): string => "'" + value.replaceAll("'", "'\"'\"'") + "'"
  const path = `${left}:${process.env.PATH ?? ""}`
  await writeFile(launcher, `#!/bin/sh\nexec env MYVIFMRC=${shellQuote(config)} PATH=${shellQuote(path)} ${shellQuote(executable)} \"$@\"\n`)
  await chmod(launcher, 0o700)
  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable: launcher,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state && state.preview?.content?.startsWith(`--fileviewer:${file}`) === true)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected fileviewer fallback session")
    expect(state.preview).toMatchObject({ kind: "text", state: "done", truncated: false })
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 30000 })

test("real v3 session routes an explicit source preview into the opposite render pane", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-explicit-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-explicit-right-"))
  await writeFile(resolve(left, "left-a"), "a")

  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state && state.preview?.content === "a")
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected ready session")
    const source = state.workspace.left
    const entry = source.entries[source.cursor]
    if (entry === undefined || source.cwd_device === undefined || source.cwd_inode === undefined || source.cwd_ctime_unix_ns === undefined || entry.device === undefined || entry.inode === undefined || entry.ctime_unix_ns === undefined) {
      throw new Error("expected source identity for explicit preview")
    }
    const rightBeforePreview = state.workspace.right
    const previewCommandSequence = state.commandSequence + 1
    expect(await session.send({
      action: "preview",
      pane: "left",
      target_pane: "right",
      cwd_bytes_hex: source.cwd_bytes_hex,
      snapshot_revision: source.snapshot_revision,
      cwd_device: source.cwd_device,
      cwd_inode: source.cwd_inode,
      cwd_ctime_unix_ns: source.cwd_ctime_unix_ns,
      path_bytes_hex: entry.path_bytes_hex,
      device: entry.device,
      inode: entry.inode,
      ctime_unix_ns: entry.ctime_unix_ns,
    })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === previewCommandSequence && state.preview?.target_pane === "right" && state.preview.content === "a")
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected explicit preview session")
    expect(state.workspace.right).toEqual(rightBeforePreview)
    const staleCommandSequence = state.commandSequence + 1
    expect(await session.send({
      action: "preview",
      pane: "left",
      target_pane: "right",
      cwd_bytes_hex: source.cwd_bytes_hex,
      snapshot_revision: "0",
      cwd_device: source.cwd_device,
      cwd_inode: source.cwd_inode,
      cwd_ctime_unix_ns: source.cwd_ctime_unix_ns,
      path_bytes_hex: entry.path_bytes_hex,
      device: entry.device,
      inode: entry.inode,
      ctime_unix_ns: entry.ctime_unix_ns,
    })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === staleCommandSequence && state.commandError?.code === "stale-preview")
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected stale preview acknowledgement")
    expect(state.workspace.right).toEqual(rightBeforePreview)
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 15000 })

test("real v3 session previews a ZIP archive as a bounded listing", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  if (process.platform !== "darwin") return
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-archive-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-archive-right-"))
  const archiveSource = resolve(left, "archive-source")
  const archivePath = resolve(left, "bundle.zip")
  await mkdir(archiveSource)
  await writeFile(resolve(archiveSource, "note.txt"), "archive content")
  const zip = Bun.spawn({
    cmd: ["/usr/bin/zip", "-q", archivePath, "note.txt"],
    cwd: archiveSource,
    stdout: "pipe",
    stderr: "pipe",
  })
  expect(await zip.exited).toBe(0)

  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state && state.workspace.left.entries.some((entry) => entry.name_display === "bundle.zip"))
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected ready session")
    const archiveIndex = state.workspace.left.entries.findIndex((entry) => entry.name_display === "bundle.zip")
    expect(archiveIndex).toBeGreaterThanOrEqual(0)
    expect(state.workspace.left.entries[archiveIndex]).toMatchObject({ resource_kind: "archive" })
    expect(await session.send({ action: "select-entry", pane: "left", index: archiveIndex, toggle: false })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.preview?.kind === "archive" && state.preview.content?.includes("note.txt") === true)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected archive preview")
    expect(state.preview).toMatchObject({ kind: "archive", state: "done", truncated: false })
    expect(state.preview?.content).toContain("note.txt")
		const enterCommandSequence = state.commandSequence + 1
		expect(await session.send({ action: "enter" })).toBe(true)
		await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === enterCommandSequence && state.resourceTasks?.some((task) => task.command_sequence === enterCommandSequence && (task.state === "done" || task.state === "failed" || task.state === "cancelled")) === true)
		if (state.phase !== "ready" || !("session" in state)) throw new Error("expected archive enter acknowledgement")
		const mountTask = state.resourceTasks?.find((task) => task.command_sequence === enterCommandSequence && task.state !== "queued" && task.state !== "running")
		if (mountTask?.state === "failed") {
			expect(mountTask.error_code).toBe("resource-mounter-unavailable")
			expect(state.workspace.left.cwd_display).toBe(left)
		} else {
			expect(mountTask?.state).toBe("done")
			expect(state.workspace.left.cwd_display).not.toBe(left)
			expect(state.workspace.left_tabs?.find((tab) => tab.active)?.resource_kind).toBe("archive")
			const parentCommandSequence = state.commandSequence + 1
			expect(await session.send({ action: "parent" })).toBe(true)
			await waitFor(() => state.phase === "ready" && "session" in state && state.workspace.left.cwd_display === left && state.resourceTasks?.some((task) => task.command_sequence === parentCommandSequence && task.state === "done") === true)
		}
		expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 30000 })

test("real v3 session renders a binary file as a bounded hex listing", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-binary-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-binary-right-"))
  const binaryPath = resolve(left, "payload.bin")
  await writeFile(binaryPath, Uint8Array.from([0, 1, 2, 15, 16, 31, 32, 65, 127, 255]))

  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state && state.preview?.kind === "binary" && state.preview.content?.includes("00000000") === true)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected binary preview")
    expect(state.preview).toMatchObject({ kind: "binary", state: "done", truncated: false })
    expect(state.preview?.content).toContain("00 01 02 0f 10 1f 20 41 7f ff")
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 30000 })

test("real v3 session renders image media metadata with dimensions", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-image-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-image-right-"))
  const imagePath = resolve(left, "photo.png")
  await writeFile(imagePath, Uint8Array.from([
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xc8,
  ]))

  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state && state.preview?.kind === "image" && state.preview.content?.includes("size: 256x200") === true)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected image preview")
    expect(state.preview).toMatchObject({ kind: "image", state: "done", truncated: false })
    expect(state.preview?.content).toContain("format: PNG")
    expect(state.preview?.content).toContain("metadata-only")
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 30000 })

test("real v3 session prefers terminal PNG rendering over metadata fileviewer rules", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-image-viewer-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-image-viewer-right-"))
  const imagePath = resolve(left, "photo.png")
  const viewer = resolve(left, "convert")
  const chafa = resolve(left, "chafa")
  const config = resolve(left, "vifmrc")
  const launcher = resolve(left, "image-viewer-wrapper.sh")
  await writeFile(imagePath, Uint8Array.from([
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
  ]))
  await writeFile(viewer, "#!/bin/sh\nprintf 'METADATA-VIEWER\n'\n")
  await writeFile(chafa, "#!/bin/sh\nprintf 'PNG-ASCII\n'\n")
  await writeFile(config, `fileviewer *.png ${viewer} %f\n`)
  const shellQuote = (value: string): string => "'" + value.replaceAll("'", "'\"'\"'") + "'"
  await writeFile(launcher,
    `#!/bin/sh\nexec env MYVIFMRC=${shellQuote(config)} NEOVIFM_CHAFA_EXECUTABLE=${shellQuote(chafa)} ${shellQuote(resolve(executable))} "$@"\n`)
  await Promise.all([chmod(viewer, 0o700), chmod(chafa, 0o700), chmod(launcher, 0o700)])
  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable: launcher,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected PNG viewer session")
    await waitFor(() => state.phase === "ready" && "session" in state && state.workspace.left.entries.some((entry) => entry.name_display === "photo.png"))
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected PNG viewer session")
    const imageIndex = state.workspace.left.entries.findIndex((entry) => entry.name_display === "photo.png")
    expect(imageIndex).toBeGreaterThanOrEqual(0)
    const commandSequence = state.commandSequence + 1
    expect(await session.send({ action: "select-entry", pane: "left", index: imageIndex, toggle: false })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === commandSequence && state.preview?.kind === "image" && state.preview.content?.includes("PNG-ASCII") === true)
    expect(state.preview).toMatchObject({ kind: "image", state: "done", truncated: false })
    expect(state.preview?.content).not.toContain("METADATA-VIEWER")
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 30000 })

test("real v3 session renders PDF and video frames and probes audio metadata", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-media-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-media-right-"))
  const pdfPath = resolve(left, "read me.pdf")
  const videoPath = resolve(left, "movie clip.mp4")
  const audioPath = resolve(left, "track.mp3")
  const pdfRaster = resolve(left, "pdftoppm")
  const ffmpeg = resolve(left, "ffmpeg")
  const ffprobe = resolve(left, "ffprobe")
  const chafa = resolve(left, "chafa")
  const launcher = resolve(left, "media-core-wrapper.sh")
  await writeFile(pdfPath, "%PDF-1.4\n")
  await writeFile(videoPath, "fake video")
  await writeFile(audioPath, "ID3\x04\x00\x00")
  await writeFile(pdfRaster, "#!/bin/sh\nfor arg do output=\"$arg\"; done\nprintf 'fake-png' > \"$output.png\"\n")
  await writeFile(ffmpeg, "#!/bin/sh\nfor arg do output=\"$arg\"; done\nprintf 'fake-png' > \"$output\"\n")
  await writeFile(ffprobe, "#!/bin/sh\nprintf 'format_name=mp3\\nduration=12.500000\\ncodec_name=mp3\\n'\n")
  await writeFile(chafa, "#!/bin/sh\nprintf 'MEDIA-ASCII\\n'\n")
  const shellQuote = (value: string): string => "'" + value.replaceAll("'", "'\"'\"'") + "'"
  await writeFile(launcher,
    `#!/bin/sh\nexec env NEOVIFM_PDF_RENDER_EXECUTABLE=${shellQuote(pdfRaster)} NEOVIFM_FFMPEG_EXECUTABLE=${shellQuote(ffmpeg)} NEOVIFM_FFPROBE_EXECUTABLE=${shellQuote(ffprobe)} NEOVIFM_CHAFA_EXECUTABLE=${shellQuote(chafa)} ${shellQuote(resolve(executable))} "$@"\n`)
  await Promise.all([chmod(pdfRaster, 0o700), chmod(ffmpeg, 0o700), chmod(ffprobe, 0o700), chmod(chafa, 0o700), chmod(launcher, 0o700)])

  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable: launcher,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected ready media session")
    const previewPath = async (path: string, kind: "pdf" | "video" | "audio", marker: string) => {
      if (state.phase !== "ready" || !("session" in state)) throw new Error("expected ready media session")
      const pane = state.workspace.left
      const entry = pane.entries.find((candidate) => candidate.path_bytes_hex === Array.from(new TextEncoder().encode(path), (byte) => byte.toString(16).padStart(2, "0")).join(""))
      if (entry === undefined || pane.cwd_device === undefined || pane.cwd_inode === undefined || pane.cwd_ctime_unix_ns === undefined || entry.device === undefined || entry.inode === undefined || entry.ctime_unix_ns === undefined) {
        throw new Error(`expected media entry: ${path}`)
      }
      const commandSequence = state.commandSequence + 1
      expect(await session.send({
        action: "preview",
        pane: "left",
        target_pane: "right",
        cwd_bytes_hex: pane.cwd_bytes_hex,
        snapshot_revision: pane.snapshot_revision,
        cwd_device: pane.cwd_device,
        cwd_inode: pane.cwd_inode,
        cwd_ctime_unix_ns: pane.cwd_ctime_unix_ns,
        path_bytes_hex: entry.path_bytes_hex,
        device: entry.device,
        inode: entry.inode,
        ctime_unix_ns: entry.ctime_unix_ns,
      })).toBe(true)
      await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === commandSequence && state.preview?.kind === kind && state.preview.state === "done" && state.preview.content?.includes(marker) === true)
    }

    await previewPath(pdfPath, "pdf", "MEDIA-ASCII")
    await previewPath(videoPath, "video", "MEDIA-ASCII")
    await previewPath(audioPath, "audio", "duration=12.500000")
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 30000 })

test("real v3 session falls back to builtin PDF preview after an empty MYVIFMRC viewer", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-media-fallback-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-media-fallback-right-"))
  const pdfPath = resolve(left, "read me.pdf")
  const pdfRaster = resolve(left, "pdftoppm")
  const emptyViewer = resolve(left, "pdftotext")
  const chafa = resolve(left, "chafa")
  const config = resolve(left, "vifmrc")
  const launcher = resolve(left, "media-fallback-wrapper.sh")
  await writeFile(pdfPath, "%PDF-1.4\n")
  await writeFile(emptyViewer, "#!/bin/sh\nexit 0\n")
  await writeFile(pdfRaster, "#!/bin/sh\nfor arg do output=\"$arg\"; done\nprintf 'fake-png' > \"$output.png\"\n")
  await writeFile(chafa, "#!/bin/sh\nprintf 'MEDIA-FALLBACK-ASCII\\n'\n")
  await writeFile(config, "fileviewer *.pdf pdftotext %f\n")
  const shellQuote = (value: string): string => "'" + value.replaceAll("'", "'\"'\"'") + "'"
  const path = `${left}:${process.env.PATH ?? ""}`
  await writeFile(launcher,
    `#!/bin/sh\nexec env MYVIFMRC=${shellQuote(config)} PATH=${shellQuote(path)} NEOVIFM_PDF_RENDER_EXECUTABLE=${shellQuote(pdfRaster)} NEOVIFM_CHAFA_EXECUTABLE=${shellQuote(chafa)} ${shellQuote(resolve(executable))} "$@"\n`)
  await Promise.all([chmod(emptyViewer, 0o700), chmod(pdfRaster, 0o700), chmod(chafa, 0o700), chmod(launcher, 0o700)])

  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable: launcher,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected ready media fallback session")
    const pane = state.workspace.left
    const entry = pane.entries.find((candidate) => candidate.path_bytes_hex === Array.from(new TextEncoder().encode(pdfPath), (byte) => byte.toString(16).padStart(2, "0")).join(""))
    if (entry === undefined || pane.cwd_device === undefined || pane.cwd_inode === undefined || pane.cwd_ctime_unix_ns === undefined || entry.device === undefined || entry.inode === undefined || entry.ctime_unix_ns === undefined) {
      throw new Error("expected PDF entry for empty viewer fallback")
    }
    const commandSequence = state.commandSequence + 1
    expect(await session.send({
      action: "preview",
      pane: "left",
      target_pane: "right",
      cwd_bytes_hex: pane.cwd_bytes_hex,
      snapshot_revision: pane.snapshot_revision,
      cwd_device: pane.cwd_device,
      cwd_inode: pane.cwd_inode,
      cwd_ctime_unix_ns: pane.cwd_ctime_unix_ns,
      path_bytes_hex: entry.path_bytes_hex,
      device: entry.device,
      inode: entry.inode,
      ctime_unix_ns: entry.ctime_unix_ns,
    })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === commandSequence && state.preview?.kind === "pdf" && state.preview.state === "done" && state.preview.content?.includes("MEDIA-FALLBACK-ASCII") === true)
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 30000 })

test("real v3 session allows a slow but bounded PDF rasterization to finish", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-slow-pdf-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-slow-pdf-right-"))
  const pdfPath = resolve(left, "slow.pdf")
  const raster = resolve(left, "pdftoppm")
  const chafa = resolve(left, "chafa")
  const launcher = resolve(left, "slow-pdf-wrapper.sh")
  await writeFile(pdfPath, "%PDF-1.4\n")
  await writeFile(raster, "#!/bin/sh\nsleep 6\nfor arg do output=\"$arg\"; done\nprintf 'slow-png' > \"$output.png\"\n")
  await writeFile(chafa, "#!/bin/sh\nprintf 'SLOW-PDF-ASCII\\n'\n")
  const shellQuote = (value: string): string => "'" + value.replaceAll("'", "'\"'\"'") + "'"
  await writeFile(launcher,
    `#!/bin/sh\nexec env NEOVIFM_PDF_RENDER_EXECUTABLE=${shellQuote(raster)} NEOVIFM_CHAFA_EXECUTABLE=${shellQuote(chafa)} ${shellQuote(resolve(executable))} "$@"\n`)
  await Promise.all([chmod(raster, 0o700), chmod(chafa, 0o700), chmod(launcher, 0o700)])
  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable: launcher,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state && state.workspace.left.entries.some((entry) => entry.name_display === "slow.pdf"))
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected slow PDF session")
    const pdfIndex = state.workspace.left.entries.findIndex((entry) => entry.name_display === "slow.pdf")
    expect(pdfIndex).toBeGreaterThanOrEqual(0)
    const commandSequence = state.commandSequence + 1
    expect(await session.send({ action: "select-entry", pane: "left", index: pdfIndex, toggle: false })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === commandSequence && state.preview?.kind === "pdf" && state.preview.state === "done" && state.preview.content?.includes("SLOW-PDF-ASCII") === true, 20000)
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 30000 })

test("real v3 session allows a slow but bounded image renderer to finish", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-slow-image-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-slow-image-right-"))
  const imagePath = resolve(left, "slow.png")
  const chafa = resolve(left, "chafa")
  const launcher = resolve(left, "slow-image-wrapper.sh")
  await writeFile(imagePath, Uint8Array.from([
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
  ]))
  await writeFile(chafa, "#!/bin/sh\nsleep 6\nprintf 'SLOW-IMAGE-BLOCK\\n'\n")
  const shellQuote = (value: string): string => "'" + value.replaceAll("'", "'\"'\"'") + "'"
  await writeFile(launcher,
    `#!/bin/sh\nexec env NEOVIFM_CHAFA_EXECUTABLE=${shellQuote(chafa)} ${shellQuote(resolve(executable))} "$@"\n`)
  await Promise.all([chmod(chafa, 0o700), chmod(launcher, 0o700)])
  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable: launcher,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state && state.workspace.left.entries.some((entry) => entry.name_display === "slow.png"))
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected slow image session")
    const imageIndex = state.workspace.left.entries.findIndex((entry) => entry.name_display === "slow.png")
    expect(imageIndex).toBeGreaterThanOrEqual(0)
    const commandSequence = state.commandSequence + 1
    expect(await session.send({ action: "select-entry", pane: "left", index: imageIndex, toggle: false })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === commandSequence && state.preview?.kind === "image" && state.preview.state === "done" && state.preview.content?.includes("SLOW-IMAGE-BLOCK") === true, 20000)
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 30000 })

test.skipIf(process.platform === "win32")("real v3 session undoes completed copy and move through the core-owned bridge", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-undo-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-undo-right-"))
  const sourcePath = resolve(left, "note.txt")
  const destinationPath = resolve(right, "note.txt")
  await writeFile(sourcePath, "source")

  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  const actionCommand = (action: "copy" | "move-files") => {
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected ready action session")
    const source = state.workspace.left
    const entry = source.entries.find((candidate) => candidate.name_display === "note.txt")
    if (entry === undefined || source.cwd_device === undefined || source.cwd_inode === undefined || source.cwd_ctime_unix_ns === undefined || entry.device === undefined || entry.inode === undefined || entry.ctime_unix_ns === undefined) {
      throw new Error("expected source action identity")
    }
    return {
      action,
      pane: "left" as const,
      cwd_bytes_hex: source.cwd_bytes_hex,
      snapshot_revision: source.snapshot_revision,
      cwd_device: source.cwd_device,
      cwd_inode: source.cwd_inode,
      cwd_ctime_unix_ns: source.cwd_ctime_unix_ns,
      destination_cwd_bytes_hex: state.workspace.right.cwd_bytes_hex,
      destination_snapshot_revision: state.workspace.right.snapshot_revision,
      destination_cwd_device: state.workspace.right.cwd_device!,
      destination_cwd_inode: state.workspace.right.cwd_inode!,
      destination_cwd_ctime_unix_ns: state.workspace.right.cwd_ctime_unix_ns!,
      targets: [{ path_bytes_hex: entry.path_bytes_hex, device: entry.device, inode: entry.inode, ctime_unix_ns: entry.ctime_unix_ns, kind: entry.kind }],
    } as const
  }
  try {
    await waitFor(() => state.phase === "ready" && "session" in state && state.workspace.left.entries.some((entry) => entry.name_display === "note.txt"))
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected ready copy session")
    const copySequence = state.commandSequence + 1
    expect(await session.send(actionCommand("copy"))).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === copySequence && state.workspace.right.entries.some((entry) => entry.name_display === "note.txt") && state.actionTasks?.some((task) => task.state === "done" && task.undo_available) === true)
    expect(await Bun.file(destinationPath).text()).toBe("source")
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected copied session")
    const undoCopySequence = state.commandSequence + 1
    expect(await session.send({ action: "undo" })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === undoCopySequence && !state.workspace.right.entries.some((entry) => entry.name_display === "note.txt"))
    expect(await Bun.file(sourcePath).text()).toBe("source")
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected copy undo session")

    const moveSequence = state.commandSequence + 1
    expect(await session.send(actionCommand("move-files"))).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === moveSequence && !state.workspace.left.entries.some((entry) => entry.name_display === "note.txt") && state.workspace.right.entries.some((entry) => entry.name_display === "note.txt") && state.actionTasks?.some((task) => task.state === "done" && task.undo_available) === true)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected moved session")
    const undoMoveSequence = state.commandSequence + 1
    expect(await session.send({ action: "undo" })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === undoMoveSequence && state.workspace.left.entries.some((entry) => entry.name_display === "note.txt") && !state.workspace.right.entries.some((entry) => entry.name_display === "note.txt"))
    expect(await Bun.file(sourcePath).text()).toBe("source")
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 30000 })

test("real v3 session resolves a core-owned open command into a structured result", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-open-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-open-right-"))
  const path = resolve(left, "note.md")
  const pathBytesHex = Array.from(new TextEncoder().encode(path), (byte) => byte.toString(16).padStart(2, "0")).join("")
  await writeFile(path, "note")

  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected ready session")
    const commandSequence = state.commandSequence + 1
    expect(await session.send({
      action: "open",
      intent: "open",
      pane: "left",
      cwd_bytes_hex: state.workspace.left.cwd_bytes_hex,
      snapshot_revision: state.workspace.left.snapshot_revision,
      cwd_device: state.workspace.left.cwd_device!,
      cwd_inode: state.workspace.left.cwd_inode!,
      cwd_ctime_unix_ns: state.workspace.left.cwd_ctime_unix_ns!,
      path_bytes_hex: pathBytesHex,
      device: state.workspace.left.entries.find((entry) => entry.path_bytes_hex === pathBytesHex)!.device!,
      inode: state.workspace.left.entries.find((entry) => entry.path_bytes_hex === pathBytesHex)!.inode!,
      ctime_unix_ns: state.workspace.left.entries.find((entry) => entry.path_bytes_hex === pathBytesHex)!.ctime_unix_ns!,
      association_argv: ["viewer", "--wait"],
    })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.open?.command_sequence === commandSequence)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected open result")
    expect(state.open).toMatchObject({
      command_sequence: commandSequence,
      source: "association",
      state: "resolved",
      argv: ["viewer", "--wait", path],
    })
    const staleCommandSequence = state.commandSequence + 1
    expect(await session.send({
      action: "open",
      intent: "open",
      pane: "left",
      cwd_bytes_hex: state.workspace.left.cwd_bytes_hex,
      snapshot_revision: "0",
      cwd_device: state.workspace.left.cwd_device!,
      cwd_inode: state.workspace.left.cwd_inode!,
      cwd_ctime_unix_ns: state.workspace.left.cwd_ctime_unix_ns!,
      path_bytes_hex: pathBytesHex,
      device: state.workspace.left.entries.find((entry) => entry.path_bytes_hex === pathBytesHex)!.device!,
      inode: state.workspace.left.entries.find((entry) => entry.path_bytes_hex === pathBytesHex)!.inode!,
      ctime_unix_ns: state.workspace.left.entries.find((entry) => entry.path_bytes_hex === pathBytesHex)!.ctime_unix_ns!,
      association_argv: ["viewer", "--wait"],
    })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === staleCommandSequence && state.commandError?.code === "stale-open")
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 15000 })

test.skipIf(process.platform !== "darwin")("real v3 session retains failed action identity for a safe core retry", async () => {
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  left = await mkdtemp(resolve(tmpdir(), "neovifm-session-retry-left-"))
  right = await mkdtemp(resolve(tmpdir(), "neovifm-session-retry-right-"))
  const sourcePath = resolve(left, "note.txt")
  const destinationPath = resolve(right, "note.txt")
  await writeFile(sourcePath, "source")
  await writeFile(destinationPath, "existing")

  let state: ProbeState = initialProbeState()
  const errors: Error[] = []
  const session = startCoreSession({
    executable,
    leftPath: left,
    rightPath: right,
    onRecord: (record) => { state = reduceProbeState(state, record) },
    onError: (error) => errors.push(error),
  })
  try {
    await waitFor(() => state.phase === "ready" && "session" in state && state.workspace.left.entries.some((entry) => entry.name_display === "note.txt"))
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected ready session")
    const source = state.workspace.left
    const entry = source.entries.find((candidate) => candidate.name_display === "note.txt")
    if (entry === undefined || source.cwd_device === undefined || source.cwd_inode === undefined || source.cwd_ctime_unix_ns === undefined || entry.device === undefined || entry.inode === undefined || entry.ctime_unix_ns === undefined) {
      throw new Error("expected source action identity")
    }
    expect(await session.send({
      action: "copy",
      pane: "left",
      cwd_bytes_hex: source.cwd_bytes_hex,
      snapshot_revision: source.snapshot_revision,
      cwd_device: source.cwd_device,
      cwd_inode: source.cwd_inode,
      cwd_ctime_unix_ns: source.cwd_ctime_unix_ns,
      destination_cwd_bytes_hex: state.workspace.right.cwd_bytes_hex,
      destination_snapshot_revision: state.workspace.right.snapshot_revision,
      destination_cwd_device: state.workspace.right.cwd_device!,
      destination_cwd_inode: state.workspace.right.cwd_inode!,
      destination_cwd_ctime_unix_ns: state.workspace.right.cwd_ctime_unix_ns!,
      targets: [{ path_bytes_hex: entry.path_bytes_hex, device: entry.device, inode: entry.inode, ctime_unix_ns: entry.ctime_unix_ns, kind: entry.kind }],
    })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.actionTasks?.some((task) => task.state === "failed" && task.retryable) === true)
    if (state.phase !== "ready" || !("session" in state)) throw new Error("expected failed action")
    const failed = state.actionTasks?.find((task) => task.state === "failed" && task.retryable)
    if (failed === undefined) throw new Error("expected retryable failed action")
    const retryCommandSequence = state.commandSequence + 1
    expect(await session.send({ action: "retry-action", task_id: failed.task_id })).toBe(true)
    await waitFor(() => state.phase === "ready" && "session" in state && state.commandSequence === retryCommandSequence && state.actionTasks?.some((task) => task.task_id !== failed.task_id && task.state === "failed" && task.retryable) === true)
    expect(await Bun.file(destinationPath).text()).toBe("existing")
    expect(errors).toEqual([])
  } finally {
    session.close()
    await session.completion
  }
}, { timeout: 30000 })
