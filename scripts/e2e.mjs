// End-to-end test over real MCP stdio against dist/bundle.cjs (what the .mcpb runs).
// Needs Windows + Audacity 4 installed. Usage: npm run build && npm run test:e2e
// E2E_BUNDLE=<path to bundle.cjs> tests another build (e.g. the contents of an unpacked .mcpb).
// Uses synthetic tones only. The per-user data folder (LOCALAPPDATA / HOME / XDG_DATA_HOME) is redirected so
// the real Audacity extensions folder is untouched. Runs on Windows, macOS and Linux.
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';
import { existsSync, mkdtempSync, readdirSync, statSync, writeFileSync } from 'fs';
import { tmpdir } from 'os';
import { join } from 'path';

const out = mkdtempSync(join(tmpdir(), 'aud-e2e-'));
const fakeHome = join(out, 'home');
const fakeEnv = process.platform === 'win32'
  ? { LOCALAPPDATA: join(fakeHome, 'AppData', 'Local') }
  : process.platform === 'darwin' ? { HOME: fakeHome } : { HOME: fakeHome, XDG_DATA_HOME: join(fakeHome, '.local', 'share') };
const extRoot = process.platform === 'win32'
  ? join(fakeHome, 'AppData', 'Local', 'Audacity', 'Audacity4', 'extensions')
  : process.platform === 'darwin'
    ? join(fakeHome, 'Library', 'Application Support', 'Audacity', 'Audacity4', 'extensions')
    : join(fakeHome, '.local', 'share', 'Audacity', 'Audacity4', 'extensions');
const nativeLib = process.platform === 'win32' ? join('platform', 'windows', 'x86_64', 'audacity_mcp_native.dll')
  : process.platform === 'darwin' ? join('platform', 'macos', 'universal', 'audacity_mcp_native.dylib')
    : join('platform', 'linux', process.arch === 'arm64' ? 'arm64' : 'x86_64', 'audacity_mcp_native.so');

// 40 s stereo 44.1 kHz 16-bit tone in a folder with non-ASCII characters
function writeTone(path, seconds, freq, rate = 44100) {
  const frames = Math.round(seconds * rate);
  const buf = Buffer.alloc(44 + frames * 4);
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + frames * 4, 4); buf.write('WAVE', 8);
  buf.write('fmt ', 12); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20); buf.writeUInt16LE(2, 22);
  buf.writeUInt32LE(rate, 24); buf.writeUInt32LE(rate * 4, 28); buf.writeUInt16LE(4, 32); buf.writeUInt16LE(16, 34);
  buf.write('data', 36); buf.writeUInt32LE(frames * 4, 40);
  for (let i = 0; i < frames; i++) {
    const s = Math.round(Math.sin((2 * Math.PI * freq * i) / rate) * 0.5 * 32767);
    buf.writeInt16LE(s, 44 + i * 4); buf.writeInt16LE(s, 46 + i * 4);
  }
  writeFileSync(path, buf);
}
const srcDir = join(out, 'Señal ñandú');
const { mkdirSync } = await import('fs');
mkdirSync(srcDir, { recursive: true });
const toneA = join(srcDir, 'toneA.wav');
const toneB = join(srcDir, 'toneB.wav');
writeTone(toneA, 40, 440);
writeTone(toneB, 20, 660);

const client = new Client({ name: 'e2e', version: '1.0.0' });
await client.connect(new StdioClientTransport({
  command: process.execPath,
  args: [process.env.E2E_BUNDLE || 'dist/bundle.cjs'],
  env: { ...process.env, ...fakeEnv },
}));

let failed = 0;
const check = (cond, msg) => { console.log(`${cond ? 'PASS' : 'FAIL'}  ${msg}`); if (!cond) failed++; };
const parse = (res) => JSON.parse(res.content.find((c) => c.type === 'text').text);
const call = async (name, args) => parse(await client.callTool({ name, arguments: args }));

const { tools } = await client.listTools();
check(tools.length === 10, `10 tools exposed (${tools.map((t) => t.name).join(', ')})`);

const status = await call('get_audacity_status', {});
check(status.ready && /libsndfile/.test(status.audio_engine.libsndfile), `engine ready on ${status.platform}: ${status.audio_engine.libsndfile} (${status.audio_engine.codecs_source}: ${status.audio_engine.libsndfile_path}), Audacity ${status.audacity_version ?? 'not installed'}`);
check(status.effects?.length >= 30, `${status.effects?.length} effects in catalog`);

const info = await call('get_audio_info', { input_path: toneA, analyze: true });
check(info.success && Math.abs(info.info.duration_seconds - 40) < 0.01 && Math.abs(info.peak_dbfs + 6.02) < 0.1, `info: ${info.info?.duration_seconds} s, peak ${info.peak_dbfs?.toFixed(2)} dBFS`);

