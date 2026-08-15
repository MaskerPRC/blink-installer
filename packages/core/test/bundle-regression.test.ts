import assert from 'node:assert/strict';
import { mkdtemp, mkdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { after, before, describe, it } from 'node:test';

import { bundleUi } from '../dist/bundle-ui.js';

let dir: string;
before(async () => {
  dir = await mkdtemp(join(tmpdir(), 'blink-regress-'));
});
after(async () => {
  await rm(dir, { recursive: true, force: true });
});

describe('inlining is literal', () => {
  /**
   * `String.replace` treats `$&`, `` $` ``, `$'` and `$1` in a *replacement
   * string* as substitution patterns. Bundled JavaScript routinely contains a
   * `$` helper identifier, and one `$'` spliced the remainder of the document
   * into the script tag — the page then rendered its own source as visible
   * text across the installer window.
   */
  it('does not let $-patterns in bundled code splice the document', async () => {
    const uiDir = join(dir, 'dollar');
    await mkdir(uiDir, { recursive: true });
    await writeFile(
      join(uiDir, 'app.js'),
      // Every replacement pattern, in code that is otherwise unremarkable.
      "var $ = 'x'; var a = $ + '&' + \"$&\" + \"$'\" + '$`' + '$1';\n" +
        "window.marker = 'SENTINEL_' + a;",
    );
    await writeFile(
      join(uiDir, 'index.html'),
      '<!doctype html><html><head></head><body>' +
        '<div id="after">TAIL_CONTENT</div>' +
        '<script src="./app.js"></script>' +
        '</body></html>',
    );

    const html = await bundleUi({ uiDir });

    assert.match(html, /SENTINEL_/, 'script was not inlined');
    // The tail of the document must appear exactly once. A `$'` substitution
    // duplicates everything after the match into the replacement.
    const tails = html.split('TAIL_CONTENT').length - 1;
    assert.equal(tails, 1, 'document content was duplicated into the script');
    assert.equal(html.split('<script>').length - 1, 1, 'more than one script tag');
  });

  it('keeps $-patterns literal in stylesheets and data URIs', async () => {
    const uiDir = join(dir, 'dollar-css');
    await mkdir(uiDir, { recursive: true });
    await writeFile(join(uiDir, 'app.css'), 'body::after { content: "$&$\'$`"; }');
    await writeFile(
      join(uiDir, 'index.html'),
      '<!doctype html><html><head><link rel="stylesheet" href="./app.css"></head>' +
        '<body>UNIQUE_TAIL</body></html>',
    );

    const html = await bundleUi({ uiDir });
    assert.equal(html.split('UNIQUE_TAIL').length - 1, 1, 'document content was duplicated');
  });
});

describe('resolving the page SDK', () => {
  /**
   * A UI directory normally lives in the application's own repository, with no
   * node_modules of its own. The bundler aliases `blink-installer-ui` to the
   * copy installed beside core so that still works.
   */
  it('bundles a page that imports blink-installer-ui from anywhere', async () => {
    const uiDir = join(dir, 'sdk');
    await mkdir(uiDir, { recursive: true });
    await writeFile(
      join(uiDir, 'app.js'),
      "import { installer } from 'blink-installer-ui';\nwindow.probe = typeof installer.begin;",
    );
    await writeFile(
      join(uiDir, 'index.html'),
      '<!doctype html><html><body><script src="./app.js"></script></body></html>',
    );

    const html = await bundleUi({ uiDir });
    assert.match(html, /probe/, 'page script was not bundled');
    assert.doesNotMatch(html, /@blink-installer\/ui/, 'the import was left unresolved');
  });
});
