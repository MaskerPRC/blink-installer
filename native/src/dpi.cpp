#include "dpi.h"

#include <windows.h>

#include "log.h"

namespace bk {
namespace {

float QueryScale() {
  HDC screen = ::GetDC(nullptr);
  if (!screen) return 1.0f;
  const int dpi = ::GetDeviceCaps(screen, LOGPIXELSX);
  ::ReleaseDC(nullptr, screen);

  // A zero or absurd reading means the call did not do what we think it does;
  // 1.0 leaves everything exactly as it was rather than scaling by garbage.
  if (dpi < 48 || dpi > 960) {
    BK_LOGF(Warn, "implausible system DPI %d, not scaling", dpi);
    return 1.0f;
  }
  return static_cast<float>(dpi) / 96.0f;
}

// Round rather than truncate: at 150% a 620px height truncates to 929 instead
// of 930, and a one-pixel deficit on a window that is supposed to sit flush
// against a computed position is visible as a seam.
int Scaled(int value, float factor) {
  const float scaled = static_cast<float>(value) * factor;
  return static_cast<int>(scaled + (scaled < 0.0f ? -0.5f : 0.5f));
}

}  // namespace

float DpiScale() {
  static const float scale = QueryScale();
  return scale;
}

int ToPhysical(int logical) { return Scaled(logical, DpiScale()); }

int ToLogical(int physical) { return Scaled(physical, 1.0f / DpiScale()); }

}  // namespace bk