const split = await call('split_audio', { input_paths: [toneA, toneB], segment_seconds: 15, output_dir: join(out, 'split') });
const a = split.results?.[0];
const b = split.results?.[1];
check(split.success && a.count === 3 && b.count === 2, `split 40 s + 20 s by 15 s -> ${a?.count} + ${b?.count} pieces`);
check(existsSync(join(out, 'split', 'toneA', 'toneA_001.wav')) && existsSync(join(out, 'split', 'toneB', 'toneB_002.wav')), 'one folder per input with numbered pieces');
check(Math.abs(a.files[2].end_seconds - a.files[2].start_seconds - 10) < 0.001, `last piece 10 s (${(a.files[2].end_seconds - a.files[2].start_seconds).toFixed(3)})`);
const again = await call('split_audio', { input_path: toneA, segment_seconds: 15, output_dir: join(out, 'split') });
check(!again.success && /already exists/.test(again.results[0].error), 'split refuses to overwrite without overwrite:true');
const dropped = await call('split_audio', { input_path: toneA, segment_seconds: 15, min_last_seconds: 12, output_dir: join(out, 'split2'), format: 'flac' });
check(dropped.success && dropped.results[0].count === 2 && dropped.results[0].output.format === 'flac', 'min_last_seconds drops the short tail; FLAC pieces');

for (const [format, extra] of [['flac', { sample_format: 'pcm24' }], ['ogg', { quality: 0.5 }], ['opus', {}], ['aiff', {}], ['wav', { sample_format: 'float32', sample_rate: 48000, channels: 1 }]]) {
  const r = await call('convert_audio', { input_path: toneB, output_dir: join(out, 'conv'), format, ...extra });
  check(r.success && existsSync(r.result.path) && r.result.output.format === format, `convert -> ${format} ${r.result?.output?.sample_format} ${r.result?.output?.sample_rate} Hz ${r.result?.output?.channels} ch (${r.result?.bytes} B)`);
}
if (status.ffmpeg) {
  const mp3 = await call('convert_audio', { input_path: toneB, output_dir: join(out, 'conv'), format: 'mp3', bitrate_kbps: 192 });
  check(mp3.success && existsSync(mp3.result.path), `convert -> mp3 via FFmpeg (${mp3.result?.bytes} B)`);
  const back = await call('get_audio_info', { input_path: mp3.result.path });
  check(back.success && back.info.container === 'mp3', `mp3 read back natively: ${back.info?.duration_seconds?.toFixed(2)} s`);
}

const trim = await call('trim_audio', { input_path: toneA, start: 5, end: 12.5, fade_in: 1, fade_out: 1 });
check(trim.success && Math.abs(trim.result.duration_seconds - 7.5) < 0.001 && /_trim\.wav$/.test(trim.result.path), `trim 5..12.5 -> ${trim.result?.duration_seconds} s`);

const fx = await call('apply_effects', {
  input_path: toneA,
  output_path: join(out, 'fx.flac'),
  effects: [{ type: 'highpass', frequency: 80 }, { type: 'compressor', threshold_db: -20, ratio: 3 }, { type: 'normalize', peak_db: -3 }, { type: 'speed', factor: 2 }],
});
check(fx.success && Math.abs(fx.peak_dbfs + 3) < 0.05 && Math.abs(fx.result.duration_seconds - 20) < 0.01, `effects chain: peak ${fx.peak_dbfs?.toFixed(2)} dBFS, ${fx.result?.duration_seconds} s after speed x2`);
const bad = await call('apply_effects', { input_path: toneA, effects: [{ type: 'warp' }] });
check(!bad.success && /Unknown effect/.test(bad.error), 'unknown effect is rejected');

const mix = await call('mix_audio', { inputs: [{ path: toneA, gain_db: -6 }, { path: toneB, offset_seconds: 30 }], output_path: join(out, 'mix.wav'), normalize: true });
check(mix.success && Math.abs(mix.result.duration_seconds - 50) < 0.01 && !mix.clipping_warning, `mix with offset -> ${mix.result?.duration_seconds} s`);
const cat = await call('concat_audio', { inputs: [toneA, toneB], output_path: join(out, 'cat.ogg'), crossfade_seconds: 2 });
check(cat.success && Math.abs(cat.result.duration_seconds - 58) < 0.01, `concat with 2 s crossfade -> ${cat.result?.duration_seconds} s`);

