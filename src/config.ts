import { existsSync } from 'fs';
import * as os from 'os';
import * as path from 'path';
import { fileURLToPath } from 'url';

/**
 * An unfilled MCPB user_config placeholder ("${user_config.x}") reaches the process
 * literally when the user left the field empty, so treat it as unset.
 */
export function cleanEnv(value: string | undefined): string | undefined {
  if (!value) return undefined;
  const v = value.trim().replace(/^"(.*)"$/, '$1');
  if (!v || v.includes('${')) return undefined;
  return v;
}

/** Folder of the running code (dist/ for both the tsc output and the esbuild bundle). */
export function codeDir(): string {
  return path.dirname(fileURLToPath(import.meta.url));
}

export type Platform = 'win32' | 'darwin' | 'linux';

export function currentPlatform(): Platform {
  const p = process.platform;
  if (p === 'win32' || p === 'darwin' || p === 'linux') return p;
  throw new Error(`Unsupported platform: ${p} (Windows, macOS and Linux are supported)`);
}

export function engineFileName(platform: Platform = currentPlatform()): string {
  return platform === 'win32' ? 'aumcp-engine.exe' : 'aumcp-engine';
}

/** Folders tried for the engine binary of this platform, best first. */
export function engineFolders(platform: Platform = currentPlatform(), arch: string = process.arch): string[] {
  if (platform === 'darwin') return ['darwin-universal', `darwin-${arch}`];
  return [`${platform}-${arch}`];
}

/** dist/bin/<platform>-<arch>/aumcp-engine[.exe], or AUDACITY_MCP_ENGINE. */
export function resolveEnginePath(
  env: NodeJS.ProcessEnv = process.env,
  exists: (p: string) => boolean = existsSync,
  platform: Platform = currentPlatform(),
  arch: string = process.arch,
): string | undefined {
  const fromEnv = cleanEnv(env['AUDACITY_MCP_ENGINE']);
  const file = engineFileName(platform);
  const candidates = [
    fromEnv,
    ...engineFolders(platform, arch).map((d) => path.join(codeDir(), 'bin', d, file)),
    ...engineFolders(platform, arch).map((d) => path.join(codeDir(), '..', 'dist', 'bin', d, file)),
  ].filter((p): p is string => !!p);
  return candidates.find((p) => exists(p));
}

/** Audacity extension bundle shipped with the server (dist/extension). */
export function resolveExtensionSource(exists: (p: string) => boolean = existsSync): string | undefined {
  return [path.join(codeDir(), 'extension'), path.join(codeDir(), '..', 'extension')].find((p) => exists(path.join(p, 'manifest.json')));
}

/**
 * Where Audacity 4 looks for user extensions: QStandardPaths::AppLocalDataLocation with
 * organisation "Audacity" and application "Audacity4", plus "/extensions".
 */
export function audacityExtensionsDir(
  env: NodeJS.ProcessEnv = process.env,
  platform: Platform = currentPlatform(),
  home: string = os.homedir(),
): string {
  if (platform === 'win32') {
    const local = env['LOCALAPPDATA'] || path.win32.join(home, 'AppData', 'Local');
    return path.win32.join(local, 'Audacity', 'Audacity4', 'extensions');
  }
  if (platform === 'darwin') {
    return path.posix.join(home, 'Library', 'Application Support', 'Audacity', 'Audacity4', 'extensions');
  }
  const xdg = env['XDG_DATA_HOME'] && path.posix.isAbsolute(env['XDG_DATA_HOME']) ? env['XDG_DATA_HOME'] : path.posix.join(home, '.local', 'share');
  return path.posix.join(xdg, 'Audacity', 'Audacity4', 'extensions');
}

export const EXTENSION_FOLDER = 'audacity-mcp-tools';

/**
 * Every extensions folder to install into: the standard one, plus (Linux) the Flatpak sandbox data
 * folder when Audacity is installed from Flathub.
 */
