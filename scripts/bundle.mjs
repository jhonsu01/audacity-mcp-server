// Single-file CommonJS bundle for the .mcpb package (no node_modules needed at runtime).
import { buildSync } from 'esbuild';

buildSync({
  entryPoints: ['src/index.ts'],
  bundle: true,
  platform: 'node',
  target: 'node20',
  format: 'cjs',
  outfile: 'dist/bundle.cjs',
  define: { 'import.meta.url': '_importMetaUrl' },
  banner: { js: 'const _importMetaUrl = require("url").pathToFileURL(__filename).href;' },
  logLevel: 'info',
});
