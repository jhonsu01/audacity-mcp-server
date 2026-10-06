import { existsSync } from 'fs';
import * as path from 'path';
import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { z } from 'zod';
import { ENGINE_TIMEOUT_SEC, resolveEnginePath, resolveFfmpeg } from '../config.js';
import { FFMPEG_INPUT_EXTENSIONS, NATIVE_INPUT_EXTENSIONS, OUTPUT_FORMATS, SAMPLE_FORMATS } from '../formats.js';
import { stemOf } from '../paths.js';
import { audacityRunning, audacityVersion, extensionStatus, installExtension, openInAudacity } from './audacity.js';
import {
  audioInfo, collectInputs, combineFiles, effectSchema, outputOptionsSchema, probe, processFile, splitFile,
} from './operations.js';

type ToolResult = { content: { type: 'text'; text: string }[]; isError?: boolean };

function json(obj: unknown, isError = false): ToolResult {
  const out: ToolResult = { content: [{ type: 'text', text: JSON.stringify(obj, null, 2) }] };
  if (isError) out.isError = true;
  return out;
}

function errorResult(e: unknown): ToolResult {
  return json({ success: false, error: e instanceof Error ? e.message : String(e) }, true);
}

function fromEngine(r: { success: boolean }): ToolResult {
  return json(r, !r.success);
}

const WRITE = { readOnlyHint: false, destructiveHint: false, idempotentHint: false, openWorldHint: false } as const;
const READ = { readOnlyHint: true, destructiveHint: false, idempotentHint: true, openWorldHint: false } as const;

const inputsSchema = {
  input_path: z.string().optional().describe('Absolute path of one audio file.'),
  input_paths: z.array(z.string()).max(500).optional().describe('Absolute paths of several audio files.'),
  input_dir: z.string().optional().describe('Absolute folder: every audio file directly inside it (non-recursive).'),
};

function inputsOf(a: { input_path?: string; input_paths?: string[]; input_dir?: string }): string[] {
  return collectInputs([...(a.input_path ? [a.input_path] : []), ...(a.input_paths ?? [])], a.input_dir);
}

