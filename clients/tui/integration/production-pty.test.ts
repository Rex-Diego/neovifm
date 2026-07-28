import { afterEach, expect, test } from "bun:test"
import { access, chmod, mkdir, mkdtemp, rm, writeFile } from "node:fs/promises"
import { tmpdir } from "node:os"
import { join, resolve } from "node:path"

let temporaryRoot: string | undefined

afterEach(async () => {
  if (temporaryRoot !== undefined) await rm(temporaryRoot, { recursive: true, force: true })
  temporaryRoot = undefined
})

function plainTerminalOutput(output: string): string {
  return output
    .replace(/\u001b\][^\u0007]*(?:\u0007|\u001b\\)/g, "")
    .replace(/\u001b\[[0-?]*[ -/]*[@-~]/g, "")
}

test("production PTY accepts Vifm keys and clickable Total Commander actions", async () => {
  if (process.platform !== "darwin") return
  const executable = process.env.NEOVIFM_CORE_SESSION
  if (executable === undefined || executable.length === 0) {
    throw new Error("NEOVIFM_CORE_SESSION must point to the built core session")
  }
  temporaryRoot = await mkdtemp(join(tmpdir(), "neovifm-production-pty-"))
  const left = join(temporaryRoot, "left")
  const right = join(temporaryRoot, "right")
  const testBin = join(temporaryRoot, "bin")
  const clipboardCapture = join(temporaryRoot, "clipboard.txt")
  const pbcopy = join(testBin, "pbcopy")
  const trashHelper = join(temporaryRoot, "test-trash.sh")
  await mkdir(left)
  await mkdir(right)
  await mkdir(testBin)
  await writeFile(join(left, "a-file"), "a")
  await writeFile(join(left, "b-file"), "b")
  await mkdir(join(right, "a-dir"))
  await writeFile(join(right, "a-dir", "inside"), "inside")
  await writeFile(join(right, "right-file"), "right")
  await writeFile(trashHelper, "#!/bin/sh\nexec /bin/rm -rf -- \"$1\"\n")
  await writeFile(pbcopy, "#!/bin/sh\nexec /bin/cat > \"$NEOVIFM_TEST_CLIPBOARD\"\n")
  await chmod(trashHelper, 0o700)
  await chmod(pbcopy, 0o700)

  const tuiRoot = resolve(import.meta.dir, "..")
  const expectProgram = String.raw`
set timeout 20
log_user 1
spawn -noecho $env(NEOVIFM_TEST_BUN) run src/index.tsx $env(NEOVIFM_TEST_LEFT) $env(NEOVIFM_TEST_RIGHT)
expect {
  "F10 Quit" {}
  timeout { exit 11 }
  eof { exit 12 }
}
after 300
send -- "j"
after 300
send -- "\t"
after 600
send -- "l"
expect {
  "ins" {}
  timeout { exit 20 }
  eof { exit 21 }
}
after 300
send -- "h"
after 300
send -- "j"
after 500
send -- "\033\[<0;20;21M\033\[<0;20;21m"
expect {
  "~/right" {}
  timeout { exit 24 }
  eof { exit 25 }
}
send -- "\033\[<2;20;21M\033\[<2;20;21m"
expect {
  "Copied ~/right" {}
  timeout { exit 26 }
  eof { exit 27 }
}
send -- "\033\[<0;49;23M\033\[<0;49;23m"
expect {
  "F7 MKDIR" {}
  timeout { exit 15 }
  eof { exit 16 }
}
send -- "clicked-pty\r"
after 1000
send -- "\033\[<0;73;23M\033\[<0;73;23m"
expect {
  eof {}
  timeout { exit 17 }
}
`
  const processHandle = Bun.spawn({
    cmd: ["/usr/bin/expect", "-c", expectProgram],
    cwd: tuiRoot,
    env: {
      ...process.env,
      TERM: "xterm-256color",
      HOME: temporaryRoot,
      PATH: `${testBin}:${process.env.PATH ?? ""}`,
      NEOVIFM_CORE_PROBE: executable,
      NEOVIFM_ICONS: "ascii",
      NEOVIFM_TEST_BUN: process.execPath,
      NEOVIFM_TEST_LEFT: left,
      NEOVIFM_TEST_RIGHT: right,
      NEOVIFM_TEST_CLIPBOARD: clipboardCapture,
      NEOVIFM_TRASH_EXECUTABLE: trashHelper,
    },
    stdin: "ignore",
    stdout: "pipe",
    stderr: "pipe",
  })
  const [exitCode, output, diagnostics] = await Promise.all([
    processHandle.exited,
    new Response(processHandle.stdout).text(),
    new Response(processHandle.stderr).text(),
  ])
  const plain = plainTerminalOutput(output)
  expect(exitCode, diagnostics || plain.slice(-2000)).toBe(0)
  expect(plain).not.toContain("NeoVifm")
  expect(plain).toContain("> - b-file")
  expect(plain).toContain("[F3 View]")
  expect(plain).toContain("[F10 Quit]")
  expect(plain).toContain("F7 MKDIR")
  expect(plain).toContain("~/right")
  await access(join(right, "clicked-pty"))
  expect(await Bun.file(clipboardCapture).text()).toBe("~/right")
}, 30_000)
