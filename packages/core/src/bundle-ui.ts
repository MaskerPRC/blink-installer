/**
 * Collapses a UI directory into one self-contained HTML file.
 *
 * The installer has exactly one file to load from $PLUGINSDIR, so scripts,
 * styles, images and fonts all have to end up inside it. esbuild bundles the
 * JavaScript properly (modules, tree shaking, minification) and turns asset
 * imports into data URIs on the way, which is why the inlining is not just a
 * regex pass over the HTML.
 */
import { build, transform } from 'esbuild';
import { readFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, extname, isAbsolute, resolve } from 'node:path';

const ASSET_LOADERS = {
  '.png': 'dataurl',
  '.jpg': 'dataurl',
  '.jpeg': 'dataurl',
  '.gif': 'dataurl',
  '.svg': 'dataurl',
  '.webp': 'dataurl',
  '.woff': 'dataurl',
  '.woff2': 'dataurl',
  '.ttf': 'dataurl',
  '.otf': 'dataurl',
} as const;

const MIME_BY_EXTENSION: Record<string, string> = {
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif': 'image/gif',
  '.svg': 'image/svg+xml',
  '.webp': 'image/webp',
  '.ico': 'image/x-icon',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
  '.ttf': 'font/ttf',
  '.otf': 'font/otf',
};

/** Leaves absolute URLs, data URIs and anchors alone. */
function isExternal(reference: string): boolean {
  return (
    /^[a-z][a-z0-9+.-]*:/i.test(reference) ||
    reference.startsWith('//') ||
    reference.startsWith('#')
  );
}

async function toDataUri(file: string): Promise<string> {
  const mime = MIME_BY_EXTENSION[extname(file).toLowerCase()] ?? 'application/octet-stream';
  const bytes = await readFile(file);
  return `data:${mime};base64,${bytes.toString('base64')}`;
}

/**
 * What miniblink's JavaScript engine actually supports.
 *
 * Its user agent claims `Chrome/60`, but that is not what the parser
 * implements. Measured directly (see native/test — the probe page evals each
 * feature and reports which throw), this engine has classes, generators,
 * arrow functions, let/const, template literals, Promise, Map/Set and for...of,
 * but *not* destructuring, default parameters, object spread, async/await,
 * class fields, `?.`, `??`, `**`, or optional catch binding.
 *
 * That is an unusual mix, matching no real Chrome release, so the gaps are
 * named explicitly below rather than approximated with a version number. The
 * point is that a page author gets to write ordinary modern JavaScript and
 * have it work.
 *
 * Two gaps cannot be compiled away because they are library, not syntax:
 * `Object.entries` and `String.padStart` are absent. Avoid those.
 */
export const DEFAULT_CSS_TARGET = 'chrome58';

/**
 * Babel transforms the engine cannot do without.
 *
 * esbuild alone is not enough here: it declines to lower destructuring and
 * default parameters at all ("not supported yet"), and those are the two
 * things this engine most conspicuously lacks. So esbuild bundles and Babel
 * lowers.
 *
 * Note what is deliberately *not* in this list: the regenerator transform.
 * The engine has native generators, so async/await is compiled to a generator
 * plus a small inline helper rather than dragging in regenerator-runtime.
 */
const BABEL_INCLUDE = [
  '@babel/plugin-transform-destructuring',
  '@babel/plugin-transform-parameters',
  '@babel/plugin-transform-spread',
  '@babel/plugin-transform-object-rest-spread',
  '@babel/plugin-transform-async-to-generator',
  '@babel/plugin-transform-exponentiation-operator',
  '@babel/plugin-transform-optional-chaining',
  '@babel/plugin-transform-nullish-coalescing-operator',
  '@babel/plugin-transform-class-properties',
  '@babel/plugin-transform-optional-catch-binding',
  '@babel/plugin-transform-logical-assignment-operators',
];

/**
 * Where `blink-installer-ui` lives, resolved from this package.
 *
 * A UI directory is usually somewhere else entirely — inside the application's
 * own repository, next to its source — and requiring it to carry a
 * node_modules just so esbuild can find the SDK would be a poor trade. The
 * bundler aliases the import to the copy installed alongside core, so any
 * directory anywhere can `import { installer } from 'blink-installer-ui'`.
 *
 * Returns undefined when the package genuinely is not there, in which case
 * normal resolution applies and esbuild reports the missing import itself.
 */
function resolveUiSdk(): string | undefined {
  try {
    const require = createRequire(import.meta.url);
    return require.resolve('blink-installer-ui');
  } catch {
    return undefined;
  }
}

