/**
 * Electron Forge maker.
 *
 * Forge's maker interface is the documented extension point for custom
 * distributable formats, so an Electron app gets an HTML-UI installer by adding
 * one entry to forge.config.js — no separate build step, no wrapper script:
 *
 *   makers: [
 *     {
 *       name: 'blink-installer-maker',
 *       config: {
 *         appId: 'com.example.myapp',
 *         install: { defaultDir: '$LOCALAPPDATA\\My App' },
 *         ui: './installer-ui',
 *       },
 *     },
 *   ]
 *
 * Forge has already packaged the app by the time make() runs, and hands us the
 * directory in `opts.dir` — exactly the input the pipeline wants. Everything
 * this package does is fill in the fields Forge already knows (product name,
 * version, executable) and delegate.
 */
import { MakerBase, type MakerOptions } from '@electron-forge/maker-base';
import { buildInstaller, type BlinkInstallerConfig } from 'blink-installer-core';
import { existsSync } from 'node:fs';
import { join } from 'node:path';

/**
 * Everything from the normal config except the fields Forge supplies. Set any
 * of them anyway to override what is inferred.
 */
export type MakerBlinkInstallerConfig = Partial<BlinkInstallerConfig>;

/** package.json `author` is either a string or an object; accept both. */
function authorName(packageJSON: unknown): string {
  const author = (packageJSON as { author?: string | { name?: string } }).author;
  if (typeof author === 'string') return author;
  return author?.name ?? '';
}

export default class MakerBlinkInstaller extends MakerBase<MakerBlinkInstallerConfig> {
  name = 'blink-installer';
  defaultPlatforms: MakerOptions['targetPlatform'][] = ['win32'];

  override isSupportedOnCurrentPlatform(): boolean {
    // makensis and the plugin are Windows binaries. Say so plainly rather than
    // failing later with a confusing spawn error.
    return process.platform === 'win32';
  }

  override async make(options: MakerOptions): Promise<string[]> {
    const { dir, makeDir, appName, targetArch, packageJSON } = options;

    const productName =
      this.config.productName ??
      (packageJSON as { productName?: string }).productName ??
      appName;
    const version =
      this.config.version ?? (packageJSON as { version?: string }).version ?? '0.0.0';

    // Forge names the executable after the app; confirm rather than assume, so
    // a mismatch is reported here instead of producing a broken shortcut.
    const exe = this.config.exe ?? `${appName}.exe`;
    if (!existsSync(join(dir, exe))) {
      throw new Error(
        `blink-installer: expected "${exe}" inside ${dir} but it is not there. ` +
          `Set \`exe\` in the maker config to the real executable name.`,
      );
    }

    const outputPath = join(
      makeDir,
      'blink-installer',
      targetArch,
      `${productName}-Setup-${version}.exe`,
    );
    await this.ensureFile(outputPath);

    const config: BlinkInstallerConfig = {
      appId: this.config.appId ?? `com.electron.${appName.toLowerCase()}`,
      productName,
      version,
      publisher: this.config.publisher ?? authorName(packageJSON),
      description:
        this.config.description ??
        (packageJSON as { description?: string }).description ??
        '',
      ...this.config,
      // Forge owns these three; a maker config cannot meaningfully override them.
      source: dir,
      exe,
      output: this.config.output ?? outputPath,
    };

    const result = await buildInstaller({ config, cwd: process.cwd() });
    return [result.outputPath];
  }
}
