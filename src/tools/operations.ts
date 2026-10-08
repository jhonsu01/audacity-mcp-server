import { existsSync, readdirSync, statSync } from 'fs';
import * as fs from 'fs/promises';
import * as path from 'path';
import { z } from 'zod';
import { resolveFfmpeg } from '../config.js';
import { encodeWithFfmpeg, runEngine, runWithInputs, tempDir, tempPath, type EngineResult } from '../engine.js';
import {
  OUTPUT_FORMATS, SAMPLE_FORMATS, fileExtension, formatFromPath, isKnownInput, isNativeInput, needsFfmpeg, normalizeFormat,
  type OutputFormat,
} from '../formats.js';
import { resolveOutputPath, stemOf } from '../paths.js';

/** Export options shared by every tool that writes audio. */
export const outputOptionsSchema = {
  format: z
    .string()
    .optional()
    .describe(
      `Output format: ${OUTPUT_FORMATS.join(', ')}. Default: from output_path's extension, else the input's format, else wav. ` +
        'wav/flac/ogg/opus/aiff/caf/w64/rf64/au are written by Audacity\'s own libsndfile; mp3/m4a/aac/wma need FFmpeg.',
    ),
  sample_format: z
    .enum(SAMPLE_FORMATS)
    .default('auto')
    .describe('Bit depth for PCM formats: pcm16, pcm24, pcm32, float32 (lossless editing headroom), float64, pcm8. auto = keep the source resolution when the format allows it.'),
  sample_rate: z.number().int().min(1000).max(384000).optional().describe('Resample to this rate in Hz (high-quality windowed-sinc). Default: keep.'),
  channels: z.union([z.literal(1), z.literal(2)]).optional().describe('1 = mix down to mono, 2 = stereo. Default: keep.'),
  quality: z.number().min(0).max(1).optional().describe('OGG Vorbis / Opus quality 0..1 (default ~0.4).'),
  compression: z.number().min(0).max(1).optional().describe('FLAC compression level 0..1 (only size/speed; always lossless).'),
  bitrate_kbps: z.number().int().min(32).max(512).optional().describe('MP3/M4A/AAC/WMA bitrate. Default: MP3 VBR ~190 kbps, others 192 kbps.'),
  overwrite: z.boolean().default(false).describe('Replace existing files. Without it, files are never overwritten.'),
};
const outputObject = z.object(outputOptionsSchema);
export type OutputOptions = z.infer<typeof outputObject>;

export const effectSchema = z
  .object({ type: z.string().describe('Effect name, e.g. gain, normalize, fade_in, highpass, compressor.') })
  .catchall(z.union([z.number(), z.string(), z.boolean()]))
  .describe('One effect: {"type":"gain","db":-3}. Parameters depend on the effect (see get_audacity_status).');

function engineOutput(o: OutputOptions, format: OutputFormat) {
  const ff = needsFfmpeg(format);
  return {
    format: ff ? 'wav' : format,
    sample_format: ff ? 'float32' : o.sample_format,
    sample_rate: o.sample_rate ?? 0,
    channels: o.channels ?? 0,
    quality: o.quality ?? -1,
    compression: o.compression ?? -1,
  };
}

function requireFfmpeg(format: OutputFormat): void {
  if (needsFfmpeg(format) && !resolveFfmpeg().path) {
    throw new Error(`Exporting ${format} needs FFmpeg (not found). Install FFmpeg or set FFMPEG_PATH, or use flac/ogg/opus/wav.`);
  }
}

export function checkInput(p: string): void {
  if (!path.isAbsolute(p)) throw new Error(`Input must be an absolute path: ${p}`);
  if (!existsSync(p) || !statSync(p).isFile()) throw new Error(`Input file not found: ${p}`);
  if (!isKnownInput(p) && !resolveFfmpeg().path) {
    throw new Error(`Unrecognised audio extension: ${p}`);
  }
  if (!isNativeInput(p) && !resolveFfmpeg().path) {
    throw new Error(`${path.extname(p)} files need FFmpeg to be decoded (not found). Set FFMPEG_PATH.`);
  }
}

