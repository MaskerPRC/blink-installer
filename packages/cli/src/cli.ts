#!/usr/bin/env node
/**
 * blink-installer command line.
 *
 *   blink-installer build [--config path] [--out path] [--keep-temp]
 *   blink-installer init
 */
import { buildInstaller, loadConfigFile } from 'blink-installer-core';
import { existsSync } from 'node:fs';
import { cp, mkdir, mkdtemp, writeFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';

const COMPRESSIONS = ['zlib', 'bzip2', 'lzma'] as const;
type Compression = (typeof COMPRESSIONS)[number];

interface Flags {
  config?: string;
  out?: string;
  compression?: Compression;
  keepTemp: boolean;
  ejectUi: boolean;
  uiOnly: boolean;
}

function parseFlags(argv: string[]): Flags {
  const flags: Flags = { keepTemp: false, ejectUi: false, uiOnly: false };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--config' || arg === '-c') flags.config = argv[++i];
    else if (arg === '--out' || arg === '-o') flags.out = argv[++i];
    else if (arg === '--compression') {
      const value = argv[++i];
      if (!COMPRESSIONS.includes(value as Compression)) {
        // Fail here rather than pass it through: makensis reports an unknown
        // compressor a thousand lines into a build that already spent minutes
        // compressing.
        throw new Error(
          `unknown --compression "${value ?? ''}"; expected one of ${COMPRESSIONS.join(', ')}`,
        );
      }
      flags.compression = value as Compression;
    } else if (arg === '--keep-temp') flags.keepTemp = true;
    else if (arg === '--ui-only') flags.uiOnly = true;
    else if (arg === '--eject-ui') flags.ejectUi = true;
  }
  return flags;
}

function usage(): void {
  process.stdout.write(`blink-installer — build a Windows installer with an HTML UI

Usage:
  blink-installer build [options]     Build the installer
  blink-installer init [--eject-ui]   Create a starter config (and optionally
                                      copy the default UI so you can edit it)

Options:
  -c, --config <path>   Config file (default: blink-installer.config.mjs)
  -o, --out <path>      Override the output path
      --compression     zlib | bzip2 | lzma — overrides the config
      --ui-only         Build without the application payload, for working on
                        the interface: same pages, same flow, seconds instead
                        of minutes. Installs to its own directory under its own
                        registry key with shortcuts off, so it cannot touch a
                        real installation. Not a distributable installer.
      --keep-temp       Keep the staging directory and print its location
  -h, --help            Show this help

lzma produces the smallest installer and is what you want for a release; zlib
compresses several times faster. Combined with --ui-only that gives you a
fast loop on the interface and a small artifact on release, from one config:

  "installer:dev":     "blink-installer build --ui-only --compression zlib"
  "installer:release": "blink-installer build --compression lzma"
`);
}

const STARTER_CONFIG = `import { defineConfig } from 'blink-installer-core';

export default defineConfig({
  appId: 'com.example.myapp',
  productName: 'My App',
  version: '1.0.0',
  publisher: 'My Company',

  // Your already-packaged application directory. For Electron this is the
  // output of \`electron-builder --dir\` or electron-packager.
  source: 'out/myapp-win32-x64',
  exe: 'myapp.exe',

  // Omit \`ui\` to use the built-in template, or point it at your own directory
  // containing index.html.
  // ui: './installer-ui',

  install: {
    defaultDir: '$PROGRAMFILES\\\\My App',
    shortcuts: { desktop: true, startMenu: true },
  },
});
`;

async function commandInit(flags: Flags): Promise<void> {
  const configPath = resolve(process.cwd(), 'blink-installer.config.mjs');
  if (existsSync(configPath)) {
    process.stderr.write(`blink-installer.config.mjs already exists — not overwriting.\n`);
  } else {
    await writeFile(configPath, STARTER_CONFIG, 'utf8');
    process.stdout.write(`created blink-installer.config.mjs\n`);
  }

  if (flags.ejectUi) {
    // core lives at node_modules/blink-installer-core; its templates sit
    // beside dist/.
    const require = createRequire(import.meta.url);
    const coreEntry = require.resolve('blink-installer-core');
    const source = resolve(dirname(coreEntry), '..', 'templates', 'default-ui');
    const target = resolve(process.cwd(), 'installer-ui');
    if (existsSync(target)) {
      process.stderr.write(`installer-ui already exists — not overwriting.\n`);
    } else {
      await mkdir(target, { recursive: true });
      await cp(source, target, { recursive: true });
      process.stdout.write(
        `copied the default UI to installer-ui/\n` +
          `Set \`ui: './installer-ui'\` in your config to use it.\n`,
      );
    }
  }
}

