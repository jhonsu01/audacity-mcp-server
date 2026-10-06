import { execFile, spawn } from 'child_process';
import { existsSync, readFileSync } from 'fs';
import * as fs from 'fs/promises';
import * as path from 'path';
import { EXTENSION_FOLDER, audacityExtensionsDir, resolveExtensionSource } from '../config.js';

function readManifestVersion(dir: string): string | undefined {
  try {
    const m = JSON.parse(readFileSync(path.join(dir, 'manifest.json'), 'utf-8')) as { version?: string };
    return m.version;
  } catch {
    return undefined;
  }
}

/** ProductVersion of Audacity4.exe (from its version resource). */
export function audacityVersion(exe: string): Promise<string | undefined> {
  return new Promise((resolve) => {
    if (!exe || !existsSync(exe)) return resolve(undefined);
    execFile(
      'powershell.exe',
      ['-NoProfile', '-NonInteractive', '-Command', `(Get-Item -LiteralPath '${exe.replace(/'/g, "''")}').VersionInfo.ProductVersion`],
      { windowsHide: true, timeout: 15000 },
      (err, stdout) => resolve(err ? undefined : stdout.trim() || undefined),
    );
  });
}

export function audacityRunning(): Promise<boolean> {
  return new Promise((resolve) => {
    execFile('tasklist.exe', ['/FI', 'IMAGENAME eq Audacity4.exe', '/FO', 'CSV', '/NH'], { windowsHide: true, timeout: 10000 }, (err, out) =>
      resolve(!err && /"Audacity4\.exe"/i.test(out)),
    );
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
  const target = path.join(audacityExtensionsDir(), EXTENSION_FOLDER);
  const running = await audacityRunning();
  if (remove) {
    await fs.rm(target, { recursive: true, force: true });
    return {
      success: true,
      removed: target,
      hint: running ? 'Restart Audacity to unload the MCP Audio Tools effects.' : undefined,
    };
  }
  const src = resolveExtensionSource();
  if (!src) throw new Error('The extension bundle is missing from this installation.');
  if (!existsSync(path.join(src, 'platform', 'windows', 'x86_64', 'audacity_mcp_native.dll'))) {
    throw new Error('The extension native library is missing from this installation.');
  }
  try {
    await fs.rm(target, { recursive: true, force: true });
    await fs.mkdir(target, { recursive: true });
    await fs.cp(src, target, { recursive: true });
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    if (/EBUSY|EPERM/.test(msg) && running) {
      throw new Error('Audacity is using the extension library. Close Audacity and run install_audacity_extension again.');
    }
    throw e;
  }
  return {
    success: true,
    installed_to: target,
    version: readManifestVersion(target),
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

/** Opens files in Audacity 4 (new window/project) for manual editing. */
export function openInAudacity(exe: string, files: string[]): Promise<void> {
  return new Promise((resolve, reject) => {
    const child = spawn(exe, files, { detached: true, stdio: 'ignore', windowsHide: false });
    child.on('error', reject);
    child.unref();
    setTimeout(resolve, 300);
  });
}
