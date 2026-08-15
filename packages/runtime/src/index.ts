/**
 * Locates the prebuilt binaries this package ships.
 *
 * They are shipped rather than built on install because building them needs
 * MSVC and a 32-bit toolchain — an unreasonable thing to require of someone
 * who just wants to package their app, and impossible on the Linux and macOS
 * CI runners that people build Windows installers from.
 */
import { createRequire } from 'node:module';
import { existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
// dist/ -> package root
const packageRoot = join(here, '..');

export interface RuntimePaths {
  /** Directory holding blinkkit.dll and node.dll. */
  readonly pluginDir: string;
  /** The NSIS plugin that hosts the installer UI (x86). */
  readonly blinkkitDll: string;
  /** miniblink. Named node.dll by upstream; it is not Node.js. */
  readonly miniblinkDll: string;
  /** Root of the bundled NSIS distribution. */
  readonly nsisDir: string;
  /** The real compiler. NSIS ships a stub at the top level that only launches this. */
  readonly makensis: string;
}

function required(path: string, what: string): string {
  if (!existsSync(path)) {
    throw new Error(
      `blink-installer-runtime is missing ${what} at ${path}. ` +
        `The package may have been installed without its binaries — try reinstalling.`,
    );
  }
  return path;
}

let cached: RuntimePaths | undefined;

export function runtimePaths(): RuntimePaths {
  if (cached) return cached;

  const pluginDir = join(packageRoot, 'bin');
  const nsisDir = join(packageRoot, 'nsis');

  cached = {
    pluginDir: required(pluginDir, 'its bin directory'),
    blinkkitDll: required(join(pluginDir, 'blinkkit.dll'), 'blinkkit.dll'),
    miniblinkDll: required(join(pluginDir, 'node.dll'), 'the miniblink engine'),
    nsisDir: required(nsisDir, 'its NSIS distribution'),
    makensis: required(join(nsisDir, 'Bin', 'makensis.exe'), 'makensis.exe'),
  };
  return cached;
}

/** Version of this runtime bundle, for cache keys and diagnostics. */
export function runtimeVersion(): string {
  const require = createRequire(import.meta.url);
  const pkg = require(join(packageRoot, 'package.json')) as { version: string };
  return pkg.version;
}
