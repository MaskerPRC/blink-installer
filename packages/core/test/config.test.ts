import assert from 'node:assert/strict';
import { describe, it } from 'node:test';

import { DEFAULT_INSTALL_DIR, normalizeVersion, parseConfig } from '../dist/config.js';

const minimal = {
  appId: 'com.example.app',
  productName: 'App',
  version: '1.0.0',
  source: './out',
  exe: 'app.exe',
};

describe('normalizeVersion', () => {
  it('pads to the four components VIProductVersion requires', () => {
    assert.equal(normalizeVersion('1.2.3'), '1.2.3.0');
    assert.equal(normalizeVersion('1.2'), '1.2.0.0');
    assert.equal(normalizeVersion('7'), '7.0.0.0');
  });

  it('keeps four components as they are', () => {
    assert.equal(normalizeVersion('1.2.3.4'), '1.2.3.4');
  });

  it('truncates beyond four', () => {
    assert.equal(normalizeVersion('1.2.3.4.5'), '1.2.3.4');
  });

  it('strips prerelease and build metadata', () => {
    // NSIS rejects anything non-numeric here, so `1.2.3-beta.1` would fail the
    // build at the very last step if it survived.
    assert.equal(normalizeVersion('1.2.3-beta.1'), '1.2.3.0');
    assert.equal(normalizeVersion('1.2.3+sha.abc'), '1.2.3.0');
  });
});

describe('parseConfig', () => {
  it('applies defaults', () => {
    const config = parseConfig(minimal);
    assert.equal(config.compression, 'lzma');
    assert.equal(config.install.elevate, true);
    assert.equal(config.install.shortcuts.desktop, true);
    assert.equal(config.uninstall.ui, true);
    assert.equal(config.window.width, 800);
    assert.equal(config.publisher, '');
  });

  it('rejects a config missing required fields, naming them', () => {
    assert.throws(
      () => parseConfig({ productName: 'App' }),
      (error: Error) => {
        assert.match(error.message, /appId/);
        assert.match(error.message, /version/);
        assert.match(error.message, /source/);
        return true;
      },
    );
  });

  it('rejects an unknown compression method', () => {
    assert.throws(() => parseConfig({ ...minimal, compression: 'brotli' }), /compression/);
  });

  it('keeps nested overrides without dropping sibling defaults', () => {
    const config = parseConfig({
      ...minimal,
      install: { elevate: false, shortcuts: { desktop: false } },
    });
    assert.equal(config.install.elevate, false);
    assert.equal(config.install.shortcuts.desktop, false);
    // Not overridden, so it must still be the default rather than undefined.
    assert.equal(config.install.shortcuts.startMenu, true);
    assert.equal(config.install.uninstallEntry, true);
  });
});

describe('the install directory name', () => {
  // The product name is a display name. Deriving the install path from it gave
  // every product with a Chinese title a Chinese default directory.
  const chinese = { ...minimal, productName: '我的应用' };

  it('leaves an ASCII product name deriving the default path', () => {
    const config = parseConfig(minimal);
    assert.equal(config.install.defaultDir, DEFAULT_INSTALL_DIR);
    assert.equal(config.install.folderName, undefined);
  });

  it('accepts a product name with spaces', () => {
    // 'My App' has always worked and is an ordinary thing to write.
    assert.doesNotThrow(() => parseConfig({ ...minimal, productName: 'My App' }));
  });

  it('refuses to derive a path from a name that is not one, naming the fix', () => {
    assert.throws(
      () => parseConfig(chinese),
      (error: Error) => {
        assert.match(error.message, /productName/);
        assert.match(error.message, /install\.folderName/);
        return true;
      },
    );
  });

  it('accepts such a product name once the directory is named separately', () => {
    const config = parseConfig({ ...chinese, install: { folderName: 'MyApp' } });
    assert.equal(config.install.folderName, 'MyApp');
    // The display name is untouched: the window title and shortcuts want it.
    assert.equal(config.productName, '我的应用');
  });

  it('leaves a config that chose its own defaultDir alone', () => {
    // Already a deliberate decision, whatever it says.
    assert.doesNotThrow(() =>
      parseConfig({ ...chinese, install: { defaultDir: '$PROGRAMFILES\\我的应用' } }),
    );
  });

  it('rejects a folderName that is not a usable directory name', () => {
    assert.throws(() => parseConfig({ ...minimal, install: { folderName: 'bad/name' } }), /folderName/);
    assert.throws(() => parseConfig({ ...minimal, install: { folderName: '我的应用' } }), /folderName/);
    // Windows strips a trailing dot, so the directory is created under one name
    // and looked up under another.
    assert.throws(() => parseConfig({ ...minimal, install: { folderName: 'MyApp.' } }), /folderName/);
  });
});
