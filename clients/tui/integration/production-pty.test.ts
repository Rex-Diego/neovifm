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
  const trashHelper = join(temporaryRoot, "test-trash.sh")
  await mkdir(left)
  await mkdir(right)
  await writeFile(join(left, "a-file"), "a")
  await writeFile(join(left, "b-file"), "b")
  await writeFile(join(right, "right-file"), "right")
  await writeFile(trashHelper, "#!/bin/sh\nexec /bin/rm -rf -- \"$1\"\n")
  await chmod(trashHelper, 0o700)

  const tuiRoot = resolve(import.meta.dir, "..")
  const expectProgram = String.raw`
set timeout 12
log_user 1
spawn -noecho $env(NEOVIFM_TEST_BUN) run src/index.tsx $env(NEOVIFM_TEST_LEFT) $env(NEOVIFM_TEST_RIGHT)
expect {
  "NeoVifm" {}
  timeout { exit 11 }
  eof { exit 12 }
}
expect {
  "F10" {}
  timeout { exit 18 }
  eof { exit 19 }
}
after 300
send -- "j"
after 300
send -- "\t"
expect {
  "RIGHT" {}
  timeout { exit 13 }
  eof { exit 14 }
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
      NEOVIFM_CORE_PROBE: executable,
      NEOVIFM_ICONS: "ascii",
      NEOVIFM_TEST_BUN: process.execPath,
      NEOVIFM_TEST_LEFT: left,
      NEOVIFM_TEST_RIGHT: right,
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
  expect(plain).toContain("NeoVifm")
  expect(plain).toContain("> - b-file")
  expect(plain).toContain("RIGHT")
  expect(plain).toContain("F7 MKDIR")
  await access(join(right, "clicked-pty"))
}, 20_000)