export function audacityExtensionsDirs(
  env: NodeJS.ProcessEnv = process.env,
  platform: Platform = currentPlatform(),
  home: string = os.homedir(),
  exists: (p: string) => boolean = existsSync,
): string[] {
  const dirs = [audacityExtensionsDir(env, platform, home)];
  if (platform === 'linux') {
    const flatpakApp = path.posix.join(home, '.var', 'app', 'org.audacityteam.Audacity');
    if (exists(flatpakApp)) dirs.push(path.posix.join(flatpakApp, 'data', 'Audacity', 'Audacity4', 'extensions'));
  }
  return dirs;
}

/** Native library of the extension for this platform, relative to the extension folder. */
export function extensionLibrary(platform: Platform = currentPlatform()): string {
  switch (platform) {
    case 'win32':
      return path.join('platform', 'windows', 'x86_64', 'audacity_mcp_native.dll');
    case 'darwin':
      return path.join('platform', 'macos', 'universal', 'audacity_mcp_native.dylib');
    default:
      return path.join('platform', 'linux', process.arch === 'arm64' ? 'arm64' : 'x86_64', 'audacity_mcp_native.so');
  }
}

/** Searches an executable on PATH (PATHEXT-aware on Windows). */
export function whichSync(
  name: string,
  env: NodeJS.ProcessEnv = process.env,
  exists: (p: string) => boolean = existsSync,
  platform: Platform = currentPlatform(),
): string | undefined {
  const sep = platform === 'win32' ? ';' : ':';
  const join = platform === 'win32' ? path.win32.join : path.posix.join;
  const dirs = (env['PATH'] || env['Path'] || '').split(sep).filter(Boolean);
  const names = platform === 'win32' && !/\.exe$/i.test(name) ? [`${name}.exe`] : [name];
  for (const d of dirs) {
    for (const n of names) {
      const candidate = join(d.replace(/"/g, ''), n);
      if (exists(candidate)) return candidate;
    }
  }
  return undefined;
}

export interface FfmpegResolution {
  path: string | undefined;
  source: 'env' | 'path' | 'not-found';
}

/** FFMPEG_PATH (file or folder), otherwise ffmpeg on PATH (plus Homebrew folders on macOS). Optional. */
export function resolveFfmpeg(
  env: NodeJS.ProcessEnv = process.env,
  exists: (p: string) => boolean = existsSync,
  platform: Platform = currentPlatform(),
): FfmpegResolution {
  const file = platform === 'win32' ? 'ffmpeg.exe' : 'ffmpeg';
  const join = platform === 'win32' ? path.win32.join : path.posix.join;
  const fromEnv = cleanEnv(env['FFMPEG_PATH']);
  if (fromEnv) {
    const candidate = /ffmpeg(\.exe)?$/i.test(fromEnv) ? fromEnv : join(fromEnv, file);
    if (exists(candidate)) return { path: candidate, source: 'env' };
  }
  const onPath = whichSync('ffmpeg', env, exists, platform);
  if (onPath) return { path: onPath, source: 'path' };
  // GUI apps on macOS do not inherit the shell PATH: try the usual package-manager folders.
  if (platform === 'darwin') {
    for (const d of ['/opt/homebrew/bin', '/usr/local/bin', '/opt/local/bin']) {
      if (exists(join(d, file))) return { path: join(d, file), source: 'path' };
    }
  }
  return { path: undefined, source: 'not-found' };
}

/** Positive integer seconds from an env var, otherwise the default. */
export function resolveSeconds(envValue: string | undefined, def: number): number {
  if (!envValue || !/^\d+$/.test(envValue.trim())) return def;
  const n = Number(envValue.trim());
  return n > 0 && n <= 86400 ? n : def;
}

/** Longest single engine job (long files with heavy effects can take minutes). */
export const ENGINE_TIMEOUT_SEC = resolveSeconds(process.env['AUDACITY_MCP_TIMEOUT'], 1800);
