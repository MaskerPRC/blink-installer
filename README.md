# blink-installer

Build Windows installers whose user interface is HTML, CSS and JavaScript.

![A full-screen entrance animation converging into the installer card](docs/demo.gif)

NSIS does the compression, extraction and uninstall registration. miniblink
renders the page. Your installer looks like a product instead of a wizard from
1998, and you style it with CSS.

Everything above is one transparent, per-pixel-alpha window: the animation
plays over the desktop, then the same window becomes the installer. No second
process, no reload, nothing visibly moves at the handoff.

```
npm i -D blink-installer
npx blink-installer init
npx blink-installer build
```

**Status:** working end to end — install, silent install and uninstall are all
verified — but young. Interfaces may still move.

---

## What it is

A build tool. It takes a directory you have already packaged and wraps it in an
installer:

```
your packaged app  ─┐
your HTML UI       ─┼─▶  blink-installer  ─▶  MyApp-Setup-1.2.3.exe
your config        ─┘
```

It does not package your app. Electron users keep using electron-builder
(`--dir`) or Electron Forge; traditional Win32 users just point at the folder
holding their exe. That boundary is deliberate — there is no value in
reimplementing what those tools already do well.

## Quick start

```js
// blink-installer.config.mjs
import { defineConfig } from 'blink-installer-core';

export default defineConfig({
  appId: 'com.example.myapp',
  productName: 'My App',
  version: '1.2.3',
  publisher: 'Example Corp',

  source: 'out/myapp-win32-x64',   // already-packaged directory
  exe: 'myapp.exe',

  install: {
    defaultDir: '$PROGRAMFILES\\My App',
    shortcuts: { desktop: true, startMenu: true },
    elevate: true,
  },
});
```

```
npx blink-installer build
```

Omit `ui` and you get the bundled template: a three-screen installer with a
directory picker, free-space check, progress and a completion screen. Run
`npx blink-installer init --eject-ui` to copy it into your project and edit it.

### Two build modes

Compressing the payload dominates build time, and while you are working on the
interface none of that work is for you. So use two scripts:

```json
"installer:dev":     "blink-installer build --ui-only --compression zlib",
"installer:release": "blink-installer build --compression lzma"
```

`--ui-only` swaps your application for a placeholder. Everything else is real —
the generated script, the pages, the progress wiring, the uninstaller — so you
can run the result and click through the whole flow. On a 1.1 GB Electron app
this is the difference between a couple of seconds and well over a minute per
iteration. The artifact is a preview and not something to ship; give it its own
`--out` path so it cannot be mistaken for a release.

`--compression` trades size against time, and the spread is wide: on that same
payload, `zlib` took 79 s to produce 496 MB while `lzma` took about eight
minutes to produce 397 MB. Release builds want `lzma` — it is also what
electron-builder's own NSIS target uses, so choosing anything else is where a
surprising size difference between the two will come from.

## Electron Forge

A custom maker, so `electron-forge make` produces the installer directly:

```js
// forge.config.js
module.exports = {
  makers: [
    {
      name: 'blink-installer-maker',
      config: {
        appId: 'com.example.myapp',
        ui: './installer-ui',
      },
    },
  ],
};
```

Product name, version, publisher and the executable name are taken from what
Forge already knows; anything in `config` overrides them.

## electron-builder

Build the unpacked directory, then hand it over:

```
electron-builder --dir
npx blink-installer build
```

with `source: 'dist/win-unpacked'` in your config.

## What it costs

Measured with an 11 KB payload, so these numbers are the framework itself.

**Installer size**

| Compression | With HTML uninstaller | Without | Build time |
| --- | --- | --- | --- |
| `lzma` (default) | **6.09 MB** | 5.96 MB | ~9 s |
| `bzip2` | 7.32 MB | — | ~1 s |
| `zlib` | 7.88 MB | 7.73 MB | ~1 s |

**Disk after install**

