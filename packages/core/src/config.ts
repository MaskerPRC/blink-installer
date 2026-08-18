/**
 * The user-facing configuration schema.
 *
 * Product name, publisher, registry key, install directory and the UI itself
 * are fields here rather than literals inside a generated .nsi, so a second
 * product needs a second config and not a second script.
 */
import { z } from 'zod';

/**
 * NSIS's VIProductVersion demands exactly four numeric components. Accept the
 * usual three-part semver and pad it, and strip any prerelease suffix, so
 * `1.2.3-beta.1` does not fail the build at the very last step.
 */
export function normalizeVersion(version: string): string {
  const core = version.split(/[-+]/)[0] ?? version;
  const parts = core.split('.').map((p) => p.trim());
  while (parts.length < 4) parts.push('0');
  return parts.slice(0, 4).join('.');
}

const shortcutsSchema = z
  .object({
    desktop: z.boolean().default(true),
    startMenu: z.boolean().default(true),
    /** Start-menu subfolder; defaults to productName. */
    startMenuFolder: z.string().optional(),
  })
  .default({});

const installSchema = z
  .object({
    /**
     * Default install directory. NSIS variables such as $PROGRAMFILES,
     * $LOCALAPPDATA and $PROGRAMFILES64 may be used.
     */
    defaultDir: z.string().default('$PROGRAMFILES\\${PRODUCT_NAME}'),
    /** Whether the page may change the directory via fs.pickDirectory. */
    allowDirChange: z.boolean().default(true),
    /**
     * Force the pre-Vista SHBrowseForFolder tree box instead of the explorer
     * folder picker.
     *
     * An escape hatch, not a style choice — the modern dialog is better in
     * every way a user would notice. It exists because the explorer picker is
     * hosted by the shell and so is exposed to whatever else is loaded into
     * the process on a given machine; where a shell extension makes it hang or
     * refuse to open, the old tree box still works because it asks far less of
     * the shell. Reach for it only with a reproduction in hand.
     */
    legacyFolderPicker: z.boolean().default(false),
    shortcuts: shortcutsSchema,
    /** Register in Programs and Features, and generate an uninstaller. */
    uninstallEntry: z.boolean().default(true),
    /**
     * Require administrator rights. Needed for $PROGRAMFILES; set false for a
     * per-user install under $LOCALAPPDATA.
     */
    elevate: z.boolean().default(true),
    /** Where shared state is persisted; defaults to Software/<publisher>/<product>. */
    registryKey: z.string().optional(),
    /** Offer to close the target app if it is running before overwriting it. */
    closeRunning: z.boolean().default(true),
    /** Launch the app when the user finishes. */
    runAfter: z.boolean().default(true),
  })
  .default({});

const uninstallSchema = z
  .object({
    /**
     * Give the uninstaller the same HTML interface as the installer.
     *
     * This costs disk: the renderer is 18 MB and the uninstaller cannot carry
     * its own copy without doubling the size of the installer, so the install
     * leaves one in `<installDir>/blink-runtime/` for the uninstaller to use.
     * Turn this off and the uninstaller falls back to NSIS's stock dialog,
     * with nothing extra on disk.
     */
    ui: z.boolean().default(true),
    /** Ask for a reason before uninstalling; the answer is logged to the store. */
    confirm: z.boolean().default(true),
  })
  .default({});

const windowSchema = z
  .object({
    width: z.number().int().positive().default(800),
    height: z.number().int().positive().default(560),
    /**
     * Layered window, so the page can have rounded corners and shadows. Turn
     * off only if you hit compositing trouble on very old systems.
     */
    transparent: z.boolean().default(true),
  })
  .default({});

