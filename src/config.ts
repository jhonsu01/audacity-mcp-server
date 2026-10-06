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

/** dist/bin/aumcp-engine.exe, or AUDACITY_MCP_ENGINE. */
export function resolveEnginePath(env: NodeJS.ProcessEnv = process.env, exists: (p: string) => boolean = existsSync): string | undefined {
  const fromEnv = cleanEnv(env['AUDACITY_MCP_ENGINE']);
  const candidates = [
    fromEnv,
    path.join(codeDir(), 'bin', 'aumcp-engine.exe'),
    path.join(codeDir(), '..', 'dist', 'bin', 'aumcp-engine.exe'),
    path.join(codeDir(), '..', 'native', 'build', 'aumcp-engine.exe'),
  ].filter((p): p is string => !!p);
  return candidates.find((p) => exists(p));
}

/** Audacity extension bundle shipped with the server (dist/extension). */
export function resolveExtensionSource(exists: (p: string) => boolean = existsSync): string | undefined {
  return [path.join(codeDir(), 'extension'), path.join(codeDir(), '..', 'extension')].find((p) => exists(path.join(p, 'manifest.json')));
}

/** Where Audacity 4 looks for user extensions. */
export function audacityExtensionsDir(env: NodeJS.ProcessEnv = process.env): string {
  const local = env['LOCALAPPDATA'] || path.join(os.homedir(), 'AppData', 'Local');
  return path.join(local, 'audacity', 'Audacity4', 'extensions');
}

export const EXTENSION_FOLDER = 'audacity-mcp-tools';

export interface FfmpegResolution {
  path: string | undefined;
  source: 'env' | 'path' | 'not-found';
}

/** FFMPEG_PATH (file or folder), otherwise ffmpeg.exe on PATH. Optional: only needed for MP3/M4A export and exotic imports. */
export function resolveFfmpeg(env: NodeJS.ProcessEnv = process.env, exists: (p: string) => boolean = existsSync): FfmpegResolution {
  const fromEnv = cleanEnv(env['FFMPEG_PATH']);
  if (fromEnv) {
    const candidate = /\.exe$/i.test(fromEnv) ? fromEnv : path.win32.join(fromEnv, 'ffmpeg.exe');
    if (exists(candidate)) return { path: candidate, source: 'env' };
  }
  const dirs = (env['PATH'] || env['Path'] || '').split(';').filter(Boolean);
  for (const d of dirs) {
    const candidate = path.win32.join(d.replace(/"/g, ''), 'ffmpeg.exe');
    if (exists(candidate)) return { path: candidate, source: 'path' };
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