| | With HTML uninstaller | Without |
| --- | --- | --- |
| Added to the install directory | **17.20 MB** | **0.05 MB** |
| | `node.dll` 17.85 MB + `Uninstall.exe` 169 KB | `Uninstall.exe` 39 KB |

Nearly all of it is miniblink. Our own plugin is 300 KB and both pages together
are 28 KB.

The HTML uninstaller has a lopsided cost: it adds 0.13 MB to the download but
17.15 MB on disk, because the uninstaller cannot carry its own renderer without
doubling the installer, so the install parks one for it. If disk matters more
than download, set `uninstall: { ui: false }` and the uninstaller falls back to
NSIS's dialog with nothing left behind.

For an Electron app — typically 150–300 MB — this is noise. For a 2 MB native
app it is not, and that is the honest trade for having a browser render your
installer.

## Writing the UI

Your page is an ordinary web page. It talks to the installer through a typed
API:

```js
import { installer, config, fs, proc, win } from 'blink-installer-ui';

const dir = await fs.pickDirectory({ title: 'Choose a folder' });
config.set('installDir', dir);

installer.on('progress', ({ percent }) => bar.style.width = percent + '%');
installer.on('log', ({ message }) => detail.textContent = message);

document.querySelector('#install').onclick = () => installer.begin();
```

### Asking the user something

`ui.messageBox` draws in the page, not through Win32, so a confirmation looks
like the rest of your installer instead of a grey system dialog dropped into
the middle of it:

```js
const answer = await ui.messageBox({
  title: 'Quit setup?',
  message: 'Installation is in progress. Leaving now leaves it incomplete.',
  buttons: 'yesNo',
  icon: 'warning',
});
if (answer === 'yes') win.close(true);
```

Three levels of control, in the order you are likely to need them:

1. **Nothing.** The defaults suit a dark card and the button labels follow the
   engine locale (English, or Chinese when it reports `zh`).
2. **Custom properties.** Override `--bk-dialog-surface`, `--bk-dialog-accent`,
   `--bk-dialog-radius` and friends to match your palette. The ejected template
   already wires these to its own theme variables.
3. **Your own markup.** Restyle the `.bk-dialog*` classes — your stylesheet
   loads after the defaults, so source order is enough and you do not need
   `!important` — or hand over the whole thing with
   `ui.setDialogRenderer(fn)` and resolve the answer yourself.
   `ui.setDialogLabels({ ok: '知道了' })` covers the common case of just wanting
   different words.

`ui.messageBoxNative` still reaches the Win32 box. Keep it for failures early
enough that the page cannot be trusted to draw — asking a broken page to render
its own error report does not work.

### Display scaling

Sizes in your config and your CSS are **logical pixels**. The installer reads
the system scaling once at startup, sizes the window accordingly and zooms the
page to match, so `width: 880` and `font-size: 14px` look the same on a 150%
laptop as on a 100% desktop. You do not need a media query and you should not
compensate by hand.

The same applies across the boundary: `sys.screen()` reports logical pixels and
`win.setBounds` / `win.resize` take them, so arithmetic mixing a CSS size with a
screen rectangle stays consistent. `sys.screen().scale` exposes the factor for
the rare case you genuinely need device pixels.

### Entrance animation

`splash: { enabled: true }` opens the window covering the work area with
per-pixel alpha and plays an animation before the installer appears. Everything
the animation does not draw is transparent — the desktop shows through — and
when it finishes the same window shrinks to the installer card. One window, no
reload, nothing visibly moves at the handoff.

The bundled template converges light streaks into a glowing core, pulses two
rings, raises the wordmark and fades the card in over about 2.2 seconds. Edit
`splash.js` and `splash.css` in your ejected UI to replace it. If it throws or
overruns `splash.timeoutMs`, the window falls back to the installer rather than
leaving a full-screen transparent window on the user's desktop.

