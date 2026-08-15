import { defineConfig } from 'blink-installer-core';

export default defineConfig({
  appId: 'com.example.demoapp',
  productName: 'Demo App',
  version: '1.0.0',
  publisher: 'Example Corp',
  description: 'A demonstration of blink-installer',
  website: 'https://example.com',

  // A traditional Win32 payload: just the folder holding the exe.
  source: './app',
  exe: 'DemoApp.exe',

  output: './dist/DemoApp-Setup-1.0.0.exe',

  // No `ui` key, so this uses the built-in template. Run
  // `blink-installer init --eject-ui` to get an editable copy.

  // Full-screen entrance animation. The window opens covering the work area
  // with per-pixel alpha, so everything the animation does not draw is
  // transparent, then shrinks to the installer card.
  splash: { enabled: true },

  install: {
    // A per-user install, so the example runs without an elevation prompt.
    // Switch to $PROGRAMFILES and elevate: true for a machine-wide install.
    defaultDir: '$LOCALAPPDATA\\Demo App',
    shortcuts: { desktop: true, startMenu: true },
    elevate: false,
  },

  // zlib builds fastest; switch to lzma for release builds.
  compression: 'zlib',
});
