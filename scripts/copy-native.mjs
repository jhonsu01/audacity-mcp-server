// Copies the native engine and the Audacity extension bundle next to the compiled server:
//   native/build/aumcp-engine.exe -> dist/bin/aumcp-engine.exe
//   extension/                     -> dist/extension/ (manifest, scripts, platform/windows/x86_64/*.dll)
// Build the native parts first with `npm run build:native` (needs Visual Studio C++ tools).
import { cpSync, existsSync, mkdirSync, readFileSync } from 'fs';

const engine = 'native/build/aumcp-engine.exe';
const dll = 'extension/platform/windows/x86_64/audacity_mcp_native.dll';
for (const f of [engine, dll]) {
  if (!existsSync(f)) {
    console.error(`${f} is missing: run "npm run build:native" first.`);
    process.exit(1);
  }
}

// The extension and the server must ship the same version.
const pkg = JSON.parse(readFileSync('package.json', 'utf-8'));
const ext = JSON.parse(readFileSync('extension/manifest.json', 'utf-8'));
const mcpb = JSON.parse(readFileSync('manifest.json', 'utf-8'));
if (ext.version !== pkg.version || mcpb.version !== pkg.version) {
  console.error(`Version mismatch: package.json ${pkg.version}, manifest.json ${mcpb.version}, extension/manifest.json ${ext.version}`);
  process.exit(1);
}

mkdirSync('dist/bin', { recursive: true });
cpSync(engine, 'dist/bin/aumcp-engine.exe');
cpSync('extension', 'dist/extension', { recursive: true });
console.log('copied native engine -> dist/bin, extension -> dist/extension');
