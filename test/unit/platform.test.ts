import { describe, expect, it } from 'vitest';
import * as path from 'path';
import {
  audacityExtensionsDir, audacityExtensionsDirs, cleanEnv, engineFileName, engineFolders, extensionLibrary,
  resolveEnginePath, resolveFfmpeg, resolveSeconds, whichSync,
} from '../../src/config.js';
import { fileExtension, formatFromPath, needsFfmpeg, normalizeFormat } from '../../src/formats.js';
import { resolveOutputPath } from '../../src/paths.js';

describe('Audacity extensions folder (QStandardPaths::AppLocalDataLocation + Audacity/Audacity4)', () => {
  it('Windows uses LOCALAPPDATA', () => {
    expect(audacityExtensionsDir({ LOCALAPPDATA: 'C:\\Users\\u\\AppData\\Local' }, 'win32', 'C:\\Users\\u')).toBe(
      'C:\\Users\\u\\AppData\\Local\\Audacity\\Audacity4\\extensions',
    );
  });
  it('macOS uses Application Support', () => {
    expect(audacityExtensionsDir({}, 'darwin', '/Users/u')).toBe('/Users/u/Library/Application Support/Audacity/Audacity4/extensions');
  });
  it('Linux honours XDG_DATA_HOME and falls back to ~/.local/share', () => {
    expect(audacityExtensionsDir({ XDG_DATA_HOME: '/data' }, 'linux', '/home/u')).toBe('/data/Audacity/Audacity4/extensions');
    expect(audacityExtensionsDir({}, 'linux', '/home/u')).toBe('/home/u/.local/share/Audacity/Audacity4/extensions');
    expect(audacityExtensionsDir({ XDG_DATA_HOME: 'relative' }, 'linux', '/home/u')).toBe('/home/u/.local/share/Audacity/Audacity4/extensions');
  });
  it('Linux adds the Flatpak sandbox folder when Audacity comes from Flathub', () => {
    const flatpak = '/home/u/.var/app/org.audacityteam.Audacity';
    expect(audacityExtensionsDirs({}, 'linux', '/home/u', (p) => p === flatpak)).toEqual([
      '/home/u/.local/share/Audacity/Audacity4/extensions',
      `${flatpak}/data/Audacity/Audacity4/extensions`,
    ]);
    expect(audacityExtensionsDirs({}, 'linux', '/home/u', () => false)).toHaveLength(1);
  });
});

describe('native binaries per platform', () => {
  it('engine names and folders', () => {
    expect(engineFileName('win32')).toBe('aumcp-engine.exe');
    expect(engineFileName('linux')).toBe('aumcp-engine');
    expect(engineFolders('darwin', 'arm64')).toEqual(['darwin-universal', 'darwin-arm64']);
    expect(engineFolders('linux', 'x64')).toEqual(['linux-x64']);
    expect(engineFolders('win32', 'x64')).toEqual(['win32-x64']);
  });
  it('resolveEnginePath prefers AUDACITY_MCP_ENGINE, then dist/bin/<platform>', () => {
    expect(resolveEnginePath({ AUDACITY_MCP_ENGINE: '/x/engine' }, (p) => p === '/x/engine', 'linux', 'x64')).toBe('/x/engine');
    const found = resolveEnginePath({}, (p) => p.endsWith(path.join('bin', 'darwin-universal', 'aumcp-engine')), 'darwin', 'arm64');
    expect(found).toMatch(/darwin-universal/);
    expect(resolveEnginePath({}, () => false, 'linux', 'x64')).toBeUndefined();
  });
  it('extension library matches what MuseApi.Native resolves', () => {
    expect(extensionLibrary('win32').split(path.sep).join('/')).toBe('platform/windows/x86_64/audacity_mcp_native.dll');
    expect(extensionLibrary('darwin').split(path.sep).join('/')).toBe('platform/macos/universal/audacity_mcp_native.dylib');
    expect(extensionLibrary('linux').split(path.sep).join('/')).toMatch(/^platform\/linux\/(x86_64|arm64)\/audacity_mcp_native\.so$/);
  });
});

describe('PATH lookup and FFmpeg', () => {
  it('uses ; and .exe on Windows, : on POSIX', () => {
    expect(whichSync('ffmpeg', { PATH: 'C:\\a;C:\\tools' }, (p) => p === 'C:\\tools\\ffmpeg.exe', 'win32')).toBe('C:\\tools\\ffmpeg.exe');
    expect(whichSync('ffmpeg', { PATH: '/usr/bin:/opt/bin' }, (p) => p === '/opt/bin/ffmpeg', 'linux')).toBe('/opt/bin/ffmpeg');
  });
  it('FFMPEG_PATH may be a file or a folder; placeholders are ignored', () => {
    expect(resolveFfmpeg({ FFMPEG_PATH: '/opt/ff' }, (p) => p === '/opt/ff/ffmpeg', 'linux').path).toBe('/opt/ff/ffmpeg');
    expect(resolveFfmpeg({ FFMPEG_PATH: '${user_config.ffmpeg_path}', PATH: '' }, () => false, 'linux').source).toBe('not-found');
  });
  it('macOS finds Homebrew ffmpeg even without it on PATH', () => {
    expect(resolveFfmpeg({ PATH: '/usr/bin' }, (p) => p === '/opt/homebrew/bin/ffmpeg', 'darwin').path).toBe('/opt/homebrew/bin/ffmpeg');
  });
  it('cleanEnv / resolveSeconds', () => {
    expect(cleanEnv('  "C:\\Audacity"  ')).toBe('C:\\Audacity');
    expect(cleanEnv('${user_config.audacity_dir}')).toBeUndefined();
    expect(resolveSeconds('90', 10)).toBe(90);
    expect(resolveSeconds('abc', 10)).toBe(10);
  });
});

describe('formats and output paths', () => {
  it('normalises formats', () => {
    expect(normalizeFormat('AIF')).toBe('aiff');
    expect(normalizeFormat('vorbis')).toBe('ogg');
    expect(normalizeFormat('xyz')).toBeUndefined();
    expect(formatFromPath('/a/b.FLAC')).toBe('flac');
    expect(fileExtension('rf64')).toBe('wav');
    expect(needsFfmpeg('mp3')).toBe(true);
    expect(needsFfmpeg('opus')).toBe(false);
  });
  it('never overwrites and numbers new files', () => {
    const dir = path.resolve('/tmp/x');
    const existing = new Set([path.join(dir, 'a_fx.wav')]);
    const r = resolveOutputPath({ inputPath: path.join(dir, 'a.wav'), ext: 'wav', suffix: '_fx', exists: (p) => existing.has(p) });
    expect('path' in r && r.path).toBe(path.join(dir, 'a_fx_2.wav'));
    const e = resolveOutputPath({ inputPath: path.join(dir, 'a.wav'), ext: 'wav', outputPath: path.join(dir, 'a_fx.wav'), exists: (p) => existing.has(p) });
    expect('error' in e && e.error).toMatch(/already exists/);
    const rel = resolveOutputPath({ inputPath: path.join(dir, 'a.wav'), ext: 'wav', outputPath: 'out.wav' });
    expect('error' in rel && rel.error).toMatch(/absolute/);
  });
});
