import { readFileSync } from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';
import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { registerTools } from './tools/register.js';

/** serverInfo.version comes from package.json (reachable as ../package.json from dist/ and src/). */
export function readPackageVersion(): string {
  try {
    const dir = path.dirname(fileURLToPath(import.meta.url));
    const pkg = JSON.parse(readFileSync(path.resolve(dir, '../package.json'), 'utf-8')) as { version?: unknown };
    if (typeof pkg.version === 'string' && pkg.version) return pkg.version;
  } catch {
    // package.json not shipped next to the bundle: still start
  }
  return '0.0.0-unknown';
}

export function createServer(): McpServer {
  const server = new McpServer(
    { name: 'audacity-mcp-server', version: readPackageVersion() },
    {
      instructions:
        'Edits audio files with the audio engine of the locally installed Audacity 4 (Windows): split into N-second pieces, ' +
        'trim, convert formats/sample rates, apply effects, mix and join. All paths must be absolute; files are never overwritten ' +
        'unless overwrite is true. Call get_audacity_status first if unsure Audacity is installed. ' +
        'install_audacity_extension adds the same tools inside Audacity\'s own menus; open_in_audacity opens results in the editor.',
    },
  );
  registerTools(server);
  return server;
}