/**
 * Rewrites the config so a build compiles the installer without the
 * application, and cannot touch a real installation of it.
 *
 * Working on the interface otherwise means paying for the payload on every
 * iteration — for a 1.1 GB Electron app that is well over a minute per run to
 * look at a page that took two seconds to edit. Everything else stays real:
 * the NSIS script, the pages, the progress wiring, the uninstaller.
 *
 * The isolation is not optional. A preview built from an unmodified config
 * installs to the same directory, under the same registry key, over the same
 * shortcuts as the product — so clicking Install in a preview would replace
 * the user's real application with a placeholder text file. Every identity the
 * install touches is therefore given a preview-only value, and the shortcuts
 * are turned off so a preview cannot litter the desktop or Start menu.
 *
 * `runAfter` goes off too: the thing it would launch is not a program.
 */
const PREVIEW_SUFFIX = ' (UI preview)';

async function usePreviewIdentity(
  config: Record<string, unknown>,
): Promise<void> {
  const exe = typeof config['exe'] === 'string' ? config['exe'] : 'app.exe';
  const dir = await mkdtemp(join(tmpdir(), 'blink-installer-uionly-'));

  // Named exactly like the real executable, so the generated script, the
  // shortcut targets and the uninstaller exercise the same paths they will in
  // a real build.
  await writeFile(
    join(dir, exe),
    'Placeholder from `blink-installer build --ui-only`.\r\n' +
      'This installer previews the interface and carries no application.\r\n',
    'utf8',
  );
  config['source'] = dir;

  const product =
    typeof config['productName'] === 'string' ? config['productName'] : 'App';
  config['productName'] = product + PREVIEW_SUFFIX;

  // Distinct uninstall entry, so Programs and Features shows the preview
  // separately and removing it cannot remove the product.
  if (typeof config['appId'] === 'string') {
    config['appId'] = config['appId'] + '.uipreview';
  }

  const install =
    typeof config['install'] === 'object' && config['install'] !== null
      ? (config['install'] as Record<string, unknown>)
      : {};

  const registryKey =
    typeof install['registryKey'] === 'string'
      ? install['registryKey'] + '\\UIPreview'
      : undefined;

  config['install'] = {
    ...install,
    // A fixed preview directory rather than the configured one. Deliberately
    // not derived from defaultDir: that may be $PROGRAMFILES, and a preview
    // has no business asking for elevation.
    defaultDir: '$LOCALAPPDATA\\blink-installer-preview\\' + product,
    elevate: false,
    runAfter: false,
    // Nothing user-visible outside the preview's own directory.
    shortcuts: { desktop: false, startMenu: false },
    ...(registryKey ? { registryKey } : {}),
  };

  process.stdout.write(
    'ui-only: previewing the interface, isolated from any real install\n' +
      `  payload   -> placeholder in ${dir}\n` +
      `  installs  -> $LOCALAPPDATA\\blink-installer-preview\\${product}\n` +
      '  shortcuts -> off,  elevate -> off,  runAfter -> off\n' +
      '  not a distributable installer\n',
  );
}

async function commandBuild(flags: Flags): Promise<void> {
  const loaded = await loadConfigFile(flags.config);
  process.stdout.write(`using ${loaded.path}\n`);

  const config = loaded.config as Record<string, unknown>;
  if (flags.out) config['output'] = flags.out;
  if (flags.compression) config['compression'] = flags.compression;
  if (flags.uiOnly) await usePreviewIdentity(config);

  const started = Date.now();
  const result = await buildInstaller({
    config,
    cwd: loaded.cwd,
    keepTemp: flags.keepTemp,
    onLog: (line) => process.stdout.write(`${line}\n`),
  });

  const seconds = ((Date.now() - started) / 1000).toFixed(1);
  process.stdout.write(
    `\ndone in ${seconds}s\n` +
      `  ${result.outputPath}\n` +
      `  ${(result.size / 1024 / 1024).toFixed(1)} MB\n`,
  );
  if (result.stagingDir) {
    process.stdout.write(`  staging kept at ${result.stagingDir}\n`);
  }
}

async function main(): Promise<void> {
  const [, , command, ...rest] = process.argv;
  if (!command || command === '-h' || command === '--help' || command === 'help') {
    usage();
    return;
  }

  const flags = parseFlags(rest);
  switch (command) {
    case 'build':
      await commandBuild(flags);
      break;
    case 'init':
      await commandInit(flags);
      break;
    default:
      process.stderr.write(`unknown command: ${command}\n\n`);
      usage();
      process.exitCode = 1;
  }
}

main().catch((error: unknown) => {
  const message = error instanceof Error ? error.message : String(error);
  process.stderr.write(`\nblink-installer failed:\n${message}\n`);
  process.exitCode = 1;
});