**Frame rate.** miniblink presents at a hard **~33 fps** while
`requestAnimationFrame` fires at ~59 Hz, so about half of the frames a page
computes are never shown. This is not tunable: it is identical across five
miniblink releases from 2017 to 2021, at every window size, with or without
per-pixel alpha, and regardless of the `drawMinInterval` debug config.
`native/test/FINDINGS.md` has the measurements.

Design for it. Large, slow, eased movement with trails and glows reads fine at
33 fps; small fast translations judder. Driving your animation loop faster than
that only burns CPU.

### The engine is old — this matters

miniblink's renderer claims `Chrome/60` but its JavaScript parser is older than
that. Measured directly, it has **no** destructuring, default parameters,
object spread, `async`/`await`, class fields, `?.`, `??`, `**`, or optional
catch binding. It *does* have classes, generators, arrow functions, `let`,
template literals, `Promise`, `Map`/`Set` and `for...of`.

You do not have to care: the build runs esbuild and then Babel, lowering all of
the above so you can write normal modern JavaScript. Two gaps are libraries
rather than syntax and cannot be compiled away — **avoid `Object.entries` and
`String.padStart`**.

CSS is not transpiled, so avoid `position: sticky` on flex children and check
anything newer than about 2017 before relying on it.

Content has one trap of its own: **characters above U+FFFF draw as nothing**.
Most pictorial emoji live up there, so `🕑` (U+1F551) and `💬` (U+1F4AC) come
out blank while `✋` (U+270B) and `✉` (U+2709) are fine. If an icon has to be a
character, keep it inside the Basic Multilingual Plane; otherwise use an inline
SVG.

`native/test/` holds the probe page that produced these results; point it at a
different miniblink build to re-measure.

## Configuration

| Field | Meaning |
| --- | --- |
| `appId` | Reverse-DNS id; used for the uninstall registry key |
| `productName`, `version`, `publisher` | Shown in the UI and in file properties |
| `source` | The packaged directory to install |
| `exe` | Main executable, relative to `source` |
| `output` | Installer path (default `dist/<product>-Setup-<version>.exe`) |
| `ui` | Directory containing `index.html`; omit for the bundled template |
| `icon` | `.ico` for setup.exe and Programs and Features |
| `window` | `width`, `height`, `transparent` |
| `splash.enabled` | Full-screen entrance animation before the installer (default `false`) |
| `splash.timeoutMs` | Backstop if the animation hangs (default 6000) |
| `install.defaultDir` | May use NSIS variables such as `$PROGRAMFILES` |
| `install.elevate` | `true` requires admin and installs machine-wide (HKLM); `false` is per-user (HKCU) |
| `install.shortcuts` | `desktop`, `startMenu`, `startMenuFolder` |
| `install.uninstallEntry` | Generate an uninstaller and register it |
| `uninstall.ui` | Give the uninstaller the same HTML interface (default `true`; see the size table) |
| `compression` | `lzma` (smallest), `bzip2`, `zlib` (fastest build) |
| `sign` | Authenticode signing; omit to ship unsigned |
| `nsis.include` | Path to an `.nsh` injected into the generated script |

## Signing

An unsigned installer gets a SmartScreen warning on every download until enough
people click through it, so this matters more than it looks.

If you already package with electron-builder you almost certainly have a signing
hook, and this takes the same shape as its `win.signtoolOptions.sign`. Point at
the file you have:

```js
sign: { hook: './electron/sign-win.cjs' }
```

Signing setups accumulate detail that is expensive to rediscover — cloud
certificates with no exportable key, sessions that lapse mid-build, timeouts for
prompts an unattended build cannot answer. Reusing the hook keeps one
implementation rather than two that drift.

With nothing yet, drive signtool directly. A certificate in the Windows store,
by thumbprint — the only option for a cloud/HSM certificate:

```js
sign: { thumbprint: 'C3C1…91ED', timestamp: 'http://time.certum.pl' }
```

