/**
 * Authenticode signing of the finished installer.
 *
 * Two ways in, because the two situations are genuinely different:
 *
 *  - **A hook.** You point at a module and it signs the file. The signature is
 *    deliberately the same as electron-builder's `win.signtoolOptions.sign`, so
 *    a project that already has a working hook — cloud HSM, timeouts, retries,
 *    whatever it had to learn — points at that same file and is done. Signing
 *    setups accumulate hard-won detail; asking for a rewrite would throw it away.
 *
 *  - **signtool directly.** A thumbprint or a .pfx, for projects with nothing
 *    yet. Enough to be useful, and no attempt to grow into the general case.
 *
 * What is *not* signed is the uninstaller. NSIS writes it at install time, on
 * the user's machine, from data embedded in the installer, so at build time it
 * does not exist as a file to sign. Getting a signed one means building twice —
 * run the installer to extract it, sign that, rebuild with the signed copy
 * embedded. Not done here; the download is what SmartScreen judges.
 */
import { execFile } from 'node:child_process';
import { existsSync, readdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { isAbsolute, join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { promisify } from 'node:util';

import type { ResolvedConfig } from './config.js';

const execFileAsync = promisify(execFile);

type SignConfig = NonNullable<ResolvedConfig['sign']>;

/**
 * Newest SDK signtool wins.
 *
 * Older ones predate RFC3161 (`/tr`) and fail on flags used below, so picking
 * whatever comes first alphabetically is not good enough.
 */
function findSigntool(explicit?: string): string {
  if (explicit && existsSync(explicit)) return explicit;

  const roots = [
    'C:/Program Files (x86)/Windows Kits/10/bin',
    'C:/Program Files/Windows Kits/10/bin',
  ];
  const found: { version: string; path: string }[] = [];
  for (const root of roots) {
    if (!existsSync(root)) continue;
    const flat = join(root, 'x64', 'signtool.exe');
    if (existsSync(flat)) found.push({ version: '0', path: flat });
    for (const entry of readdirSync(root)) {
      const candidate = join(root, entry, 'x64', 'signtool.exe');
      if (existsSync(candidate)) found.push({ version: entry, path: candidate });
    }
  }
  if (found.length > 0) {
    found.sort((a, b) =>
      b.version.localeCompare(a.version, undefined, { numeric: true }),
    );
    return found[0]!.path;
  }
  return 'signtool';
}

/**
 * Loads the hook and unwraps whatever module shape it turned out to be.
 *
 * A `.cjs` hook written as `exports.default = fn` arrives from `import()` as
 * `{ default: { default: fn } }`, because Node exposes the whole
 * `module.exports` as the default export. An ESM hook gives `{ default: fn }`.
 * Both are normal; peel until it is callable.
 */
async function loadHook(
  hookPath: string,
): Promise<(context: { path: string }) => unknown> {
  const loaded: unknown = await import(pathToFileURL(hookPath).href);

  let candidate: unknown = loaded;
  for (let depth = 0; depth < 3; depth++) {
    if (typeof candidate === 'function') return candidate as never;
    if (candidate && typeof candidate === 'object' && 'default' in candidate) {
      candidate = (candidate as { default: unknown }).default;
      continue;
    }
    break;
  }
  if (typeof candidate === 'function') return candidate as never;

  throw new Error(
    `sign.hook at ${hookPath} does not export a function ` +
      `(expected \`export default (ctx) => …\` or \`exports.default = …\`)`,
  );
}

async function signWithSigntool(
  file: string,
  config: SignConfig,
  cwd: string,
  log: (line: string) => void,
): Promise<void> {
  const signtool = findSigntool(config.signtool);
  const args = ['sign', '/fd', 'sha256'];

  if (config.thumbprint) {
    // From the certificate store. The only option for a cloud/HSM certificate,
    // where no exportable key file exists.
    args.push('/sha1', config.thumbprint.replace(/[^0-9a-fA-F]/g, ''));
  } else if (config.certificateFile) {
    const pfx = isAbsolute(config.certificateFile)
      ? config.certificateFile
      : resolve(cwd, config.certificateFile);
    if (!existsSync(pfx)) throw new Error(`certificate not found: ${pfx}`);
    args.push('/f', pfx);
    const password =
      config.certificatePassword ?? process.env['BLINK_SIGN_PASSWORD'];
    if (password) args.push('/p', password);
  } else {
    throw new Error(
      'sign needs one of: hook, thumbprint, or certificateFile',
    );
  }

  if (config.timestamp) {
    // Countersigning keeps the signature valid after the certificate expires.
    // Without it every artifact silently goes bad on the expiry date.
    args.push('/tr', config.timestamp, '/td', 'sha256');
  }
  args.push(file);

  // Timeout rather than wait: a cloud certificate whose session has lapsed
  // makes signtool block on an auth prompt no unattended build can answer.
  const { stdout, stderr } = await execFileAsync(signtool, args, {
    timeout: config.timeoutMs,
    windowsHide: true,
  });
  for (const line of `${stdout}${stderr}`.split(/\r?\n/)) {
    if (line.trim()) log(`  signtool | ${line.trim()}`);
  }
}

export async function signArtifact(
  file: string,
  config: SignConfig,
  cwd: string,
  log: (line: string) => void,
): Promise<void> {
  try {
    if (config.hook) {
      const hookPath = isAbsolute(config.hook)
        ? config.hook
        : resolve(cwd, config.hook);
      if (!existsSync(hookPath)) {
        throw new Error(`sign.hook not found: ${hookPath}`);
      }
      log(`signing via ${config.hook}`);
      const hook = await loadHook(hookPath);
      await hook({ path: file });
    } else {
      log('signing with signtool');
      await signWithSigntool(file, config, cwd, log);
    }
    log('signed');
  } catch (error) {
    const reason = (error as Error).message.split('\n')[0] ?? 'unknown error';
    if (config.required) {
      throw new Error(
        `signing failed and sign.required is set: ${reason}\n` +
          `Unset sign.required to ship this build unsigned instead.`,
      );
    }
    // Unsigned is usually better than no build at all — a lapsed cloud session
    // should not stop you producing something to test. Loud, so it cannot pass
    // for success in a log nobody reads closely.
    log(`WARNING: not signed — ${reason}`);
    log('WARNING: this installer will trip SmartScreen. Set sign.required to fail instead.');
  }
}

/** Kept separate so `createRequire` stays available for future resolution needs. */
export const requireFrom = createRequire(import.meta.url);
