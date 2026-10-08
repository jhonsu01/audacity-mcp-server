// Copies the native engines and the Audacity extension bundle next to the compiled server:
//   native/out/<platform>-<arch>/aumcp-engine[.exe] -> dist/bin/<platform>-<arch>/
//   extension/                                     -> dist/extension/ (manifest, scripts, platform/*/*/native library)
// Every engine present in native/out is shipped (CI collects all three platforms before packing);
// the current platform's engine is required. Build it with `npm run build:native`.
import { chmodSync, cpSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync } from 'fs';
import { join } from 'path';

const current = process.platform === 'darwin' ? ['darwin-universal', `darwin-${process.arch}`] : [`${process.platform}-${process.arch}`];
const available = existsSync('native/out') ? readdirSync('native/out') : [];
if (!current.some((d) => available.includes(d))) {
  console.error(`No native engine for ${current.join(' / ')} in native/out: run "npm run build:native" first.`);
  process.exit(1);
}

// The extension and the server must ship the same version.
const pkg = JSON.parse(readFileSync('package.json', 'utf-8'));
const ext = JSON.parse(readFileSync('extension/manifest.json', 'utf-8'));
const mcpb = JSON.parse(readFileSync('manifest.json', 'utf-8'));
if (ext.version !== pkg.version || mcpb.version !== pkg.version) {
  console.error(`Version mismatch: package.json ${pkg.version}, manifest.json ${mcpb.version}, extension/manifest.json ${ext.version}`);
  process.exit(1);
}

rmSync('dist/bin', { recursive: true, force: true });
for (const dir of available) {
  mkdirSync(join('dist/bin', dir), { recursive: true });
  cpSync(join('native/out', dir), join('dist/bin', dir), { recursive: true });
  for (const f of readdirSync(join('dist/bin', dir))) {
    if (!f.endsWith('.exe')) chmodSync(join('dist/bin', dir, f), 0o755);
  }
}
rmSync('dist/extension', { recursive: true, force: true });
cpSync('extension', 'dist/extension', { recursive: true });
const libs = [];
for (const os of existsSync('extension/platform') ? readdirSync('extension/platform') : []) {
  for (const arch of readdirSync(join('extension/platform', os))) libs.push(`${os}/${arch}`);
}
console.log(`engines: ${available.join(', ')} | extension libraries: ${libs.join(', ') || 'none'}`);
