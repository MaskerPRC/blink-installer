import assert from 'node:assert/strict';
import { describe, it } from 'node:test';

import { parseConfig } from '../dist/config.js';
import { renderInstallerScript, renderUninstallerScript, type NsiInputs } from '../dist/nsi.js';

function inputs(overrides: Record<string, unknown> = {}): NsiInputs {
  return {
    config: parseConfig({
      appId: 'com.example.app',
      productName: 'App',
      version: '1.0.0',
      source: './out',
      exe: 'app.exe',
      ...overrides,
    }),
    htmlPath: 'C:\\stage\\index.min.html',
    uninstallHtmlPath: 'C:\\stage\\uninstall.min.html',
    miniblinkPath: 'C:\\rt\\node.dll',
    pluginDir: 'C:\\rt',
    sourceDir: 'C:\\app',
    outputPath: 'C:\\dist\\Setup.exe',
  };
}

describe('installer script', () => {
  it('puts /NOUNLOAD on every plugin call', () => {
    const script = renderInstallerScript(inputs());
    const calls = script.match(/blinkkit::\w+[^\r\n]*/g) ?? [];
    assert.ok(calls.length > 5, 'expected several plugin calls');
    for (const call of calls) {
      // Without this NSIS frees the DLL as soon as the call returns, taking
      // the window, the ability registry and the config store with it.
      assert.match(call, /\/NOUNLOAD/, `missing /NOUNLOAD: ${call}`);
    }
  });

  it('leaves NSIS variables in defaultDir unescaped', () => {
    const script = renderInstallerScript(
      inputs({ install: { defaultDir: '$LOCALAPPDATA\\App' } }),
    );
    // Escaping this to `$$LOCALAPPDATA` made the installer offer to install
    // into a directory literally named "$LOCALAPPDATA".
    assert.match(script, /InstallDir "\$LOCALAPPDATA\\App"/);
    assert.doesNotMatch(script, /InstallDir "\$\$LOCALAPPDATA/);
  });

  it('escapes dollar signs in real filesystem paths', () => {
    const base = inputs();
    const script = renderInstallerScript({ ...base, outputPath: 'C:\\a$b\\Setup.exe' });
    assert.match(script, /OutFile "C:\\a\$\$b\\Setup\.exe"/);
  });

  it('uses HKLM and the all-users context when elevating', () => {
    const script = renderInstallerScript(inputs({ install: { elevate: true } }));
    assert.match(script, /RequestExecutionLevel admin/);
    assert.match(script, /SetShellVarContext all/);
    assert.match(script, /WriteRegStr HKLM/);
  });

  it('falls back to HKCU and the current user for a per-user install', () => {
    // A per-user install has no rights to HKLM; writing there fails silently
    // and the app ends up with no Programs-and-Features entry.
    const script = renderInstallerScript(inputs({ install: { elevate: false } }));
    assert.match(script, /RequestExecutionLevel user/);
    assert.match(script, /SetShellVarContext current/);
    assert.match(script, /WriteRegStr HKCU/);
    assert.doesNotMatch(script, /WriteRegStr HKLM/);
  });

  it('honours /S by skipping the UI pages', () => {
    const script = renderInstallerScript(inputs());
    assert.match(script, /\$\{Silent\}/);
  });

  it('declares Unicode and DPI awareness', () => {
    const script = renderInstallerScript(inputs());
    assert.match(script, /^Unicode true/m);
    assert.match(script, /^ManifestDPIAware true/m);
  });

  it('includes WinMessages for ${SW_HIDE}', () => {
    // Without it NSIS substitutes an empty string and its own window stays up.
    const script = renderInstallerScript(inputs());
    assert.match(script, /!include "WinMessages\.nsh"/);
  });

  it('omits the shortcut commands that are turned off', () => {
    const script = renderInstallerScript(
      inputs({ install: { shortcuts: { desktop: false, startMenu: true } } }),
    );
    assert.doesNotMatch(script, /CreateShortCut "\$DESKTOP/);
    assert.match(script, /CreateShortCut "\$SMPROGRAMS/);
  });
});

describe('uninstaller script', () => {
  it('prefixes every uninstaller function with un.', () => {
    const script = renderUninstallerScript(inputs());
    const functions = script.match(/^Function (\w[\w.]*)/gm) ?? [];
    assert.ok(functions.length > 0);
    for (const fn of functions) {
      assert.match(fn, /^Function un\./, `${fn} is not in the uninstaller namespace`);
    }
  });

  it('reuses the renderer the installer parked instead of embedding one', () => {
    const script = renderUninstallerScript(inputs());
    // Embedding a second 18 MB renderer would double the installer download.
    assert.match(script, /blink-runtime\\node\.dll/);
    assert.match(script, /CopyFiles \/SILENT/);
  });

  it('degrades to the stock dialog when the parked renderer is missing', () => {
    const script = renderUninstallerScript(inputs());
    assert.match(script, /IfFileExists/);
    assert.match(script, /\$UnHasUi/);
  });

  it('removes shortcuts, the install directory and both registry keys', () => {
    const script = renderUninstallerScript(inputs());
    assert.match(script, /Delete "\$DESKTOP/);
    assert.match(script, /RMDir \/r "\$INSTDIR"/);
    assert.match(script, /DeleteRegKey \w+ "Software\\Microsoft\\Windows/);
  });

  it('skips the UI entirely without an uninstall page', () => {
    const base = inputs();
    const script = renderUninstallerScript({ ...base, uninstallHtmlPath: undefined });
    assert.doesNotMatch(script, /UninstPage custom/);
    assert.match(script, /Section "Uninstall"/);
  });
});
