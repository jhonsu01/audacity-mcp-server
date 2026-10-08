import { existsSync } from 'fs';
import * as path from 'path';

/**
 * Output path rules:
 *  - explicit path: must be absolute; the extension is added when missing and must match the format;
 *    an existing file is only replaced with overwrite=true.
 *  - no path: in outputDir (default: next to the input) as <input name><suffix>.<ext>, adding _2, _3...
 *    instead of replacing anything.
 */
export function resolveOutputPath(opts: {
  inputPath: string;
  ext: string;
  suffix?: string;
  outputPath?: string;
  outputDir?: string;
  overwrite?: boolean;
  exists?: (p: string) => boolean;
}): { path: string } | { error: string } {
  const exists = opts.exists ?? existsSync;
  const p = path;
  if (opts.outputPath) {
    if (!p.isAbsolute(opts.outputPath)) return { error: `output_path must be an absolute path: ${opts.outputPath}` };
    const cur = p.extname(opts.outputPath).replace(/^\./, '').toLowerCase();
    let out = opts.outputPath;
    if (!cur) out = `${opts.outputPath}.${opts.ext}`;
    else if (!sameExt(cur, opts.ext)) return { error: `output_path has extension ".${cur}" but the format is "${opts.ext}".` };
    if (exists(out) && !opts.overwrite) return { error: `File already exists (set overwrite: true to replace it): ${out}` };
    if (sameFile(out, opts.inputPath)) return { error: 'output_path must be different from the input file.' };
    return { path: out };
  }
  const dir = opts.outputDir ?? p.dirname(opts.inputPath);
  if (!p.isAbsolute(dir)) return { error: `output_dir must be an absolute path: ${dir}` };
  const stem = p.basename(opts.inputPath).replace(/\.[^.]+$/, '') + (opts.suffix ?? '');
  let candidate = p.join(dir, `${stem}.${opts.ext}`);
  for (let i = 2; (exists(candidate) && !opts.overwrite) || sameFile(candidate, opts.inputPath); i++) {
    candidate = p.join(dir, `${stem}_${i}.${opts.ext}`);
  }
  return { path: candidate };
}

function sameExt(a: string, b: string): boolean {
  const norm = (e: string) => ({ aif: 'aiff', wave: 'wav', oga: 'ogg' })[e] ?? e;
  return norm(a) === norm(b);
}

export function sameFile(a: string, b: string): boolean {
  // Windows and macOS file systems are case-insensitive by default; Linux is not.
  const fold = (s: string) => (process.platform === 'linux' ? s : s.toLowerCase());
  return fold(path.resolve(a)) === fold(path.resolve(b));
}

/** File-name-safe stem of a path (used for per-input split folders). */
export function stemOf(p: string): string {
  return path.basename(p).replace(/\.[^.]+$/, '');
}
