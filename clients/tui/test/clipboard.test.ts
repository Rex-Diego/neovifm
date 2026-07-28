import { describe, expect, mock, test } from "bun:test"

import {
  ClipboardError,
  MAX_CLIPBOARD_TEXT_BYTES,
  copyTextToClipboard,
  type ClipboardProcess,
  type ClipboardSpawnOptions,
} from "../src/clipboard.js"

function processWithExit(
  exitCode: number,
  write: (text: string) => number | Promise<number> = (text) => text.length,
): ClipboardProcess {
  return {
    stdin: {
      write,
      end: () => undefined,
    },
    exited: Promise.resolve(exitCode),
  }
}

describe("copyTextToClipboard", () => {
  test("writes macOS text to pbcopy through argv and stdin", async () => {
    const write = mock((text: string) => text.length)
    const end = mock(() => undefined)
    const spawn = mock((_options: ClipboardSpawnOptions): ClipboardProcess => ({
      stdin: { write, end },
      exited: Promise.resolve(0),
    }))

    const result = await copyTextToClipboard("~/work folder", {
      platform: "darwin",
      which: (name) => name === "pbcopy" ? "/usr/bin/pbcopy" : undefined,
      spawn,
    })

    expect(result).toEqual({ provider: "pbcopy", byteLength: 13 })
    expect(spawn).toHaveBeenCalledWith({
      cmd: ["/usr/bin/pbcopy"],
      stdin: "pipe",
      stdout: "ignore",
      stderr: "ignore",
    })
    expect(write).toHaveBeenCalledWith("~/work folder")
    expect(end).toHaveBeenCalledTimes(1)
  })

  test("selects the first available Linux provider with its required arguments", async () => {
    const spawn = mock((_options: ClipboardSpawnOptions) => processWithExit(0))

    const result = await copyTextToClipboard("/tmp/project", {
      platform: "linux",
      which: (name) => name === "xclip" ? "/opt/bin/xclip" : undefined,
      spawn,
    })

    expect(result.provider).toBe("xclip")
    expect(spawn).toHaveBeenCalledWith({
      cmd: ["/opt/bin/xclip", "-selection", "clipboard"],
      stdin: "pipe",
      stdout: "ignore",
      stderr: "ignore",
    })
  })

  test("falls through Linux providers after a spawn failure or non-zero exit", async () => {
    const spawn = mock((options: ClipboardSpawnOptions): ClipboardProcess => {
      const executable = options.cmd[0]
      if (executable === "/bin/wl-copy") throw new Error("Wayland unavailable")
      if (executable === "/bin/xclip") return processWithExit(1)
      return processWithExit(0)
    })

    const result = await copyTextToClipboard("selection", {
      platform: "linux",
      which: (name) => `/bin/${name}`,
      spawn,
    })

    expect(result.provider).toBe("xsel")
    expect(spawn).toHaveBeenCalledTimes(3)
    expect(spawn.mock.calls[2]?.[0]).toEqual({
      cmd: ["/bin/xsel", "--clipboard", "--input"],
      stdin: "pipe",
      stdout: "ignore",
      stderr: "ignore",
    })
  })

  test("uses clip.exe on Windows", async () => {
    const spawn = mock((_options: ClipboardSpawnOptions) => processWithExit(0))

    const result = await copyTextToClipboard("C:\\work", {
      platform: "win32",
      which: (name) => name === "clip.exe" ? "C:\\Windows\\System32\\clip.exe" : undefined,
      spawn,
    })

    expect(result.provider).toBe("clip.exe")
    expect(spawn.mock.calls[0]?.[0].cmd).toEqual(["C:\\Windows\\System32\\clip.exe"])
  })

  test("uses the production spawner without a shell", async () => {
    const cat = Bun.which("cat")
    if (cat === null) throw new Error("The test baseline requires cat")

    const result = await copyTextToClipboard("direct stdin", {
      platform: "darwin",
      which: () => cat,
    })

    expect(result).toEqual({ provider: "pbcopy", byteLength: 12 })
  })

  test("rejects when the platform has no supported clipboard provider", async () => {
    await expect(copyTextToClipboard("/tmp", {
      platform: "linux",
      which: () => undefined,
      spawn: () => processWithExit(0),
    })).rejects.toMatchObject({
      name: "ClipboardError",
      code: "provider-unavailable",
    })

    await expect(copyTextToClipboard("/tmp", {
      platform: "freebsd",
      which: () => "/bin/tool",
      spawn: () => processWithExit(0),
    })).rejects.toMatchObject({
      code: "unsupported-platform",
    })
  })

  test("reports a spawn failure when the only provider cannot start", async () => {
    const cause = new Error("spawn denied")

    await expect(copyTextToClipboard("/tmp", {
      platform: "darwin",
      which: () => "/usr/bin/pbcopy",
      spawn: () => { throw cause },
    })).rejects.toMatchObject({
      code: "spawn-failed",
      provider: "pbcopy",
      cause,
    })
  })

  test("reports a non-zero provider exit", async () => {
    await expect(copyTextToClipboard("/tmp", {
      platform: "win32",
      which: () => "clip.exe",
      spawn: () => processWithExit(7),
    })).rejects.toMatchObject({
      code: "provider-failed",
      provider: "clip.exe",
      exitCode: 7,
    })
  })

  test("reports stdin write failures", async () => {
    const cause = new Error("pipe closed")

    await expect(copyTextToClipboard("/tmp", {
      platform: "darwin",
      which: () => "/usr/bin/pbcopy",
      spawn: () => processWithExit(0, () => { throw cause }),
    })).rejects.toMatchObject({
      code: "write-failed",
      provider: "pbcopy",
      cause,
    })
  })

  test("enforces the UTF-8 byte limit before resolving or spawning a provider", async () => {
    const which = mock(() => "/usr/bin/pbcopy")
    const spawn = mock((_options: ClipboardSpawnOptions) => processWithExit(0))
    const oversized = "界".repeat(Math.floor(MAX_CLIPBOARD_TEXT_BYTES / 3) + 1)

    await expect(copyTextToClipboard(oversized, {
      platform: "darwin",
      which,
      spawn,
    })).rejects.toBeInstanceOf(ClipboardError)
    await expect(copyTextToClipboard("界", {
      platform: "darwin",
      maximumBytes: 2,
      which,
      spawn,
    })).rejects.toMatchObject({
      code: "text-too-large",
    })
    expect(which).not.toHaveBeenCalled()
    expect(spawn).not.toHaveBeenCalled()
  })
})
