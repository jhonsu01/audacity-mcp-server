// Builds the native engine and the Audacity extension library for the current OS and places them in
//   native/out/<platform>-<arch>/aumcp-engine[.exe]
//   extension/platform/<windows|macos|linux>/<x86_64|arm64|universal>/audacity_mcp_native.<dll|dylib|so>
// Windows: MSVC through native/build.ps1. macOS / Linux: CMake (macOS builds universal arm64+x86_64).
import { execFileSync } from 'child_process';
import { copyFileSync, mkdirSync, chmodSync } from 'fs';
import { join } from 'path';

const run = (cmd, args) => execFileSync(cmd, args, { stdio: 'inherit' });
const platform = process.platform;

if (platform === 'win32') {
  run('powershell', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'native/build.ps1']);
  mkdirSync('native/out/win32-x64', { recursive: true });
  copyFileSync('native/build/aumcp-engine.exe', 'native/out/win32-x64/aumcp-engine.exe');
  console.log('native/out/win32-x64 ready');
} else {
  const build = 'native/build-cmake';
  const args = ['-S', 'native', '-B', build, '-DCMAKE_BUILD_TYPE=Release'];
  if (platform === 'darwin') args.push('-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64');
  run('cmake', args);
  run('cmake', ['--build', build, '--config', 'Release', '--parallel']);
  const arch = process.arch === 'arm64' ? 'arm64' : 'x86_64';
  const engineDir = platform === 'darwin' ? 'native/out/darwin-universal' : `native/out/linux-${process.arch}`;
  const libDir = platform === 'darwin' ? 'extension/platform/macos/universal' : `extension/platform/linux/${arch}`;
  const libName = platform === 'darwin' ? 'audacity_mcp_native.dylib' : 'audacity_mcp_native.so';
  mkdirSync(engineDir, { recursive: true });
  mkdirSync(libDir, { recursive: true });
  copyFileSync(join(build, 'aumcp-engine'), join(engineDir, 'aumcp-engine'));
  chmodSync(join(engineDir, 'aumcp-engine'), 0o755);
  copyFileSync(join(build, libName), join(libDir, libName));
  if (platform === 'darwin') {
    // Apple Silicon only runs signed code: ad-hoc sign both binaries.
    run('codesign', ['--force', '--sign', '-', join(engineDir, 'aumcp-engine')]);
    run('codesign', ['--force', '--sign', '-', join(libDir, libName)]);
  }
  console.log(`${engineDir} and ${libDir} ready`);
}
