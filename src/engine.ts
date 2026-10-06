import { spawn } from 'child_process';
import { randomUUID } from 'crypto';
import * as fs from 'fs/promises';
import * as os from 'os';
import * as path from 'path';
import { ENGINE_TIMEOUT_SEC, resolveEnginePath, resolveFfmpeg } from './config.js';
import { ffmpegEncodeArgs, isNativeInput, type OutputFormat } from './formats.js';

export interface EngineResult {
  success: boolean;
  error?: string;
  [key: string]: unknown;
}

let running = 0;
export function pendingRuns(): number {
  return running;
}

/** Runs one JSON job through aumcp-engine.exe (stdin -> stdout). */
export async function runEngine(job: Record<string, unknown>, timeoutSec = ENGINE_TIMEOUT_SEC): Promise<EngineResult> {
  const exe = resolveEnginePath();
  if (!exe) {
    return { success: false, error: 'Native engine (aumcp-engine.exe) not found next to the server. Reinstall the extension.' };
  }
  running++;
  try {
    return await new Promise<EngineResult>((resolve) => {
      const child = spawn(exe, [], { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
      const out: Buffer[] = [];
      const err: Buffer[] = [];
      const timer = setTimeout(() => {
        child.kill();
        resolve({ success: false, error: `Engine timed out after ${timeoutSec} s (AUDACITY_MCP_TIMEOUT)` });
      }, timeoutSec * 1000);
      child.stdout.on('data', (d: Buffer) => out.push(d));
      child.stderr.on('data', (d: Buffer) => err.push(d));
      child.on('error', (e) => {
        clearTimeout(timer);
        resolve({ success: false, error: `Cannot start engine: ${e.message}` });
      });
      child.on('close', (code) => {
        clearTimeout(timer);
        const text = Buffer.concat(out).toString('utf-8').trim();
        try {
          resolve(JSON.parse(text) as EngineResult);
        } catch {
          const stderr = Buffer.concat(err).toString('utf-8').trim();
          resolve({ success: false, error: `Engine failed (exit ${code}): ${stderr || text || 'no output'}` });
        }
      });
      child.stdin.end(JSON.stringify(job), 'utf-8');
    });
  } finally {
    running--;
  }
}

function runFfmpeg(args: string[], timeoutSec = ENGINE_TIMEOUT_SEC): Promise<{ ok: boolean; error?: string }> {
  const ff = resolveFfmpeg();
  if (!ff.path) return Promise.resolve({ ok: false, error: 'FFmpeg not found (set FFMPEG_PATH or add ffmpeg.exe to PATH)' });
  return new Promise((resolve) => {
    const child = spawn(ff.path as string, ['-hide_banner', '-loglevel', 'error', '-nostdin', ...args], {
      windowsHide: true,
      stdio: ['ignore', 'ignore', 'pipe'],
    });
    const err: Buffer[] = [];
    const timer = setTimeout(() => child.kill(), timeoutSec * 1000);
    child.stderr.on('data', (d: Buffer) => err.push(d));
    child.on('error', (e) => {
      clearTimeout(timer);
      resolve({ ok: false, error: e.message });
    });
    child.on('close', (code) => {
      clearTimeout(timer);
      resolve(code === 0 ? { ok: true } : { ok: false, error: Buffer.concat(err).toString('utf-8').trim() || `ffmpeg exit ${code}` });
    });
  });
}

export function tempDir(): string {
  return path.join(os.tmpdir(), 'audacity-mcp');
}

export async function tempPath(ext: string): Promise<string> {
  await fs.mkdir(tempDir(), { recursive: true });
  return path.join(tempDir(), `${randomUUID()}.${ext}`);
}

/** Decodes a file Audacity's codecs cannot read into a temporary 32-bit float WAV with FFmpeg. */
export async function decodeWithFfmpeg(input: string): Promise<string> {
  const out = await tempPath('wav');
  const r = await runFfmpeg(['-y', '-i', input, '-vn', '-c:a', 'pcm_f32le', '-f', 'wav', out]);
  if (!r.ok) {
    await fs.rm(out, { force: true });
    throw new Error(`FFmpeg could not decode ${input}: ${r.error}`);
  }
  return out;
}

export async function encodeWithFfmpeg(wav: string, output: string, format: OutputFormat, bitrateKbps?: number): Promise<void> {
  const r = await runFfmpeg(['-y', '-i', wav, ...ffmpegEncodeArgs(format, bitrateKbps), output]);
  if (!r.ok) throw new Error(`FFmpeg could not encode ${format}: ${r.error}`);
}

const UNREADABLE = /Unsupported or unreadable|Not a decodable/;

/**
 * Runs a job whose input(s) are named by `keys`, transparently decoding inputs that need FFmpeg
 * (M4A, AAC, WMA, WavPack...) to temporary WAV files first. Temporary files are always removed.
 */
export async function runWithInputs(
  job: Record<string, unknown>,
  inputs: string[],
  setInputs: (job: Record<string, unknown>, decoded: string[]) => Record<string, unknown>,
): Promise<EngineResult> {
  const temps: string[] = [];
  try {
    const decoded: string[] = [];
    for (const p of inputs) {
      if (isNativeInput(p) || !resolveFfmpeg().path) decoded.push(p);
      else {
        const t = await decodeWithFfmpeg(p);
        temps.push(t);
        decoded.push(t);
      }
    }
    let r = await runEngine(setInputs(job, decoded));
    if (!r.success && UNREADABLE.test(r.error ?? '') && resolveFfmpeg().path) {
      // Unusual codec inside a known container (e.g. WAV with MP3 data): let FFmpeg decode it.
      const retry: string[] = [];
      for (const p of decoded) {
        if (temps.includes(p)) retry.push(p);
        else {
          const t = await decodeWithFfmpeg(p);
          temps.push(t);
          retry.push(t);
        }
      }
      r = await runEngine(setInputs(job, retry));
    }
    return r;
  } finally {
    await Promise.all(temps.map((t) => fs.rm(t, { force: true }).catch(() => undefined)));
  }
}
