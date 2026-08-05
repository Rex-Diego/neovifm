import { once } from "node:events"
import { readFile, writeFile } from "node:fs/promises"
import { dirname, join } from "node:path"

export type FakeCoreEvent =
  | { type: "sleep"; milliseconds: number }
  | { type: "spawn-child-and-wait"; milliseconds: number }
  | { type: "stderr"; value: string }
  | { type: "stderr-repeat"; count: number; prefix: string; suffix: string }
  | { type: "stdout"; value: string }
  | { type: "stdout-line"; value: string }
  | { type: "write-env"; filename: string; names: string[] }

export interface FakeCoreScenario {
  events: FakeCoreEvent[]
  exitCode?: number
  expectedArguments?: string[]
}

async function write(stream: NodeJS.WriteStream, value: string): Promise<void> {
  if (!stream.write(value)) await once(stream, "drain")
}

const childFlag = process.argv.indexOf("--fake-child-sleep")
if (childFlag >= 0) {
  await Bun.sleep(Number(process.argv[childFlag + 1]))
} else {
  const directory = dirname(process.execPath)
  const scenario = JSON.parse(
    await readFile(join(directory, "scenario.json"), "utf8"),
  ) as FakeCoreScenario
  const args = process.argv.slice(2)

  if (scenario.expectedArguments !== undefined &&
      JSON.stringify(args) !== JSON.stringify(scenario.expectedArguments)) {
    process.exitCode = 9
  } else {
    for (const event of scenario.events) {
      switch (event.type) {
        case "sleep":
          await Bun.sleep(event.milliseconds)
          break
        case "spawn-child-and-wait": {
          const child = Bun.spawn({
            cmd: [process.execPath, "--fake-child-sleep", String(event.milliseconds)],
            stdin: "ignore",
            stdout: "inherit",
            stderr: "inherit",
          })
          await child.exited
          break
        }
        case "stderr":
          await write(process.stderr, event.value)
          break
        case "stderr-repeat":
          for (let index = 0; index < event.count; ++index) {
            await write(process.stderr, `${event.prefix}${String(index).padStart(4, "0")}${event.suffix}`)
          }
          break
        case "stdout":
          await write(process.stdout, event.value)
          break
        case "stdout-line":
          await write(process.stdout, `${event.value}\n`)
          break
        case "write-env":
          await writeFile(
            join(directory, event.filename),
            event.names.map((name) => process.env[name] ?? "").join(":"),
          )
          break
      }
    }
    process.exitCode = scenario.exitCode ?? 0
  }
}
