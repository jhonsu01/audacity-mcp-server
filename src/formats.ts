import * as path from 'path';

/** Written by Audacity's own libsndfile through the native engine. */
export const NATIVE_OUTPUT_FORMATS = ['wav', 'flac', 'ogg', 'opus', 'aiff', 'caf', 'w64', 'rf64', 'au'] as const;
/** Need FFmpeg (Audacity 4 itself also exports these through FFmpeg/LAME). */
export const FFMPEG_OUTPUT_FORMATS = ['mp3', 'm4a', 'aac', 'wma'] as const;
export const OUTPUT_FORMATS = [...NATIVE_OUTPUT_FORMATS, ...FFMPEG_OUTPUT_FORMATS] as const;
export type OutputFormat = (typeof OUTPUT_FORMATS)[number];

export const SAMPLE_FORMATS = ['auto', 'pcm8', 'pcm16', 'pcm24', 'pcm32', 'float32', 'float64'] as const;

/** Read natively (libsndfile / mpg123 from Audacity 4). */
export const NATIVE_INPUT_EXTENSIONS = [
  'wav', 'wave', 'aif', 'aiff', 'aifc', 'flac', 'ogg', 'oga', 'opus', 'mp3', 'mp2', 'mpga', 'caf', 'w64', 'rf64', 'au', 'snd',
  'voc', 'paf', 'sd2', 'iff', 'svx', 'sf', 'avr', 'htk', 'xi', 'mat', 'pvf', 'sds', 'wve',
];
/** Decoded through FFmpeg first when it is available. */
export const FFMPEG_INPUT_EXTENSIONS = ['m4a', 'aac', 'mp4', 'wma', 'wv', 'ac3', 'amr', 'webm', 'mka', 'mkv', 'mov', 'ape', 'tta', '3gp'];

const EXT_BY_FORMAT: Record<string, string> = { rf64: 'wav', aiff: 'aiff' };

export function extensionOf(p: string): string {
  return path.win32.extname(p).replace(/^\./, '').toLowerCase();
}

export function isNativeInput(p: string): boolean {
  return NATIVE_INPUT_EXTENSIONS.includes(extensionOf(p));
}

export function isKnownInput(p: string): boolean {
  const e = extensionOf(p);
  return NATIVE_INPUT_EXTENSIONS.includes(e) || FFMPEG_INPUT_EXTENSIONS.includes(e);
}

export function normalizeFormat(f: string | undefined): OutputFormat | undefined {
  if (!f) return undefined;
  const v = f.trim().toLowerCase().replace(/^\./, '');
  const alias: Record<string, string> = { aif: 'aiff', wave: 'wav', oga: 'ogg', vorbis: 'ogg', mp4: 'm4a' };
  const n = alias[v] ?? v;
  return (OUTPUT_FORMATS as readonly string[]).includes(n) ? (n as OutputFormat) : undefined;
}

export function formatFromPath(p: string): OutputFormat | undefined {
  return normalizeFormat(extensionOf(p));
}

export function fileExtension(format: OutputFormat): string {
  return EXT_BY_FORMAT[format] ?? format;
}

export function needsFfmpeg(format: OutputFormat): boolean {
  return (FFMPEG_OUTPUT_FORMATS as readonly string[]).includes(format);
}

/** FFmpeg encoder arguments for the formats libsndfile cannot write. */
export function ffmpegEncodeArgs(format: OutputFormat, bitrateKbps: number | undefined): string[] {
  switch (format) {
    case 'mp3':
      return bitrateKbps ? ['-c:a', 'libmp3lame', '-b:a', `${bitrateKbps}k`] : ['-c:a', 'libmp3lame', '-q:a', '2'];
    case 'm4a':
    case 'aac':
      return ['-c:a', 'aac', '-b:a', `${bitrateKbps ?? 192}k`];
    case 'wma':
      return ['-c:a', 'wmav2', '-b:a', `${bitrateKbps ?? 192}k`];
    default:
      return [];
  }
}
