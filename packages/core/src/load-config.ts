/**
 * Finds and loads the user's config file.
 */
import { existsSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { isAbsolute, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const CANDIDATES = [
  'blink-installer.config.mjs',
  'blink-installer.config.js',
  'blink-installer.config.json',
];

export interface LoadedConfig {
  config: unknown;
  /** Absolute path of the file that was loaded. */
  path: string;
  /** Directory relative paths inside the config resolve against. */
  cwd: string;
}

function resolveExplicit(explicitPath: string, cwd: string): string {
  const path = isAbsolute(explicitPath) ? explicitPath : resolve(cwd, explicitPath);
  if (!existsSync(path)) throw new Error(`config file not found: ${path}`);
  return path;
}

function resolveDiscovered(cwd: string): string {
  const found = CANDIDATES.map((name) => resolve(cwd, name)).find((p) => existsSync(p));
  if (!found) {
    throw new Error(
      `No config file found in ${cwd}.\n` +
        `Expected one of: ${CANDIDATES.join(', ')}\n` +
        `Run \`blink-installer init\` to create one.`,
    );
  }
  return found;
}

export async function loadConfigFile(
  explicitPath: string | undefined,
  cwd: string = process.cwd(),
): Promise<LoadedConfig> {
  const path = explicitPath
    ? resolveExplicit(explicitPath, cwd)
    : resolveDiscovered(cwd);

  if (path.endsWith('.json')) {
    return { config: JSON.parse(await readFile(path, 'utf8')), path, cwd };
  }

  // Cache-bust so repeated builds in one process (a watch mode, or a Forge
  // maker invoked twice) see edits rather than the first version loaded.
  const url = `${pathToFileURL(path).href}?t=${Date.now()}`;
  const module = (await import(url)) as { default?: unknown };
  const config = module.default ?? module;
  return { config, path, cwd };
}
