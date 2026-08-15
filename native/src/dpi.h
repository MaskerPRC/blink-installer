// The one place that knows about display scaling.
//
// The installer manifest declares DPI awareness, which means Windows hands us
// physical pixels and does no scaling of its own. Left alone, a 880x620 window
// on a 150% display is 880x620 *physical* pixels — visually the size of a
// 587x413 window — and 14px text renders at what looks like 9px. That is the
// whole bug this file exists to prevent.
//
// The rule everywhere above this line: **pages and configs speak logical
// pixels; only C++ converts.** A page author writes `width: 880` and gets a
// window that looks the same on every machine. Converting anywhere else means
// every new ability that touches a coordinate has to remember, and one that
// forgets is a bug nobody sees until a HiDPI laptop shows up.
//
// Awareness is declared as system-DPI (NSIS `ManifestDPIAware true`), not
// per-monitor, so a single process-wide factor is the correct model and
// GetDeviceCaps is the right query — it works back to XP, unlike
// GetDpiForMonitor which needs 8.1 and an awareness level we do not declare.
#pragma once

namespace bk {

// Physical pixels per logical pixel: 1.0 at 100%, 1.5 at 150%. Queried once
// on first use, because the manifest fixes it for the life of the process.
float DpiScale();

// Logical -> physical. Use when handing a size or position to Win32.
int ToPhysical(int logical);

// Physical -> logical. Use when reporting a Win32 rectangle to a page.
int ToLogical(int physical);

}  // namespace bk