const splashSchema = z
  .object({
    /**
     * Play a full-screen animation before the installer appears.
     *
     * The window is created covering the work area with per-pixel alpha, so
     * everything the animation does not draw stays transparent and the desktop
     * shows through. When it finishes, the same window shrinks to the installer
     * card — no second window, no reload.
     */
    enabled: z.boolean().default(false),
    /**
     * Hard cap. The page normally ends the splash itself; this is the backstop
     * so a scripting error cannot leave a full-screen window on the user's
     * desktop forever.
     */
    timeoutMs: z.number().int().positive().default(6000),
  })
  .default({});

const signSchema = z
  .object({
    /**
     * Module that signs one file, called as `hook({ path })`.
     *
     * Same shape as electron-builder's `win.signtoolOptions.sign`, on purpose:
     * point this at a hook you already have and it works unchanged.
     */
    hook: z.string().optional(),
    /** SHA-1 thumbprint of a certificate in the Windows store. Required for cloud/HSM certs. */
    thumbprint: z.string().optional(),
    /** Or a .pfx / .p12 file, relative to the config. */
    certificateFile: z.string().optional(),
    /** Prefer the BLINK_SIGN_PASSWORD environment variable over writing this down. */
    certificatePassword: z.string().optional(),
    /** RFC3161 server. Timestamping keeps signatures valid past certificate expiry. */
    timestamp: z.string().default('http://timestamp.digicert.com'),
    /** Explicit signtool.exe; auto-detected from the Windows SDK otherwise. */
    signtool: z.string().optional(),
    /** Hard limit per call, so a stalled cloud-auth prompt cannot wedge a build. */
    timeoutMs: z.number().int().positive().default(120000),
    /**
     * Fail the build when signing does not happen.
     *
     * Off by default so a lapsed session still produces something testable; on
     * for release runs, where shipping unsigned is worse than not shipping.
     */
    required: z.boolean().default(false),
  })
  .optional();

export const configSchema = z.object({
  /** Reverse-DNS identity, used for the uninstall registry key. */
  appId: z.string().min(1),
  productName: z.string().min(1),
  version: z.string().min(1),

  publisher: z.string().default('')  ,
  description: z.string().default(''),
  copyright: z.string().default(''),
  website: z.string().default(''),

  /** .ico used for setup.exe and the Programs and Features entry. */
  icon: z.string().optional(),

  /**
   * The already-packaged application directory. For Electron this is what
   * electron-builder --dir or electron-packager produced; for a traditional
   * Win32 app it is simply the folder holding your exe and its DLLs.
   */
  source: z.string().min(1),
  /** Main executable, relative to `source`. */
  exe: z.string().min(1),

  /** Output path for the installer. Defaults to dist/<product>-Setup-<version>.exe. */
  output: z.string().optional(),

  /** Directory containing index.html for a custom UI. Omit to use the default template. */
  ui: z.string().optional(),

  window: windowSchema,
  splash: splashSchema,
  install: installSchema,
  uninstall: uninstallSchema,

  /** lzma gives the smallest installer; zlib is fastest to build. */
  compression: z.enum(['zlib', 'bzip2', 'lzma']).default('lzma'),

  /** Authenticode signing. Omit and the installer ships unsigned. */
  sign: signSchema,

  nsis: z
    .object({
      /** Path to an .nsh injected into the generated script — the escape hatch. */
      include: z.string().optional(),
      /** Extra !define values. */
      defines: z.record(z.string(), z.string()).default({}),
    })
    .default({}),
});

export type BlinkInstallerConfig = z.input<typeof configSchema>;
export type ResolvedConfig = z.output<typeof configSchema>;

/** Identity helper giving editors full completion on the config object. */
export function defineConfig(config: BlinkInstallerConfig): BlinkInstallerConfig {
  return config;
}

export function parseConfig(input: unknown): ResolvedConfig {
  const result = configSchema.safeParse(input);
  if (!result.success) {
    const issues = result.error.issues
      .map((issue) => `  - ${issue.path.join('.') || '(root)'}: ${issue.message}`)
      .join('\n');
    throw new Error(`Invalid blink-installer config:\n${issues}`);
  }
  return result.data;
}
