# Third-party notices

`blink-installer` itself is MIT (see [LICENSE](LICENSE)). It redistributes the
following third-party software, each under its own license. Nothing below is
covered by the MIT grant.

Everything redistributed lives in the `blink-installer-runtime` package, which
carries its own copy of this file.

---

## miniblink49

- **Files:** `node_modules/blink-installer-runtime/bin/node.dll`, the `wke.h` header used to build `blinkkit.dll`
- **Upstream:** https://github.com/weolar/miniblink49
- **License:** Apache License 2.0
- **Version:** 2021-05-27 release, x86
- **SHA-256 (`node.dll`):**
  `7013fc901e1dd80acbcc0cbf389b033c356730e1fdddd18f0bc1f8d2bdf498d5`
  (also recorded in `node_modules/blink-installer-runtime/bin/node.dll.sha256`)

The DLL is redistributed byte-for-byte as published by the upstream project.
It is not modified, patched, or recompiled. The name `node.dll` is upstream's
own; despite it, this is a Blink-derived web renderer and not Node.js.

The full Apache-2.0 text is available at
https://www.apache.org/licenses/LICENSE-2.0.

Apache-2.0 §4 requires that redistributions carry the license, state any
changes, and retain attribution notices. There are no changes to state; the
attribution banner in `wke.h` is retained verbatim.

---

## NSIS (Nullsoft Scriptable Install System)

- **Files:** `node_modules/blink-installer-runtime/nsis/**` — `makensis.exe`, `Stubs/`, `Include/`,
  `Plugins/x86-unicode/`, `Bin/`, `Contrib/Graphics/`
- **Upstream:** https://nsis.sourceforge.io/
- **Version:** 3.10, Unicode
- **License:** see `node_modules/blink-installer-runtime/nsis/COPYING`, redistributed verbatim

NSIS is a mix of licenses, all of which permit redistribution in a commercial
product:

| Component | License |
|---|---|
| NSIS source, plug-ins, headers, graphics | zlib/libpng |
| zlib compression module | zlib/libpng |
| bzip2 compression module | bzip2 |
| LZMA compression module | Common Public License 1.0 |

All four compression stubs ship, because `compression` is a config option; the
CPL-1.0 LZMA module is therefore redistributed even when a given build selects
`zlib`.

The distribution is subset to what compiling actually needs. Documentation
(`.chm`), `Examples/`, and unused plug-ins are omitted. `COPYING` is kept.

---

## Build-time dependencies

esbuild (MIT), Babel (MIT), and zod (MIT) are ordinary npm dependencies. They
run only at build time and are not redistributed inside a produced installer —
npm resolves them normally and their own license files travel with them.