export function registerTools(server: McpServer): void {
  server.registerTool(
    'split_audio',
    {
      title: 'Split audio into segments',
      description:
        'Cut one or many audio files into consecutive pieces of segment_seconds (default 15 s) using Audacity 4\'s audio engine. ' +
        'Every input gets its own folder of numbered pieces (<name>_001.wav, <name>_002.wav...). ' +
        'Output folder: output_dir/<input name>/ when output_dir is given (group_by_input), otherwise <input folder>/<input name>_segments/. ' +
        'Pieces keep the source format and resolution unless format/sample_format/sample_rate/channels say otherwise. ' +
        'Streams the audio, so very long recordings are fine. Returns every file with its start/end time.',
      inputSchema: {
        ...inputsSchema,
        segment_seconds: z.number().min(0.05).max(86400).default(15).describe('Length of each piece in seconds. The last piece can be shorter.'),
        output_dir: z.string().optional().describe('Absolute folder for the pieces (created if missing).'),
        group_by_input: z.boolean().default(true).describe('With output_dir: put each input\'s pieces in a subfolder named after the input.'),
        prefix: z.string().optional().describe('File name prefix. Default: the input file name.'),
        min_last_seconds: z
          .number()
          .min(0)
          .default(0)
          .describe('Drop the final piece when it is shorter than this (0 = always keep it).'),
        start: z.number().min(0).optional().describe('Only split from this time (seconds).'),
        end: z.number().min(0).optional().describe('Only split up to this time (seconds).'),
        ...outputOptionsSchema,
      },
      annotations: WRITE,
    },
    async (args) => {
      try {
        const inputs = inputsOf(args);
        const results: Record<string, unknown>[] = [];
        let ok = 0;
        let files = 0;
        for (const input of inputs) {
          const stem = stemOf(input);
          const outDir = args.output_dir
            ? args.group_by_input
              ? path.join(args.output_dir, stem)
              : args.output_dir
            : path.join(path.dirname(input), `${stem}_segments`);
          try {
            const r = await splitFile({
              input,
              outputDir: outDir,
              segmentSeconds: args.segment_seconds,
              prefix: args.prefix ? (inputs.length > 1 && !args.group_by_input ? `${args.prefix}_${stem}` : args.prefix) : undefined,
              minLastSeconds: args.min_last_seconds,
              start: args.start,
              end: args.end,
              output: args,
            });
            if (r.success) {
              ok++;
              files += Number(r['count'] ?? 0);
            }
            results.push(r);
          } catch (e) {
            results.push({ success: false, input, error: e instanceof Error ? e.message : String(e) });
          }
        }
        return json(
          { success: ok === inputs.length, inputs: inputs.length, succeeded: ok, total_files: files, results },
          ok === 0,
        );
      } catch (e) {
        return errorResult(e);
      }
    },
  );

  server.registerTool(
    'convert_audio',
    {
      title: 'Convert audio format',
      description:
        'Convert one or many audio files to another format and/or sample rate, bit depth or channel count. ' +
        'Reads anything Audacity 4 imports natively (WAV, AIFF, FLAC, OGG, Opus, MP3, CAF, W64, AU...) plus M4A/AAC/WMA/WavPack via FFmpeg. ' +
        'Writes wav/flac/ogg/opus/aiff/caf/w64/rf64/au (Audacity\'s libsndfile) or mp3/m4a/aac/wma (FFmpeg). ' +
        'Use float32 WAV for further editing without loss.',
      inputSchema: {
        ...inputsSchema,
        output_path: z.string().optional().describe('Absolute output file (single input only).'),
        output_dir: z.string().optional().describe('Absolute folder for the results. Default: next to each input.'),
        ...outputOptionsSchema,
      },
      annotations: WRITE,
    },
    async (args) => {
      try {
        const inputs = inputsOf(args);
        if (args.output_path && inputs.length > 1) throw new Error('output_path only works with one input; use output_dir.');
        const results = [];
        let ok = 0;
        for (const input of inputs) {
          try {
            const r = await processFile({ input, outputPath: args.output_path, outputDir: args.output_dir, suffix: '', output: args });
            if (r.success) ok++;
            results.push(r);
          } catch (e) {
            results.push({ success: false, input, error: e instanceof Error ? e.message : String(e) });
          }
        }
        if (inputs.length === 1) return json(results[0], !ok);
        return json({ success: ok === inputs.length, total: inputs.length, succeeded: ok, results }, ok === 0);
      } catch (e) {
        return errorResult(e);
      }
    },
  );

  server.registerTool(
    'trim_audio',
    {
      title: 'Trim audio',
      description:
        'Keep only the part between start and end (seconds) of an audio file, optionally with fade in/out, and save it as a new file ' +
        '(default: <name>_trim.<ext> next to the input).',
      inputSchema: {
        input_path: z.string().describe('Absolute path of the audio file.'),
        start: z.number().min(0).default(0).describe('Start time in seconds.'),
        end: z.number().min(0).optional().describe('End time in seconds. Default: end of the file.'),
        fade_in: z.number().min(0).optional().describe('Fade-in length in seconds.'),
        fade_out: z.number().min(0).optional().describe('Fade-out length in seconds.'),
        output_path: z.string().optional().describe('Absolute output file.'),
        ...outputOptionsSchema,
      },
      annotations: WRITE,
    },
    async (args) => {
      try {
        const effects: Record<string, unknown>[] = [];
        if (args.fade_in) effects.push({ type: 'fade_in', seconds: args.fade_in });
        if (args.fade_out) effects.push({ type: 'fade_out', seconds: args.fade_out });
        return fromEngine(
          await processFile({
            input: args.input_path,
            outputPath: args.output_path,
            suffix: '_trim',
            start: args.start,
            end: args.end,
            effects,
            output: args,
          }),
        );
      } catch (e) {
        return errorResult(e);
      }
    },
  );

  server.registerTool(
    'apply_effects',
    {
      title: 'Apply audio effects',
      description:
        'Apply a chain of effects, in order, to an audio file and save the result as a new file (default <name>_fx.<ext>). ' +
        'Effects: gain{db}, normalize{peak_db=-1, per_channel}, fade_in{seconds, curve}, fade_out{seconds, curve}, trim{start,end}, ' +
        'pad{start,end}, trim_silence{threshold_db=-50, padding=0.1}, reverse, speed{factor}, invert, remove_dc, ' +
        'highpass{frequency,q}, lowpass{frequency,q}, bandpass{frequency,q}, notch{frequency,q}, eq{frequency,gain_db,q}, ' +
        'bass{gain_db,frequency}, treble{gain_db,frequency}, echo{delay,decay}, compressor{threshold_db,ratio,attack,release,makeup_db}, ' +
        'limiter{ceiling_db,release}, noise_gate{threshold_db,attack,release,floor_db}, mono, stereo, swap_channels, pan{value}. ' +
        'Curves: linear, exponential, logarithmic, scurve. Example: [{"type":"highpass","frequency":80},{"type":"normalize","peak_db":-1}].',
      inputSchema: {
        input_path: z.string().describe('Absolute path of the audio file.'),
        effects: z.array(effectSchema).min(1).max(50).describe('Effects applied in order.'),
        start: z.number().min(0).optional().describe('Only process (and keep) from this time.'),
        end: z.number().min(0).optional().describe('Only process (and keep) up to this time.'),
        output_path: z.string().optional().describe('Absolute output file.'),
        ...outputOptionsSchema,
      },
      annotations: WRITE,
    },
    async (args) => {
      try {
        return fromEngine(
          await processFile({
            input: args.input_path,
            outputPath: args.output_path,
            suffix: '_fx',
            start: args.start,
            end: args.end,
            effects: args.effects as Record<string, unknown>[],
            output: args,
          }),
        );
      } catch (e) {
        return errorResult(e);
      }
    },
  );

  const pieceSchema = z.object({
    path: z.string().describe('Absolute path of the audio file.'),
    gain_db: z.number().optional().describe('Volume change for this input.'),
    offset_seconds: z.number().min(0).optional().describe('mix only: start this input at this time.'),
    start: z.number().min(0).optional().describe('Use the input from this time.'),
    end: z.number().min(0).optional().describe('Use the input up to this time.'),
  });

  server.registerTool(
    'mix_audio',
    {
      title: 'Mix audio files',
      description:
        'Mix several audio files on top of each other (like several Audacity tracks rendered together), each with its own gain and start offset. ' +
        'Different sample rates/channel counts are converted automatically. Use normalize to avoid clipping.',
      inputSchema: {
        inputs: z.array(pieceSchema).min(1).max(64),
        output_path: z.string().describe('Absolute output file.'),
        normalize: z.boolean().default(false).describe('Normalize the mix peak to normalize_peak_db.'),
        normalize_peak_db: z.number().max(0).default(-1),
        ...outputOptionsSchema,
      },
      annotations: WRITE,
    },
    async (args) => {
      try {
        return fromEngine(
          await combineFiles({
            command: 'mix',
            inputs: args.inputs,
            outputPath: args.output_path,
            output: args,
            extra: { normalize: args.normalize, normalize_peak_db: args.normalize_peak_db },
          }),
        );
      } catch (e) {
        return errorResult(e);
      }
    },
  );

  server.registerTool(
    'concat_audio',
    {
      title: 'Join audio files',
      description: 'Join audio files one after another into a single file, with an optional crossfade or a silent gap between them.',
      inputSchema: {
        inputs: z.array(z.union([z.string(), pieceSchema])).min(1).max(500).describe('Files in order (paths or {path,start,end,gain_db}).'),
        output_path: z.string().describe('Absolute output file.'),
        crossfade_seconds: z.number().min(0).default(0).describe('Equal-power crossfade between consecutive files.'),
        gap_seconds: z.number().min(0).default(0).describe('Silence between files (ignored when crossfading).'),
        normalize: z.boolean().default(false),
        normalize_peak_db: z.number().max(0).default(-1),
        ...outputOptionsSchema,
      },
      annotations: WRITE,
    },
    async (args) => {
      try {
        return fromEngine(
          await combineFiles({
            command: 'concat',
            inputs: args.inputs.map((i) => (typeof i === 'string' ? { path: i } : i)),
            outputPath: args.output_path,
            output: args,
            extra: {
              crossfade_seconds: args.crossfade_seconds,
              gap_seconds: args.gap_seconds,
              normalize: args.normalize,
              normalize_peak_db: args.normalize_peak_db,
            },
          }),
        );
      } catch (e) {
        return errorResult(e);
      }
    },
  );

  server.registerTool(
    'get_audio_info',
    {
      title: 'Audio file info',
      description: 'Format, sample rate, channels, bit depth and duration of an audio file. analyze=true also measures peak and RMS level (dBFS).',
      inputSchema: {
        input_path: z.string().describe('Absolute path of the audio file.'),
        analyze: z.boolean().default(false).describe('Read the whole file to measure peak and RMS level.'),
      },
      annotations: READ,
    },
    async (args) => {
      try {
        return fromEngine(await audioInfo(args.input_path, args.analyze));
      } catch (e) {
        return errorResult(e);
      }
    },
  );

  server.registerTool(
    'open_in_audacity',
    {
      title: 'Open in Audacity',
      description: 'Open audio files (or .aup4 projects) in the Audacity 4 editor for manual editing. Each file opens as a project.',
      inputSchema: {
        paths: z.array(z.string()).min(1).max(20).describe('Absolute paths of the files to open.'),
      },
      annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: false, openWorldHint: false },
    },
    async (args) => {
      try {
        for (const p of args.paths) {
          if (!path.win32.isAbsolute(p) || !existsSync(p)) throw new Error(`File not found or not absolute: ${p}`);
        }
        const st = await probe();
        const exe = st['audacity_exe'] as string | undefined;
        if (!exe) throw new Error('Audacity 4 not found. Set AUDACITY_DIR to its install folder.');
        await openInAudacity(exe, args.paths);
        return json({ success: true, audacity: exe, opened: args.paths });
      } catch (e) {
        return errorResult(e);
      }
    },
  );

  server.registerTool(
    'install_audacity_extension',
    {
      title: 'Install the Audacity extension',
      description:
        'Install (or update / remove) the "MCP Audio Tools" extension inside Audacity 4. It adds effects to Audacity itself: ' +
        'Split into Files, Export Selection to File, Import Audio File as Track (Tools menu) and Labels Every N Seconds (Analyze menu). ' +
        'Restart Audacity afterwards.',
      inputSchema: {
        uninstall: z.boolean().default(false).describe('Remove the extension instead of installing it.'),
      },
      annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: true, openWorldHint: false },
    },
    async (args) => {
      try {
        return json(await installExtension(args.uninstall));
      } catch (e) {
        return errorResult(e);
      }
    },
  );

  server.registerTool(
    'get_audacity_status',
    {
      title: 'Audacity status',
      description:
        'Check the Audacity 4 installation and the audio engine: Audacity path and version, codec library versions, MP3 decoding, ' +
        'FFmpeg availability, whether the in-app extension is installed, supported input/output formats and the effect catalog.',
      inputSchema: {},
      annotations: READ,
    },
    async () => {
      const st = await probe();
      const exe = (st['audacity_exe'] as string | undefined) || '';
      const ff = resolveFfmpeg();
      return json(
        {
          ready: st.success,
          error: st.error ?? (st['sndfile_error'] || undefined),
          audacity_exe: exe || null,
          audacity_version: (await audacityVersion(exe)) ?? null,
          audacity_running: await audacityRunning(),
          audio_engine: {
            engine: resolveEnginePath() ?? null,
            libsndfile: st['sndfile_version'] ?? null,
            mp3_decoder: st['mp3_decoder'] ?? false,
            codecs_from: st['audacity_bin_dir'] ?? null,
          },
          ffmpeg: ff.path ?? null,
          extension: extensionStatus(),
          input_formats: { native: NATIVE_INPUT_EXTENSIONS, via_ffmpeg: FFMPEG_INPUT_EXTENSIONS },
          output_formats: OUTPUT_FORMATS,
          sample_formats: SAMPLE_FORMATS,
          sample_formats_by_format: st['output_formats'] ?? null,
          effects: st['effects'] ?? null,
          timeout_sec: ENGINE_TIMEOUT_SEC,
          hint: st.success ? undefined : 'Install Audacity 4 or set AUDACITY_DIR to its install folder.',
        },
        !st.success,
      );
    },
  );
}
