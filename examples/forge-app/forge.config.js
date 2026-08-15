/**
 * Electron Forge configuration.
 *
 * The blink-installer maker slots in alongside any other maker. Forge packages
 * the app first and hands the resulting directory to us, so there is no
 * separate build step and nothing to keep in sync.
 */
module.exports = {
  packagerConfig: {
    asar: true,
  },
  makers: [
    {
      name: 'blink-installer-maker',
      config: {
        appId: 'com.example.forgedemo',
        // productName, version and publisher come from package.json; the
        // executable name comes from Forge. Everything else is yours.
        install: {
          // Per-user, so `electron-forge make` needs no elevation prompt.
          defaultDir: '$LOCALAPPDATA\\Forge Demo',
          elevate: false,
          shortcuts: { desktop: true, startMenu: true },
        },
        compression: 'zlib',
      },
    },
    {
      // Kept to show the maker coexists with the stock ones.
      name: '@electron-forge/maker-zip',
      platforms: ['win32'],
    },
  ],
};
