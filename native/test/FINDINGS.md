# miniblink rendering: what works, what does not

Measured on the bundled renderer (`node.dll`, miniblink 2021.11.8, user agent
`Chrome/60.0.3729.169`) with the probes in this directory. Re-run them against
a different miniblink build before trusting any of it.

    packages/runtime/nsis/Bin/makensis.exe anim-probe.nsi
    packages/runtime/nsis/Bin/makensis.exe /DFULLSCREEN anim-probe.nsi
    powershell -File anim-probe.ps1   -Mode win     # which techniques animate
    powershell -File anim-probe.ps1   -Mode full
    powershell -File measure-frames.ps1 -Mode win   # presented frame rate
    powershell -File capture-splash.ps1 -Exe <setup.exe>

## Animation techniques

All eight work, in both an ordinary transparent window and a full-screen one.
There is no need to avoid CSS animation on this engine.

| Technique | 1100x620 transparent | 2560x1440 transparent |
| --- | --- | --- |
| CSS `@keyframes` on `transform` | animates | animates |
| CSS `@keyframes` on `left` | animates | animates |
| CSS `transition` on `transform` | animates | animates |
| CSS `transition` on `left` | animates | animates |
| JS rAF writing `transform` | animates | animates |
| JS rAF writing `left` | animates | animates |
| Canvas 2D | animates | animates |
| SVG SMIL | animates | animates |

## Frame rate

**The renderer presents at ~33 fps and that is a hard ceiling.**
`requestAnimationFrame` fires at ~59 Hz, so roughly every second frame is
computed and then never reaches the screen.

Everything below was measured by encoding the rAF frame counter into a swatch
colour and counting distinct colours in rapid `PrintWindow` captures — position
based measurement under-reports, because two consecutive frames of slow motion
can round to the same pixel.

| Variable | Presented |
| --- | --- |
| Busy page (8 lanes + canvas + SVG), 1100x620 | 33 /s |
| Minimal page (one colour swatch), 1100x620 | 33 /s |
| Minimal page, 2560x1440 | 32 /s |
| `drawMinInterval` = 1, 3, 8, 16 | 33 /s in every case |
| Opaque window instead of layered | 32 /s |

Neither content cost, window size, the `drawMinInterval` debug config, nor
per-pixel alpha moves the number. It is the engine's present loop.

Nor does the miniblink version. Five builds spanning four years, swapped in by
`bench-engines.ps1` and measured identically:

| Build | node.dll | Presented |
| --- | --- | --- |
| 2017-05-31 (earliest release) | 17.0 MB | 33 /s |
| 2019-09-23 | 22.4 MB | 33 /s |
| 2020-06-14 | 27.7 MB | 33 /s |
| 2021-05-27 | 33.1 MB | 33 /s |
| 2021-11-08 (bundled) | 17.0 MB | 33 /s |

Getting above 33 fps means changing renderer, not changing miniblink build or
tuning it.

**Consequences for design.** Build motion that reads well at 30 fps: large,
slow, eased movement, fades and scales. Small fast translations judder. Trails
and glows hide the gaps. Driving the animation loop faster than ~33 Hz only
burns CPU on frames nobody sees.

## Capturing for verification

`BitBlt` screen grabs of a layered window are timing- and z-order-sensitive and
will happily return the desktop while the window is animating perfectly well —
which produced two wrong conclusions during development. Use `PrintWindow` with
`PW_RENDERFULLCONTENT` (0x2), and start the clock only after the window handle
exists, not after `Start-Process` returns.

## JavaScript language support

Measured by `eval`-ing each construct and catching `SyntaxError`; see the probe
page in the git history.

Supported: `let`/`const`, arrow functions, template literals, classes, `class
extends`, rest/spread in calls, `for...of`, `Promise`, generators,
`Object.assign`, `Array.from`, `Array.includes`, `Map`, `Set`, `Symbol`.

**Not supported:** destructuring, default parameters, object spread,
`async`/`await`, class fields, optional catch binding, `??`, `?.`, `**`,
`Object.entries`, `String.padStart`.

The build pipeline lowers all the syntax gaps through Babel, so page authors can
write modern JavaScript. `Object.entries` and `String.padStart` are library
gaps and cannot be compiled away — avoid them.

## DOM and CSS quirks

- `window.innerWidth` / `innerHeight` report **0** on a transparent window when
  it covers the work area. Use `document.documentElement.clientWidth`.
- `NodeList` has no `[Symbol.iterator]`, so `for (const el of
  document.querySelectorAll(...))` throws. Use `Array.from(...)`.
- `var()` works in ordinary declarations but is unreliable inside `margin`
  shorthands here; set computed offsets as inline styles instead.
- `devicePixelRatio` is 1 and CSS pixels map 1:1 to physical pixels, so window
  bounds and page coordinates agree.

## Window behaviour

- Resizing a miniblink window with `SetWindowPos` after creation leaves its
  layered surface at the old size and the window then presents nothing at all —
  a fully transparent rectangle with no error anywhere. Pass the geometry to
  `wkeCreateWebWindow` and afterwards change only z-order
  (`SWP_NOMOVE | SWP_NOSIZE`).
- A full-screen splash must be topmost or it opens behind whatever the user had
  in front.
