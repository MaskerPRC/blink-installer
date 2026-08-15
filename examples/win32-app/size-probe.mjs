/**
 * Isolates window size from the splash code path.
 *
 * Builds the installer with the splash off and a very large ordinary window.
 * If the card renders at 2560x1440 the problem is the fullscreen path or the
 * splash stylesheet; if it does not, miniblink simply cannot present a layered
 * surface that big and the animation has to live on a smaller one.
 */
import { buildInstaller } from 'blink-installer-core';

const sizes = [
  [1280, 800],
  [1920, 1080],
  [2560, 1440],
];

for (const [width, height] of sizes) {
  const result = await buildInstaller({
    config: {
      appId: 'com.example.demoapp',
      productName: 'Demo App',
      version: '1.0.0',
      publisher: 'Example Corp',
      source: './app',
      exe: 'DemoApp.exe',
      compression: 'zlib',
      window: { width, height },
      splash: { enabled: false },
      install: { defaultDir: '$LOCALAPPDATA\\Demo App', elevate: false },
      output: `./dist/size-${width}x${height}.exe`,
    },
    onLog: () => {},
  });
  console.log(`${width}x${height} -> ${(result.size / 1024 / 1024).toFixed(1)} MB`);
}