or a file, with the password from the environment rather than the config:

```js
sign: { certificateFile: './cert.pfx' }   // BLINK_SIGN_PASSWORD=…
```

By default a build that cannot sign warns loudly and ships unsigned, so a lapsed
session still gives you something to test. Set `sign.required: true` for release
runs, where shipping unsigned is worse than not shipping.

**The uninstaller is not signed.** NSIS writes it during installation, on the
user's machine, so at build time there is no file to sign. A signed one needs a
two-pass build; the installer is what SmartScreen judges on download.

## The uninstaller

By default the uninstaller gets the same treatment as the installer: your own
HTML, a confirmation screen, progress, and a completion screen. The bundled
template also asks an optional "why are you uninstalling?" question and records
the answer in the shared store before it is torn down.

It comes from `uninstall.html` in your UI directory, alongside `index.html`, so
both pages share one stylesheet. If that file is absent — say you pointed `ui`
at a directory with only an installer page — the uninstaller quietly falls back
to NSIS's own dialog rather than failing the build.

Set `uninstall: { ui: false }` to skip it entirely and leave nothing on disk.

## Calling your own NSIS code

Register a script function and the page can invoke it by name:

```nsis
; via nsis.include
Function InstallDriver
  ExecWait '"$INSTDIR\driver\setup.exe" /quiet'
FunctionEnd

Function RegisterExtras
  GetFunctionAddress $0 InstallDriver
  blinkkit::RegisterAbility /NOUNLOAD "installDriver" $0
FunctionEnd
```

```js
await nsis.call('installDriver');
```

The address stays in C++; the page only ever sees the name.

## Silent install

Standard NSIS switches, for deployment:

```
MyApp-Setup-1.2.3.exe /S
MyApp-Setup-1.2.3.exe /S /D=C:\Custom\Path
"C:\Program Files\My App\Uninstall.exe" /S
```

## Troubleshooting

Set `BLINKKIT_LOG` to a file path before running the installer. It records
plugin calls, ability failures, and anything the page logs to `console` —
including script errors, which are otherwise invisible because there is no
devtools window:

```
set BLINKKIT_LOG=%TEMP%\blink.log
MyApp-Setup-1.2.3.exe
```

Set `BLINKKIT_DEVTOOLS` to a miniblink front-end directory to open the
inspector.

## Packages

| Package | Purpose |
| --- | --- |
| `blink-installer` | CLI |
| `blink-installer-core` | Build pipeline and config schema |
| `blink-installer-ui` | The API your page imports |
| `blink-installer-maker` | Electron Forge maker |
| `blink-installer-runtime` | Prebuilt binaries (plugin, miniblink, NSIS) |

Binaries are shipped prebuilt rather than compiled on install: building them
needs MSVC and a 32-bit toolchain, which is an unreasonable thing to ask of
someone packaging an app and impossible on the Linux runners many people build
Windows installers from.

## Building from source

Only needed if you are changing the C++ side.

```
npm install
npm run native:configure   # cmake -S native -B native/build -A Win32
npm run native:build
npm run build
```

Requires Visual Studio with the C++ workload. The plugin **must** be x86: the
NSIS stub is 32-bit and the miniblink binary is 32-bit only.

## Notes on the design

- The installer is a 32-bit process. That is normal for installers and fine for
  installing 64-bit applications, but registry access goes through
  `KEY_WOW64_64KEY` so a per-app key lands where the 64-bit app will look for it.
- NSIS unloads a plugin DLL as soon as a call returns unless `/NOUNLOAD` is
  given. Every generated call carries it; without it the window, the ability
  registry and the shared config would be destroyed between statements.
- Generated `.nsi` files are UTF-8 **with BOM**. NSIS 3 otherwise reads them in
  the system ANSI code page and rejects the first non-ASCII byte, which any
  non-English product name will have.

## Licence

MIT. Bundles NSIS (zlib/libpng licence) and miniblink.
