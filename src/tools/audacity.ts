import { execFile, spawn } from 'child_process';
import { existsSync, readFileSync } from 'fs';
import * as fs from 'fs/promises';
import * as path from 'path';
import {
  EXTENSION_FOLDER, audacityExtensionsDir, audacityExtensionsDirs, currentPlatform, extensionLibrary, resolveExtensionSource, whichSync,
} from '../config.js';

const FLATPAK_ID = 'org.audacityteam.Audacity';

function run(cmd: string, args: string[], timeout = 15000): Promise<{ ok: boolean; out: string }> {
  return new Promise((resolve) => {
    execFile(cmd, args, { windowsHide: true, timeout }, (err, stdout) => resolve({ ok: !err, out: String(stdout ?? '') }));
  });
}

function readManifestVersion(dir: string): string | undefined {
  try {
    const m = JSON.parse(readFileSync(path.join(dir, 'manifest.json'), 'utf-8')) as { version?: string };
    return m.version;
  } catch {
    return undefined;
  }
}

/** How to start Audacity on this machine. `engineExe` is what the native engine found. */
export interface AudacityLauncher {
  kind: 'exe' | 'app' | 'flatpak';
  /** Executable, Audacity.app or flatpak id, for display. */
  location: string;
}

export async function resolveAudacity(engineExe: string | undefined): Promise<AudacityLauncher | undefined> {
  const platform = currentPlatform();
  if (platform === 'darwin') {
    if (engineExe && existsSync(engineExe)) return { kind: 'app', location: engineExe };
    for (const app of ['/Applications/Audacity.app', path.join(process.env['HOME'] ?? '', 'Applications', 'Audacity.app')]) {
      if (existsSync(app)) return { kind: 'app', location: app };
    }
    return undefined;
  }
  if (engineExe && existsSync(engineExe)) return { kind: 'exe', location: engineExe };
  if (platform === 'linux') {
    for (const name of ['audacity4', 'audacity']) {
      const found = whichSync(name);
      if (found) return { kind: 'exe', location: found };
    }
    if (whichSync('flatpak')) {
      const info = await run('flatpak', ['info', FLATPAK_ID]);
      if (info.ok) return { kind: 'flatpak', location: FLATPAK_ID };
    }
  }
  return undefined;
}

/** Audacity version string, read without starting the GUI where possible. */
export async function audacityVersion(launcher: AudacityLauncher | undefined): Promise<string | undefined> {
  if (!launcher) return undefined;
  const platform = currentPlatform();
  if (platform === 'win32') {
    const r = await run('powershell.exe', [
      '-NoProfile', '-NonInteractive', '-Command',
      `(Get-Item -LiteralPath '${launcher.location.replace(/'/g, "''")}').VersionInfo.ProductVersion`,
    ]);
    return r.ok ? r.out.trim() || undefined : undefined;
  }
  if (launcher.kind === 'app') {
    const r = await run('/usr/libexec/PlistBuddy', ['-c', 'Print :CFBundleShortVersionString', path.join(launcher.location, 'Contents', 'Info.plist')]);
    return r.ok ? r.out.trim() || undefined : undefined;
  }
  if (launcher.kind === 'flatpak') {
    const r = await run('flatpak', ['info', FLATPAK_ID]);
    return /Version:\s*(\S+)/.exec(r.out)?.[1];
  }
  const r = await run(launcher.location, ['--version'], 20000);
  return /(\d+\.\d+\.\d+)/.exec(r.out)?.[1];
}

export async function audacityRunning(): Promise<boolean> {
  if (currentPlatform() === 'win32') {
    const r = await run('tasklist.exe', ['/FI', 'IMAGENAME eq Audacity4.exe', '/FO', 'CSV', '/NH'], 10000);
    return r.ok && /"Audacity4\.exe"/i.test(r.out);
  }
  const r = await run('pgrep', ['-i', '-f', 'audacity'], 10000);
  // pgrep exits 1 when nothing matches
  return r.ok && r.out.trim().length > 0;
}

/** Opens files in Audacity 4 (new window/project) for manual editing. */
export function openInAudacity(launcher: AudacityLauncher, files: string[]): Promise<void> {
  let cmd: string;
  let args: string[];
  if (launcher.kind === 'app') {
    cmd = 'open';
    args = ['-a', launcher.location, ...files];
  } else if (launcher.kind === 'flatpak') {
    cmd = 'flatpak';
    args = ['run', FLATPAK_ID, ...files];
  } else {
    cmd = launcher.location;
    args = files;
  }
  return new Promise((resolve, reject) => {
    const child = spawn(cmd, args, { detached: true, stdio: 'ignore', windowsHide: false });
    child.on('error', reject);
    child.unref();
    setTimeout(resolve, 300);
  });
}

export interface ExtensionStatus {
  installed: boolean;
  installed_version: string | null;
  bundled_version: string | null;
  path: string;
  up_to_date: boolean;
}

export function extensionStatus(): ExtensionStatus {
  const target = path.join(audacityExtensionsDir(), EXTENSION_FOLDER);
  const src = resolveExtensionSource();
  const installed = existsSync(path.join(target, 'manifest.json'));
  const installedVersion = installed ? readManifestVersion(target) ?? null : null;
  const bundled = src ? readManifestVersion(src) ?? null : null;
  return {
    installed,
    installed_version: installedVersion,
    bundled_version: bundled,
    path: target,
    up_to_date: installed && installedVersion === bundled,
  };
}

export async function installExtension(remove: boolean): Promise<Record<string, unknown>> {
  const targets = audacityExtensionsDirs().map((d) => path.join(d, EXTENSION_FOLDER));
  const target = targets[0]!;
  const running = await audacityRunning();
  if (remove) {
    for (const t of targets) await fs.rm(t, { recursive: true, force: true });
    return {
      success: true,
      removed: targets,
      hint: running ? 'Restart Audacity to unload the MCP Audio Tools effects.' : undefined,
    };
  }
  const src = resolveExtensionSource();
  if (!src) throw new Error('The extension bundle is missing from this installation.');
  const lib = extensionLibrary();
  if (!existsSync(path.join(src, lib))) {
    throw new Error(`The extension native library for this platform (${lib}) is missing from this installation.`);
  }
  try {
    for (const t of targets) {
      await fs.rm(t, { recursive: true, force: true });
      await fs.mkdir(t, { recursive: true });
      await fs.cp(src, t, { recursive: true });
    }
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    if (/EBUSY|EPERM/.test(msg) && running) {
      throw new Error('Audacity is using the extension library. Close Audacity and run install_audacity_extension again.');
    }
    throw e;
  }
  return {
    success: true,
    installed_to: targets.length === 1 ? target : targets,
    version: readManifestVersion(target),
    native_library: lib,
    audacity_running: running,
    effects_added: [
      'Tools > Split into Files (MCP)',
      'Tools > Export Selection to File (MCP)',
      'Tools > Import Audio File as Track (MCP)',
      'Analyze > Labels Every N Seconds (MCP)',
    ],
    hint: running
      ? 'Restart Audacity 4 so it registers the new effects.'
      : 'Start Audacity 4: the effects appear in the Tools and Analyze menus.',
  };
}
