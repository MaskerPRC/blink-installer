import assert from 'node:assert/strict';
import { mkdtemp, rm, writeFile, mkdir } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { after, before, describe, it } from 'node:test';

import { bundleUi } from '../dist/bundle-ui.js';

let dir: string;

before(async () => {
  dir = await mkdtemp(join(tmpdir(), 'blink-ui-test-'));
});

after(async () => {
  await rm(dir, { recursive: true, force: true });
});

describe('bundleUi', () => {
  it('inlines scripts, styles and images into one document', async () => {
    const uiDir = join(dir, 'basic');
    await mkdir(uiDir, { recursive: true });
    await writeFile(join(uiDir, 'app.js'), 'document.title = "bundled";');
    await writeFile(join(uiDir, 'app.css'), 'body { color: rebeccapurple; }');
    // 1x1 transparent GIF.
    await writeFile(
      join(uiDir, 'dot.gif'),
      Buffer.from('R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7', 'base64'),
    );
    await writeFile(
      join(uiDir, 'index.html'),
      `<!doctype html><html><head><link rel="stylesheet" href="./app.css"></head>` +
        `<body><img src="./dot.gif"><script src="./app.js"></script></body></html>`,
    );

    const html = await bundleUi({ uiDir });

    assert.match(html, /<style>/, 'stylesheet was not inlined');
    // The minifier rewrites named colours to hex, so match the declaration
    // rather than the literal source text.
    assert.match(html, /body\s*\{\s*color:\s*(#639|rebeccapurple)/);
    assert.match(html, /bundled/, 'script was not inlined');
    assert.match(html, /src="data:image\/gif;base64,/, 'image was not inlined');
    assert.doesNotMatch(html, /src="\.\/app\.js"/, 'script tag still references a file');
    assert.doesNotMatch(html, /href="\.\/app\.css"/, 'link tag still references a file');
  });

  it('lowers syntax the miniblink engine cannot parse', async () => {
    const uiDir = join(dir, 'modern');
    await mkdir(uiDir, { recursive: true });
    // Every one of these is a SyntaxError in miniblink's engine.
    await writeFile(
      join(uiDir, 'app.js'),
      `const { a, b } = { a: 1, b: 2 };
       function withDefault(x = 5) { return x; }
       const spread = { ...{ c: 3 } };
       async function go() { await Promise.resolve(1); }
       const chained = globalThis?.location ?? 'none';
       try { go(); } catch { /* ignore */ }
       window.result = [a, b, withDefault(), spread.c, chained];`,
    );
    await writeFile(
      join(uiDir, 'index.html'),
      `<!doctype html><html><body><script src="./app.js"></script></body></html>`,
    );

    const html = await bundleUi({ uiDir });
    const script = html.slice(html.indexOf('<script>'), html.indexOf('</script>'));

    assert.doesNotMatch(script, /\?\?/, 'nullish coalescing survived');
    assert.doesNotMatch(script, /\?\./, 'optional chaining survived');
    assert.doesNotMatch(script, /\basync\b/, 'async survived');
    assert.doesNotMatch(script, /\bawait\b/, 'await survived');
    assert.doesNotMatch(script, /\.\.\./, 'spread survived');
    // Destructuring and default parameters are the two esbuild refuses to
    // lower, which is the whole reason Babel is in the pipeline.
    assert.doesNotMatch(script, /(var|let|const)\s*\{/, 'destructuring survived');
  });

  it('injects build-time defines ahead of page scripts', async () => {
    const uiDir = join(dir, 'defines');
    await mkdir(uiDir, { recursive: true });
    await writeFile(join(uiDir, 'index.html'), `<!doctype html><html><head></head><body></body></html>`);

    const html = await bundleUi({ uiDir, defines: { productName: 'Acme' } });
    assert.match(html, /__BLINK_DEFINES__=\{"productName":"Acme"\}/);
    assert.ok(
      html.indexOf('__BLINK_DEFINES__') < html.indexOf('</head>'),
      'defines must be set before any page script runs',
    );
  });

  it('bundles an alternate entry document', async () => {
    const uiDir = join(dir, 'entry');
    await mkdir(uiDir, { recursive: true });
    await writeFile(join(uiDir, 'index.html'), `<!doctype html><title>install</title>`);
    await writeFile(join(uiDir, 'uninstall.html'), `<!doctype html><title>remove</title>`);

    const html = await bundleUi({ uiDir, entry: 'uninstall.html' });
    assert.match(html, /remove/);
    assert.doesNotMatch(html, /install/);
  });

  it('reports a missing entry rather than producing an empty page', async () => {
    const uiDir = join(dir, 'empty');
    await mkdir(uiDir, { recursive: true });
    await assert.rejects(() => bundleUi({ uiDir }), /No index\.html found/);
  });

  it('leaves remote references alone', async () => {
    const uiDir = join(dir, 'remote');
    await mkdir(uiDir, { recursive: true });
    await writeFile(
      join(uiDir, 'index.html'),
      `<!doctype html><html><body><a href="https://example.com/x">x</a></body></html>`,
    );
    const html = await bundleUi({ uiDir });
    assert.match(html, /href="https:\/\/example\.com\/x"/);
  });
});
