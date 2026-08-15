/**
 * The build pipeline: config in, setup.exe out.
 */
import { runtimePaths } from 'blink-installer-runtime';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { mkdir, mkdtemp, readFile, rm, stat, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, isAbsolute, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { bundleUi } from './bundle-ui.js';
import { parseConfig, normalizeVersion, type ResolvedConfig } from './config.js';
import { renderInstallerScript, renderUninstallerScript } from './nsi.js';
import { signArtifact } from './sign.js';

const here = dirname(fileURLToPath(import.meta.url));

export interface BuildOptions {
  /** Parsed config, or the raw object to validate. */
  config: unknown;
  /** Directory that relative paths in the config resolve against. */
  cwd?: string;
  /** Keep the staging directory for inspection. */
  keepTemp?: boolean;
  onLog?: (line: string) => void;
}

export interface BuildResult {
  /** Absolute path of the produced installer. */
  outputPath: string;
  /** Size in bytes. */
  size: number;
  /** Where the intermediate .nsi and page were staged, if kept. */
  stagingDir?: string;
}

function defaultLog(line: string): void {
  process.stdout.write(`${line}\n`);
}

function abs(cwd: string, path: string): string {
  return isAbsolute(path) ? path : resolve(cwd, path);
}

/** The UI that ships with the package, used when the config names none. */
function defaultUiDir(): string {
  // dist/ -> package root -> templates/default-ui
  return resolve(here, '..', 'templates', 'default-ui');
}

async function runMakensis(
  scriptPath: string,
  onLog: (line: string) => void,
): Promise<void> {
  const { makensis, nsisDir } = runtimePaths();

  await new Promise<void>((resolvePromise, rejectPromise) => {
    // NSISDIR must point at our bundled distribution or makensis will look for
    // Include/ and Stubs/ next to its own exe and fail confusingly.
    const child = spawn(makensis, [`/NOCD`, `/X!AddIncludeDir "${join(nsisDir, 'Include')}"`, scriptPath], {
      env: { ...process.env, NSISDIR: nsisDir },
      windowsHide: true,
    });

    const output: string[] = [];
    const capture = (chunk: Buffer) => {
      const text = chunk.toString();
      output.push(text);
      for (const line of text.split(/\r?\n/)) {
        if (line.trim()) onLog(`  makensis | ${line}`);
      }
    };
    child.stdout.on('data', capture);
    child.stderr.on('data', capture);

    child.on('error', (error) => {
      rejectPromise(
        new Error(`Could not run makensis at ${makensis}: ${error.message}`),
      );
    });
    child.on('close', (code) => {
      if (code === 0) {
        resolvePromise();
        return;
      }
      // Surface what NSIS actually said. A bare non-zero exit code sends the
      // user off to re-run the compiler by hand to find out what broke.
      rejectPromise(
        new Error(
          `makensis exited with code ${code}.\n\n${output.join('').trim()}`,
        ),
      );
    });
  });
}

export async function buildInstaller(options: BuildOptions): Promise<BuildResult> {
  const log = options.onLog ?? defaultLog;
  const cwd = options.cwd ?? process.cwd();
  const config: ResolvedConfig = parseConfig(options.config);

  // ---- resolve inputs -----------------------------------------------------
  const sourceDir = abs(cwd, config.source);
  if (!existsSync(sourceDir)) {
    throw new Error(
      `source directory does not exist: ${sourceDir}\n` +
        `This should be your already-packaged app — for Electron, the output of ` +
        `\`electron-builder --dir\` or electron-packager.`,
    );
  }
  const exePath = join(sourceDir, config.exe);
  if (!existsSync(exePath)) {
    throw new Error(
      `exe "${config.exe}" not found inside the source directory (${sourceDir}).`,
    );
  }

  const uiDir = config.ui ? abs(cwd, config.ui) : defaultUiDir();
  if (!existsSync(uiDir)) {
    throw new Error(`ui directory does not exist: ${uiDir}`);
  }

  const version4 = normalizeVersion(config.version);
  const outputPath = abs(
    cwd,
    config.output ?? join('dist', `${config.productName}-Setup-${config.version}.exe`),
  );
  await mkdir(dirname(outputPath), { recursive: true });

  // Remove a previous build first. makensis reports a locked output as the
  // bare "Can't open output file", which does not hint that the usual cause is
  // the last installer still running.
  if (existsSync(outputPath)) {
    try {
      await rm(outputPath, { force: true });
    } catch (error) {
      throw new Error(
        `cannot overwrite ${outputPath} — it is most likely still running, ` +
          `or open in another program.\n${(error as Error).message}`,
      );
    }
  }

  const iconPath = config.icon ? abs(cwd, config.icon) : undefined;
  if (iconPath && !existsSync(iconPath)) {
    throw new Error(`icon not found: ${iconPath}`);
  }

  const includeSnippet = config.nsis.include
    ? await readFile(abs(cwd, config.nsis.include), 'utf8')
    : undefined;

  // ---- stage --------------------------------------------------------------
  const stagingDir = await mkdtemp(join(tmpdir(), 'blink-installer-'));
  log(`staging in ${stagingDir}`);

  try {
    log(`bundling UI from ${uiDir}`);
    const html = await bundleUi({
      uiDir,
      defines: {
        productName: config.productName,
        version: version4,
        publisher: config.publisher,
      },
    });
    const htmlPath = join(stagingDir, 'index.min.html');
    await writeFile(htmlPath, html, 'utf8');
    log(`  page is ${(Buffer.byteLength(html) / 1024).toFixed(1)} KB`);

    // The uninstaller page is optional and, when the UI directory is a custom
    // one, may simply not exist — in which case the uninstaller falls back to
    // NSIS's own dialog rather than failing the build.
    let uninstallHtmlPath: string | undefined;
    if (config.install.uninstallEntry && config.uninstall.ui) {
      if (existsSync(join(uiDir, 'uninstall.html'))) {
        const uninstallHtml = await bundleUi({
          uiDir,
          entry: 'uninstall.html',
          defines: {
            productName: config.productName,
            version: version4,
            publisher: config.publisher,
          },
        });
        uninstallHtmlPath = join(stagingDir, 'uninstall.min.html');
        await writeFile(uninstallHtmlPath, uninstallHtml, 'utf8');
        log(
          `  uninstall page is ${(Buffer.byteLength(uninstallHtml) / 1024).toFixed(1)} KB` +
            ` (adds ~18 MB to the install directory for the renderer)`,
        );
      } else {
        log(
          `  no uninstall.html in ${uiDir} — the uninstaller will use the stock NSIS dialog`,
        );
      }
    }

    const { pluginDir, miniblinkDll } = runtimePaths();

    const nsiInputs = {
      config,
      htmlPath,
      uninstallHtmlPath,
      miniblinkPath: miniblinkDll,
      pluginDir,
      sourceDir,
      outputPath,
      iconPath,
      includeSnippet,
    };

    const scriptBody =
      renderInstallerScript(nsiInputs) +
      (config.install.uninstallEntry ? renderUninstallerScript(nsiInputs) : '');

    const scriptPath = join(stagingDir, 'installer.nsi');
    // The BOM is required: without it NSIS 3 reads the script in the system
    // ANSI code page and rejects the first non-ASCII byte, which any non-English
    // product name will have.
    await writeFile(scriptPath, `﻿${scriptBody}`, 'utf8');

    log('compiling with makensis');
    await runMakensis(scriptPath, log);

    // After makensis, not before: signing an installer means signing the
    // finished .exe, and makensis writes it whole.
    if (config.sign) {
      await signArtifact(outputPath, config.sign, cwd, log);
    }

    const info = await stat(outputPath);
    log(`built ${outputPath} (${(info.size / 1024 / 1024).toFixed(1)} MB)`);

    return {
      outputPath,
      size: info.size,
      stagingDir: options.keepTemp ? stagingDir : undefined,
    };
  } finally {
    if (!options.keepTemp) {
      await rm(stagingDir, { recursive: true, force: true });
    }
  }
}
