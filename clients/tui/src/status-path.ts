export type StatusPathMode = "absolute" | "home"

function trimHomeSeparators(path: string): string {
  if (path === "/" || /^[A-Za-z]:[\\/]$/.test(path)) return path
  return path.replace(/[\\/]+$/, "")
}

export function formatStatusPath(
  absolutePath: string,
  homeDirectory: string | undefined,
  mode: StatusPathMode,
): string {
  if (mode === "absolute" || homeDirectory === undefined || homeDirectory.length === 0) {
    return absolutePath
  }

  const home = trimHomeSeparators(homeDirectory)
  const windowsStyle = home.includes("\\") || /^[A-Za-z]:/.test(home)
  const comparablePath = windowsStyle ? absolutePath.toLowerCase() : absolutePath
  const comparableHome = windowsStyle ? home.toLowerCase() : home
  if (comparablePath === comparableHome) return "~"
  if (!comparablePath.startsWith(comparableHome)) return absolutePath
  const boundary = absolutePath.charAt(home.length)
  return boundary === "/" || boundary === "\\" ? `~${absolutePath.slice(home.length)}` : absolutePath
}