/** Expands input_paths + input_dir (non-recursive, audio files only). */
export function collectInputs(inputPaths: string[] | undefined, inputDir: string | undefined, max = 500): string[] {
  let inputs = [...(inputPaths ?? [])];
  if (inputDir) {
    if (!path.isAbsolute(inputDir) || !existsSync(inputDir)) throw new Error(`input_dir not found or not absolute: ${inputDir}`);
    inputs = inputs.concat(
      readdirSync(inputDir)
        .map((n) => path.join(inputDir, n))
        .filter((p) => statSync(p).isFile() && isKnownInput(p))
        .sort(),
    );
  }
  if (inputs.length === 0) throw new Error('No input audio: give input_path(s) or an input_dir containing audio files.');
  if (inputs.length > max) throw new Error(`At most ${max} files per call.`);
  return inputs;
}

function chooseFormat(o: OutputOptions, outputPath: string | undefined, input: string): OutputFormat {
  const explicit = normalizeFormat(o.format);
  if (o.format && !explicit) throw new Error(`Unsupported output format "${o.format}". Use one of: ${OUTPUT_FORMATS.join(', ')}`);
  if (explicit) return explicit;
  if (outputPath) {
    const f = formatFromPath(outputPath);
    if (f) return f;
  }
  const fromInput = formatFromPath(input);
  return fromInput && !needsFfmpeg(fromInput) ? fromInput : 'wav';
}

async function finishFfmpeg(r: EngineResult, finalPath: string, format: OutputFormat, o: OutputOptions): Promise<EngineResult> {
  const res = r['result'] as { path: string; bytes: number; output: Record<string, unknown> } | undefined;
  if (!r.success || !res) return r;
  try {
    await encodeWithFfmpeg(res.path, finalPath, format, o.bitrate_kbps);
  } finally {
    await fs.rm(res.path, { force: true });
  }
  const bytes = (await fs.stat(finalPath)).size;
  return { ...r, result: { ...res, path: finalPath, bytes, output: { ...res.output, format, sample_format: 'compressed', encoder: 'ffmpeg' } } };
}

/** Trim + effects + format conversion of one file. */
export async function processFile(args: {
  input: string;
  outputPath?: string;
  outputDir?: string;
  suffix: string;
  start?: number;
  end?: number;
  effects?: Record<string, unknown>[];
  output: OutputOptions;
}): Promise<EngineResult> {
  checkInput(args.input);
  const format = chooseFormat(args.output, args.outputPath, args.input);
  requireFfmpeg(format);
  const resolved = resolveOutputPath({
    inputPath: args.input,
    ext: fileExtension(format),
    suffix: args.suffix,
    outputPath: args.outputPath,
    outputDir: args.outputDir,
    overwrite: args.output.overwrite,
  });
  if ('error' in resolved) throw new Error(resolved.error);
  if (args.outputDir) await fs.mkdir(args.outputDir, { recursive: true });
  const ff = needsFfmpeg(format);
  const enginePath = ff ? await tempPath('wav') : resolved.path;
  const job = {
    command: 'process',
    input: args.input,
    output_path: enginePath,
    start: args.start ?? 0,
    end: args.end ?? 0,
    effects: args.effects ?? [],
    output: engineOutput(args.output, format),
    overwrite: ff ? true : args.output.overwrite,
  };
  const r = await runWithInputs(job, [args.input], (j, d) => ({ ...j, input: d[0] }));
  const final = ff ? await finishFfmpeg(r, resolved.path, format, args.output) : r;
  return { ...final, input: args.input };
}