// Restoration: a tone with 30 mouth-click-like bursts, and a clipped tone
{
  const rate = 44100, secs = 20, frames = rate * secs;
  const buf = Buffer.alloc(44 + frames * 4);
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + frames * 4, 4); buf.write('WAVE', 8);
  buf.write('fmt ', 12); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20); buf.writeUInt16LE(2, 22);
  buf.writeUInt32LE(rate, 24); buf.writeUInt32LE(rate * 4, 28); buf.writeUInt16LE(4, 32); buf.writeUInt16LE(16, 34);
  buf.write('data', 36); buf.writeUInt32LE(frames * 4, 40);
  const clickAt = new Set(Array.from({ length: 30 }, (_, k) => Math.round((0.5 + k * 0.6) * rate)));
  let burst = 0, burstPos = 0;
  for (let i = 0; i < frames; i++) {
    if (clickAt.has(i)) { burst = 1; burstPos = 0; }
    let v = 0.2 * Math.sin((2 * Math.PI * 220 * i) / rate);
    if (burst) { v += 0.3 * Math.exp(-burstPos / 12) * Math.sin(2 * Math.PI * 6000 * burstPos / rate); if (++burstPos > 60) burst = 0; }
    const s = Math.round(Math.max(-1, Math.min(1, v)) * 32767);
    buf.writeInt16LE(s, 44 + i * 4); buf.writeInt16LE(s, 46 + i * 4);
  }
  const clicky = join(srcDir, 'clicky.wav');
  writeFileSync(clicky, buf);
  const dc = await call('apply_effects', { input_path: clicky, output_path: join(out, 'declicked.wav'), effects: [{ type: 'mouth_declick' }] });
  const rep = dc.effect_reports?.[0]?.report;
  check(dc.success && rep.clicks_repaired >= 27 && rep.clicks_repaired <= 40, `mouth_declick repaired ${rep?.clicks_repaired} of 30 injected clicks`);
  // Residual against the clean tone: mix (result + inverted clean tone), then measure its RMS.
  for (let i = 0; i < frames; i++) {
    const s = Math.round(0.2 * Math.sin((2 * Math.PI * 220 * i) / rate) * 32767);
    buf.writeInt16LE(s, 44 + i * 4); buf.writeInt16LE(s, 46 + i * 4);
  }
  const clean = join(srcDir, 'clean.wav');
  writeFileSync(clean, buf);
  await call('apply_effects', { input_path: clean, output_path: join(out, 'clean_inv.wav'), effects: [{ type: 'invert' }], sample_format: 'float32' });
  const residual = async (file, tag) => {
    const m = await call('mix_audio', { inputs: [{ path: file }, { path: join(out, 'clean_inv.wav') }], output_path: join(out, `res_${tag}.wav`), sample_format: 'float32' });
    return (await call('get_audio_info', { input_path: m.result.path, analyze: true })).rms_dbfs;
  };
  const before = await residual(clicky, 'before');
  const after = await residual(join(out, 'declicked.wav'), 'after');
  // Only the high band is rebuilt (the voice body below ~1.5 kHz is kept as recorded), so a little
  // low-frequency leakage of each click stays, far below audibility.
  check(after < before - 12, `click residual ${before.toFixed(1)} dBFS -> ${after.toFixed(1)} dBFS after mouth_declick`);

  for (let i = 0; i < frames; i++) {
    const s = Math.round(Math.max(-1, Math.min(1, 1.6 * Math.sin((2 * Math.PI * 220 * i) / rate))) * 32767);
    buf.writeInt16LE(s, 44 + i * 4); buf.writeInt16LE(s, 46 + i * 4);
  }
  const clipped = join(srcDir, 'clipped.wav');
  writeFileSync(clipped, buf);
  const dp = await call('apply_effects', { input_path: clipped, output_path: join(out, 'declipped.wav'), effects: [{ type: 'declip' }, { type: 'limiter', ceiling_db: -1 }] });
  check(dp.success && dp.effect_reports[0].report.clipped_runs_rebuilt > 1000 && dp.peak_dbfs <= -0.99, `declip rebuilt ${dp.effect_reports?.[0]?.report?.clipped_runs_rebuilt} clipped peaks, peak ${dp.peak_dbfs?.toFixed(2)} dBFS`);
  const nr = await call('apply_effects', { input_path: toneA, output_path: join(out, 'nr.wav'), effects: [{ type: 'noise_reduction', reduction_db: 12 }] });
  check(nr.success && /automatic/.test(nr.effect_reports[0].report.profile), `noise_reduction (Audacity algorithm) with ${nr.effect_reports?.[0]?.report?.profile}`);
  const cr = await call('apply_effects', { input_path: clicky, output_path: join(out, 'clickremoval.wav'), effects: [{ type: 'click_removal' }] });
  check(cr.success && typeof cr.effect_reports[0].report.clicks_repaired === 'number', `click_removal (Audacity algorithm) repaired ${cr.effect_reports?.[0]?.report?.clicks_repaired}`);
}

const ext = await call('install_audacity_extension', {});
const extDir = join(extRoot, 'audacity-mcp-tools');
check(ext.success && existsSync(join(extDir, 'manifest.json')) && existsSync(join(extDir, nativeLib)), `extension installs into ${extRoot} with ${nativeLib}`);
const st2 = await call('get_audacity_status', {});
check(st2.extension.installed && st2.extension.up_to_date, `status sees extension v${st2.extension.installed_version}`);
const rm = await call('install_audacity_extension', { uninstall: true });
check(rm.success && !existsSync(extDir), 'extension uninstalls');

const notAbs = await call('get_audio_info', { input_path: 'toneA.wav' });
check(!notAbs.success && /absolute/.test(notAbs.error), 'rejects relative paths');

await client.close();
const count = readdirSync(join(out, 'split', 'toneA')).length;
console.log(`\nOutputs in ${out} (${count} pieces for toneA, ${statSync(toneA).size} B source)`);
process.exit(failed ? 1 : 0);
