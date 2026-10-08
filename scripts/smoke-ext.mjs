// Runs the native smoke test of the Audacity extension library built for this OS
// (loads it through extension_dispatch_v0 like Audacity's MuseApi.Native and round-trips audio).
import { execFileSync } from 'child_process';
import { mkdtempSync } from 'fs';
import { tmpdir } from 'os';
import { join } from 'path';

const win = process.platform === 'win32';
const exe = win ? 'native/build/ext_smoke.exe' : 'native/build-cmake/ext_smoke';
const lib = win ? 'native/build/audacity_mcp_native.dll'
  : process.platform === 'darwin' ? 'native/build-cmake/audacity_mcp_native.dylib' : 'native/build-cmake/audacity_mcp_native.so';
execFileSync(exe, [lib, mkdtempSync(join(tmpdir(), 'ext-smoke-'))], { stdio: 'inherit' });
