import { defineConfig } from 'blink-installer-core';

/**
 * electron-builder produces the unpacked directory; we wrap it.
 *
 * There is no plugin or hook to keep in sync — `electron-builder --dir` writes
 * `out/win-unpacked` and this config points at it. Run both with `npm run make`.
 */
export default defineConfig({
  appId: 'com.example.builderdemo',
  productName: 'Builder Demo',
  version: '3.4.0',
  publisher: 'Example Corp',
  description: 'electron-builder + blink-installer',

  source: 'out/win-unpacked',
  exe: 'Builder Demo.exe',
  output: 'out/Builder Demo-Setup-3.4.0.exe',

  install: {
    defaultDir: '$LOCALAPPDATA\\Builder Demo',
    elevate: false,
    shortcuts: { desktop: true, startMenu: true },
  },

  compression: 'zlib',
});