/** Splits one file into consecutive segments in outputDir. */
export async function splitFile(args: {
  input: string;
  outputDir: string;
  segmentSeconds: number;
  prefix?: string;
  minLastSeconds: number;
  start?: number;
  end?: number;
  output: OutputOptions;
}): Promise<EngineResult> {
  checkInput(args.input);
  const format = chooseFormat(args.output, undefined, args.input);
  requireFfmpeg(format);
  if (!path.isAbsolute(args.outputDir)) throw new Error(`output_dir must be absolute: ${args.outputDir}`);
  const ff = needsFfmpeg(format);
  const engineDir = ff ? path.join(tempDir(), `split-${Date.now()}-${Math.random().toString(36).slice(2)}`) : args.outputDir;
  const job = {
    command: 'split',
    input: args.input,
    output_dir: engineDir,
    segment_seconds: args.segmentSeconds,
    prefix: args.prefix ?? stemOf(args.input),
    min_last_seconds: args.minLastSeconds,
    start: args.start ?? 0,
    end: args.end ?? 0,
    output: engineOutput(args.output, format),
    overwrite: ff ? true : args.output.overwrite,
  };
  const r = await runWithInputs(job, [args.input], (j, d) => ({ ...j, input: d[0] }));
  if (!ff || !r.success) return { ...r, input: args.input };
  // Encode each temporary WAV segment with FFmpeg into the real output folder.
  try {
    await fs.mkdir(args.outputDir, { recursive: true });
    const files = r['files'] as { path: string; start_seconds: number; end_seconds: number }[];
    const out: unknown[] = [];
    let total = 0;
    for (const f of files) {
      const dest = path.join(args.outputDir, path.basename(f.path).replace(/\.wav$/i, `.${fileExtension(format)}`));
      if (existsSync(dest) && !args.output.overwrite) throw new Error(`File already exists (set overwrite: true to replace it): ${dest}`);
      await encodeWithFfmpeg(f.path, dest, format, args.output.bitrate_kbps);
      const bytes = (await fs.stat(dest)).size;
      total += bytes;
      out.push({ ...f, path: dest, bytes });
    }
    return {
      ...r,
      input: args.input,
      output_dir: args.outputDir,
      output: { ...(r['output'] as object), format, sample_format: 'compressed', encoder: 'ffmpeg' },
      files: out,
      total_bytes: total,
    };
  } finally {
    await fs.rm(engineDir, { recursive: true, force: true });
  }
}

/** mix or concat of several inputs into one file. */
export async function combineFiles(args: {
  command: 'mix' | 'concat';
  inputs: { path: string; gain_db?: number; offset_seconds?: number; start?: number; end?: number }[];
  outputPath: string;
  output: OutputOptions;
  extra: Record<string, unknown>;
}): Promise<EngineResult> {
  for (const i of args.inputs) checkInput(i.path);
  const format = chooseFormat(args.output, args.outputPath, args.inputs[0]!.path);
  requireFfmpeg(format);
  const resolved = resolveOutputPath({
    inputPath: args.inputs[0]!.path,
    ext: fileExtension(format),
    outputPath: args.outputPath,
    overwrite: args.output.overwrite,
  });
  if ('error' in resolved) throw new Error(resolved.error);
  const ff = needsFfmpeg(format);
  const enginePath = ff ? await tempPath('wav') : resolved.path;
  const job = {
    command: args.command,
    inputs: args.inputs,
    output_path: enginePath,
    output: engineOutput(args.output, format),
    overwrite: ff ? true : args.output.overwrite,
    ...args.extra,
  };
  const r = await runWithInputs(
    job,
    args.inputs.map((i) => i.path),
    (j, d) => ({ ...j, inputs: args.inputs.map((i, k) => ({ ...i, path: d[k] })) }),
  );
  return ff ? finishFfmpeg(r, resolved.path, format, args.output) : r;
}

export async function audioInfo(input: string, analyze: boolean): Promise<EngineResult> {
  checkInput(input);
  const r = await runWithInputs({ command: 'info', input, analyze }, [input], (j, d) => ({ ...j, input: d[0] }));
  return { ...r, input };
}

export async function probe(): Promise<EngineResult> {
  return runEngine({ command: 'probe' }, 30);
}