async function bundleScript(entry: string): Promise<string> {
  const uiSdk = resolveUiSdk();

  // 1. Bundle: resolve modules, inline asset imports, drop dead code.
  const bundled = await build({
    entryPoints: [entry],
    bundle: true,
    write: false,
    format: 'iife',
    platform: 'browser',
    target: ['esnext'],
    minify: false,
    loader: { ...ASSET_LOADERS },
    alias: uiSdk ? { 'blink-installer-ui': uiSdk } : undefined,
    logLevel: 'silent',
  });
  const code = bundled.outputFiles?.[0]?.text ?? '';

  // 2. Lower the syntax this engine does not parse.
  //
  // The preset is passed as a resolved absolute path, not by name. Babel
  // resolves preset names relative to the file being transformed — which is
  // the user's UI directory, in their own project, where @babel/preset-env has
  // no reason to be installed. Resolving it from here makes the pipeline work
  // regardless of where the page lives.
  //
  // The include/exclude entries stay as names: preset-env matches those
  // against its own plugin list rather than resolving them.
  const babel = await import('@babel/core');
  const require = createRequire(import.meta.url);
  const presetEnv = require.resolve('@babel/preset-env');

  const lowered = await babel.transformAsync(code, {
    babelrc: false,
    configFile: false,
    compact: false,
    sourceType: 'script',
    // Anchor any remaining resolution here rather than in the user's project.
    cwd: dirname(require.resolve('@babel/core/package.json')),
    presets: [
      [
        presetEnv,
        {
          // Close to the measured baseline; the include list covers the gaps
          // where this engine is older than the version it reports.
          targets: { chrome: '49' },
          include: BABEL_INCLUDE,
          exclude: ['@babel/plugin-transform-regenerator'],
          bugfixes: true,
        },
      ],
    ],
  });

  // 3. Minify. Nothing here introduces syntax, so the output stays parseable.
  const minified = await transform(lowered?.code ?? code, {
    minify: true,
    target: ['es2015'],
    logLevel: 'silent',
  });
  return minified.code;
}

async function bundleStylesheet(entry: string, target: string): Promise<string> {
  const result = await build({
    entryPoints: [entry],
    bundle: true,
    write: false,
    target: [target],
    minify: true,
    loader: { ...ASSET_LOADERS },
    logLevel: 'silent',
  });
  return result.outputFiles?.[0]?.text ?? '';
}

export interface BundleUiOptions {
  /** Directory containing the entry document. */
  uiDir: string;
  /**
   * Document to bundle, relative to `uiDir`. The installer and uninstaller
   * pages live in the same directory so they can share a stylesheet.
   */
  entry?: string;
  /** Injected as a global before any page script runs. */
  defines?: Record<string, unknown>;
  /** esbuild target for stylesheets. */
  cssTarget?: string;
}

/**
 * Returns the single-file HTML document.
 *
 * Rewriting is done with targeted regexes rather than a full HTML parser. That
 * is a deliberate trade: the input is a page the developer wrote for this
 * purpose, not arbitrary web content, and it keeps the dependency footprint at
 * one package. Anything it cannot resolve is left untouched rather than broken.
 */
export async function bundleUi(options: BundleUiOptions): Promise<string> {
  const entry = options.entry ?? 'index.html';
  const indexPath = resolve(options.uiDir, entry);
  if (!existsSync(indexPath)) {
    throw new Error(`No ${entry} found in UI directory: ${options.uiDir}`);
  }

  let html = await readFile(indexPath, 'utf8');
  const baseDir = dirname(indexPath);
  const cssTarget = options.cssTarget ?? DEFAULT_CSS_TARGET;

  const resolveRef = (reference: string): string | undefined => {
    if (isExternal(reference)) return undefined;
    const clean = reference.split(/[?#]/)[0] ?? reference;
    const full = isAbsolute(clean) ? clean : resolve(baseDir, clean);
    return existsSync(full) ? full : undefined;
  };

  // <script src="..."> -> inline bundle
  const scriptTags = [...html.matchAll(/<script\b[^>]*\bsrc\s*=\s*["']([^"']+)["'][^>]*>\s*<\/script>/gi)];
  for (const match of scriptTags) {
    const entry = resolveRef(match[1]!);
    if (!entry) continue;
    const code = await bundleScript(entry);
    // Replacer *function*, not a template string: in a replacement string
    // `$&`, `` $` `` and `$'` are substitution patterns, and bundled output
    // routinely contains `$` as a helper identifier. A stray `$'` spliced the
    // remainder of the document into the script tag, which rendered the page's
    // own source as visible text.
    html = html.replace(match[0], () => `<script>${code}</script>`);
  }

  // <link rel="stylesheet" href="..."> -> inline <style>
  const linkTags = [...html.matchAll(/<link\b[^>]*\bhref\s*=\s*["']([^"']+)["'][^>]*>/gi)];
  for (const match of linkTags) {
    if (!/stylesheet/i.test(match[0])) continue;
    const entry = resolveRef(match[1]!);
    if (!entry) continue;
    const css = await bundleStylesheet(entry, cssTarget);
    html = html.replace(match[0], () => `<style>${css}</style>`);
  }

  // Remaining src/href attributes that point at local assets -> data URI.
  const attrRefs = [...html.matchAll(/\b(src|href)\s*=\s*["']([^"']+)["']/gi)];
  for (const match of attrRefs) {
    const reference = match[2]!;
    if (reference.startsWith('data:')) continue;
    const file = resolveRef(reference);
    if (!file || extname(file) === '.html') continue;
    const uri = await toDataUri(file);
    html = html.replace(match[0], () => `${match[1]}="${uri}"`);
  }

  // url(...) inside any surviving inline <style> block.
  const urlRefs = [...html.matchAll(/url\(\s*["']?([^"')]+)["']?\s*\)/gi)];
  for (const match of urlRefs) {
    const reference = match[1]!;
    if (reference.startsWith('data:')) continue;
    const file = resolveRef(reference);
    if (!file) continue;
    const uri = await toDataUri(file);
    html = html.replace(match[0], () => `url("${uri}")`);
  }

  if (options.defines && Object.keys(options.defines).length > 0) {
    const injected = `<script>window.__BLINK_DEFINES__=${JSON.stringify(options.defines)};</script>`;
    html = html.includes('</head>')
      ? html.replace('</head>', () => `${injected}</head>`)
      : injected + html;
  }

  return html;
}
