<div align="center">

# blink-installer

### You spent six months on your app. The first thing anyone sees is a grey wizard from 1998.

**Build Windows installers whose interface is HTML, CSS and JavaScript.**

[![npm](https://img.shields.io/npm/v/blink-installer?color=%2312b866&label=npm)](https://www.npmjs.com/package/blink-installer)
[![license](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
![platform](https://img.shields.io/badge/Windows-7%20%E2%86%92%2011-0078d4)
![size](https://img.shields.io/badge/framework%20overhead-6%20MB-lightgrey)

[中文](README.md)

![A full-screen entrance animation converging into the installer card](docs/demo.gif)

</div>

---

## Sound familiar?

- Moving a button means working out pixel coordinates in `nsDialogs`, then
  recompiling to see whether you got it right
- An entrance animation is not something you can do badly — it is not something
  the NSIS interface model has at all
- Every Inno Setup installer on earth looks identical, including yours
- "Are you sure you want to quit?" arrives as a Windows 95 grey box
- Changing one line of copy means learning a DSL you will never use anywhere
  else
- Your frontend colleague offers to help and cannot: there is no CSS here

**And you already know how to build a web page.**

```html
<button class="primary" id="install">Install</button>
```

```js
import { installer, fs } from 'blink-installer-ui';

document.querySelector('#install').onclick = () => installer.begin();
installer.on('progress', ({ percent }) => bar.style.width = percent + '%');
```

That is the whole idea. Rounded corners, gradients, shadows, `transition`, a
full-screen animation, your own typeface — **written exactly the way you would
write them anywhere else**.

```
npm i -D blink-installer
npx blink-installer init
npx blink-installer build
```

---

## How it gets away with it

**NSIS** does the unglamorous work — compression, extraction, shortcuts,
uninstall registration — and **miniblink** renders the page: a Blink engine in
a single DLL, old enough to still run on Windows 7.

What makes the look possible is that the window is **transparent with
per-pixel alpha**. That is where the rounded corners, the drop shadow, and the
full-screen animation played straight onto the desktop come from — none of
which a stock Win32 installer can do.

And the animation and the installer are the **same window**. It shrinks from
full screen into the card rather than handing off to a second process, so
nothing jumps, flashes, or reloads.

---

## What it does

It does not package your application. It wraps a directory you have **already
packaged**:

```
packaged directory (electron-builder's win-unpacked, electron-packager's output,
                    or a plain folder of an exe and its DLLs)
        +  your HTML interface
        +  a config
        ─────────────────────────▶  MyApp-Setup-1.2.3.exe
```

## Quick start

```js
// blink-installer.config.mjs
import { defineConfig } from 'blink-installer-core';

export default defineConfig({
  appId: 'com.example.myapp',
  productName: 'My App',
  version: '1.2.3',
  publisher: 'Example Corp',

  source: 'dist/win-unpacked',   // already-packaged directory
  exe: 'myapp.exe',

  ui: './installer-ui',          // omit for the bundled template
  splash: { enabled: true },     // full-screen entrance animation

  install: {
    defaultDir: '$LOCALAPPDATA\\Programs\\My App',
    elevate: false,              // per-user, no UAC prompt
    shortcuts: { desktop: true, startMenu: true },
  },
});
```

`npx blink-installer init --eject-ui` copies the bundled template into your
project so you can edit it directly.

### Two build modes

Compressing the payload dominates build time, and while you are working on the
interface none of that work is for you. So use two commands:

```json
"installer:dev":     "blink-installer build --ui-only --compression zlib --out dist/preview.exe",
"installer:release": "blink-installer build --compression lzma"
```

`--ui-only` swaps your application for a placeholder. Everything else is real —
the generated script, the pages, the progress wiring, the uninstaller — so you
can run it and click through the whole flow. It also **isolates itself**:
its own directory, its own registry key, shortcuts off, so a preview can never
touch a real installation of your product.

Measured on a 1.1 GB Electron app:

| | Time | Size |
|---|---|---|
| `--ui-only --compression zlib` | 3 s | 13 MB |
| `--compression zlib` | 79 s | 496 MB |
| `--compression lzma` | 450 s | 401 MB |

Release with `lzma` — it is what electron-builder's own NSIS target uses, so
anything else is where a surprising size difference between the two comes from.

---

## Let an AI agent integrate it

Paste the block below into a coding agent (Claude Code, Cursor, Codex, …). It
reads your project and generates an animation, dialogs and an install flow that
fit it.

> The prompt carries every hard constraint of the rendering engine. Without
> them an agent writes an interface that looks right in a browser and comes out
> blank or collapsed in the installer.

````markdown
Integrate blink-installer into this project: a Windows installer whose
interface is written in HTML/CSS/JS.

## Step 1: understand this project first
Before writing anything, read the README, package.json, the main entry point
and any product copy. Work out what this product does, what it is good at, who
it is for, whether it has brand colours and an icon, and where the name and
version come from. The interface and the animation must grow out of those
**specifics** — do not reach for a generic template.

## Step 2: install and scaffold
npm i -D blink-installer
npx blink-installer init --eject-ui

## Step 3: write blink-installer.config.mjs
- Derive version and productName from package.json; never hardcode them
  (a hardcoded version always drifts)
- source points at the packaged directory (for electron-builder,
  dist/<out>/win-unpacked)
- exe is the main executable's filename inside that directory
- Installing under $LOCALAPPDATA means elevate: false — no UAC, a much better
  experience. Only $PROGRAMFILES needs true
- If this should replace an existing installer, match its artifact naming

## Step 4: design the interface (the real work)
Build three things under installer-ui/, all specific to this product:

1. **Entrance animation** (splash.js / splash.css)
   Full-screen, transparent, drawn over the desktop. Use imagery from the
   product itself, not generic particles. A collaboration tool might converge
   several nodes into a centre; a utility might assemble parts into a whole.
   Two to three seconds, then call win.setBounds to shrink into the card.

2. **Install flow** (index.html / main.js / style.css)
   A welcome screen that says what this product is and what it will do once
   installed; a directory picker with a free-space check; progress with the
   product's capabilities cycling during the copy; a completion screen with a
   sensible next action.

3. **Dialogs**
   Use ui.messageBox — it draws in the page. Theme it with the --bk-dialog-*
   custom properties. Quit confirmation, overwrite confirmation and the
   uninstall reason prompt all go through it.

## Hard constraints of the rendering engine (violate these and things fail silently)
The renderer is Blink from the Chromium 57-60 era:
- CSS that does NOT exist: inset (use top/right/bottom/left), flex gap (use
  margins), accent-color, aspect-ratio, :is()/:where(), position: sticky on a
  flex child
- JS syntax is lowered by Babel automatically, so write modern JavaScript —
  but Object.entries and String.padStart are missing at runtime and cannot be
  compiled away. Never use them
- **Characters above U+FFFF render as nothing.** Most pictorial emoji live
  there (🕑 U+1F551 and 💬 U+1F4AC are blank); stay inside the Basic
  Multilingual Plane (✋ U+270B, ✉ U+2709 are fine) or use inline SVG
- Presentation is capped at roughly 33 fps. Design large, slow, eased motion;
  avoid small fast translations, which judder
- Write all sizes in **logical pixels**. The native side scales the window and
  zooms the page by the system factor — do not compensate for DPI yourself
- window.innerWidth is 0 on a transparent window; call sys.screen() for
  screen dimensions

## Step 5: verify
Add two scripts to package.json:
  "installer:dev":     "blink-installer build --ui-only --compression zlib --out dist/preview.exe"
  "installer:release": "blink-installer build --compression lzma"
Run installer:dev (a few seconds), actually run the artifact, and click through
the entire flow before calling it done.
````

---

## Writing the UI

Your page is an ordinary web page talking to the installer through a typed API:

```js
import { installer, config, fs, proc, win, ui } from 'blink-installer-ui';

const dir = await fs.pickDirectory({ title: 'Choose a folder' });
config.set('installDir', dir);

installer.on('progress', ({ percent }) => bar.style.width = percent + '%');
installer.on('log', ({ message }) => detail.textContent = message);

document.querySelector('#install').onclick = () => installer.begin();
```

### Dialogs

`ui.messageBox` draws in the page rather than through Win32, so a confirmation
looks like the rest of your installer:

```js
const answer = await ui.messageBox({
  title: 'Quit setup?',
  message: 'Installation is in progress. Leaving now leaves it incomplete.',
  buttons: 'yesNo',
  icon: 'warning',
});
if (answer === 'yes') win.close(true);
```

Three levels of control: the defaults just work (button labels follow the system
language) → override `--bk-dialog-surface`, `--bk-dialog-accent` and friends to
match your palette → restyle the `.bk-dialog*` classes, or take over entirely
with `ui.setDialogRenderer(fn)`.

`ui.messageBoxNative` still reaches the Win32 box, for failures early enough
that the page cannot be trusted to draw.

### Display scaling

Sizes in your config and CSS are **logical pixels**. The installer reads the
system scaling once at startup, sizes the window accordingly and zooms the page
to match, so `width: 880` and `font-size: 14px` look the same on a 150% laptop
as on a 100% desktop. No media queries, and no compensating by hand.

`sys.screen()` reports logical pixels and `win.setBounds` / `win.resize` take
them, so arithmetic mixing a screen rectangle with a CSS size stays consistent.
`sys.screen().scale` exposes the factor if you genuinely need device pixels.

### Entrance animation

`splash: { enabled: true }` opens the window covering the work area with
per-pixel alpha and plays an animation before the installer appears. Anything
the animation does not draw is transparent and the desktop shows through; when
it finishes, the same window shrinks to the installer card.

**Frame rate.** miniblink presents at a hard **~33 fps** while
`requestAnimationFrame` fires at ~59 Hz, so about half the frames a page
computes are never shown. It is not tunable: identical across five miniblink
releases from 2017 to 2021, at every window size, with or without per-pixel
alpha, and regardless of the `drawMinInterval` debug config.
`native/test/FINDINGS.md` has the measurements.

Design for it. Large, slow, eased movement with trails and glows reads fine at
33 fps; small fast translations judder. Driving the loop faster only burns CPU.

### The engine is old — this matters

miniblink reports `Chrome/60` but its JavaScript parser is older. Measured
directly, it has **no** destructuring, default parameters, object spread,
`async`/`await`, class fields, `?.`, `??`, `**`, or optional catch binding. It
*does* have classes, generators, arrow functions, `let`, template literals,
`Promise`, `Map`/`Set` and `for...of`.

You do not have to care: the build runs esbuild and then Babel, lowering all of
it, so you write normal modern JavaScript. Two gaps are libraries rather than
syntax and cannot be compiled away — **avoid `Object.entries` and
`String.padStart`**.

CSS is not transpiled, so avoid `position: sticky` on flex children and check
anything newer than about 2017 before relying on it.

Content has one trap of its own: **characters above U+FFFF draw as nothing**.
Most pictorial emoji live up there, so `🕑` (U+1F551) and `💬` (U+1F4AC) come
out blank while `✋` (U+270B) and `✉` (U+2709) are fine. Keep character icons
inside the Basic Multilingual Plane, or use an inline SVG.

---

## Configuration

| Field | Meaning |
| --- | --- |
| `appId` | Reverse-DNS id, used for the uninstall registry key |
| `productName` / `version` / `publisher` | Shown in the UI and in file properties |
| `source` | The packaged directory to install |
| `exe` | Main executable, relative to `source` |
| `output` | Installer path (default `dist/<product>-Setup-<version>.exe`) |
| `ui` | Directory containing `index.html`; omit for the bundled template |
| `icon` | `.ico` for setup.exe and Programs and Features |
| `window` | `width`, `height`, `transparent` |
| `splash` | `enabled`, `timeoutMs` (backstop if the animation hangs) |
| `install.defaultDir` | May use NSIS variables such as `$PROGRAMFILES` |
| `install.elevate` | `true` requires admin, installs machine-wide (HKLM); `false` is per-user (HKCU) |
| `install.shortcuts` | `desktop`, `startMenu`, `startMenuFolder` |
| `install.uninstallEntry` | Generate an uninstaller and register it |
| `install.legacyFolderPicker` | Force the XP-era folder tree box; see below |
| `uninstall.ui` | Give the uninstaller the same HTML interface (default `true`) |
| `compression` | `lzma` (smallest), `bzip2`, `zlib` (fastest build) |
| `sign` | Authenticode signing; omit to ship unsigned |
| `nsis.include` | Path to an `.nsh` injected into the generated script |

### On `install.legacyFolderPicker`

Choosing a directory uses the explorer dialog (`IFileOpenDialog`) by default:
address bar, favourites, and somewhere to paste a path. Turning this on
substitutes the 400-pixel-wide tree box from the XP era
(`SHBrowseForFolder`).

**It is an escape hatch, not a style choice.** The modern dialog is hosted by
the shell, which means it is exposed to whatever shell extensions are loaded
into the process on a given machine. Where one of those makes it hang or refuse
to open, the old tree box still works, because it asks far less of the shell.
Turn it on against a reproduction, not a preference.

A page can also override a single call without changing the build:

```js
await fs.pickDirectory({ title: 'Choose a folder', legacy: true });
```

Both paths cancel the same way: **declining ends the request** and does not
open a second dialog.

## Signing

An unsigned installer gets a SmartScreen warning on every download until enough
people click through it.

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

With nothing yet, drive signtool directly. From the certificate store by
thumbprint, the only option for a cloud/HSM certificate:

```js
sign: { thumbprint: 'C3C1…91ED', timestamp: 'http://time.certum.pl' }
```

or from a file, with the password in the environment rather than the config:

```js
sign: { certificateFile: './cert.pfx' }   // BLINK_SIGN_PASSWORD=…
```

By default a build that cannot sign warns loudly and ships unsigned, so a lapsed
session still gives you something to test. Set `sign.required: true` for release
runs.

**The uninstaller is not signed.** NSIS writes it during installation, on the
user's machine, so at build time there is no file to sign. A signed one needs a
two-pass build; the installer is what SmartScreen judges on download.

## The uninstaller

By default it gets the same treatment as the installer: your HTML, a
confirmation screen, progress, a completion screen. That costs a renderer
(~17 MB) parked in the install directory. `uninstall: { ui: false }` falls back
to the stock NSIS dialog with nothing extra on disk.

## Silent install

`/S` skips the interface; `/D=path` sets the directory (must come last, and
unquoted):

```
MyApp-Setup.exe /S /D=C:\Tools\MyApp
```

## Integrations

**Electron Forge**:

```js
makers: [{ name: 'blink-installer-maker', config: { /* as above */ } }]
```

**electron-builder**: run `electron-builder --dir` for `win-unpacked`, then let
`blink-installer build` take over.

**Plain Win32**: point `source` at the folder holding your exe and its DLLs.

## Building from source

```
npm install
npm run build              # the TypeScript packages
npm run native:configure   # CMake, x86
npm run native:build       # produces blinkkit.dll
npm test
```

Needs Visual Studio with the C++ desktop workload, and CMake. The runtime
binaries are committed, so a fresh clone builds offline.

## Licence

MIT. Redistributed third-party binaries keep their own licences: miniblink is
Apache-2.0, NSIS is zlib/libpng with its LZMA module under CPL-1.0. See
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
