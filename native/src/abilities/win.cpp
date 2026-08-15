// Window control abilities.
//
// The page owns its own chrome — a transparent window has no title bar — so it
// has to drive close, minimize and drag itself.
//
// One ability each, rather than a single window-control ability taking a
// command argument: separate names are individually typeable in the page SDK,
// and a typo becomes an unknown-ability error instead of a silently ignored
// command.
#include <windows.h>

#include "ability.h"
#include "blink_window.h"
#include "dpi.h"

namespace bk {

BK_ABILITY("win.close", kAbilityUiThread) {
  // force skips the close guard, which is how a page confirms its own
  // "really quit?" dialog and then proceeds.
  BlinkWindow::Get()->Close(args["force"].as_bool());
  return Json(true);
}

BK_ABILITY("win.minimize", kAbilityUiThread) {
  BlinkWindow::Get()->Minimize();
  return Json(true);
}

BK_ABILITY("win.resize", kAbilityUiThread) {
  const int width = args["width"].as_int();
  const int height = args["height"].as_int();
  if (width <= 0 || height <= 0) {
    ctx.Fail("win.resize requires positive 'width' and 'height'");
    return Json();
  }
  // Logical in, physical out — see dpi.h.
  BlinkWindow::Get()->Resize(ToPhysical(width), ToPhysical(height));
  return Json(true);
}

// Moves and resizes in one step.
//
// Separate move and resize calls would show the window at an intermediate
// rectangle for a frame, which is exactly the flicker a splash-to-installer
// transition has to avoid.
BK_ABILITY("win.setBounds", kAbilityUiThread) {
  if (!ctx.hwnd) {
    ctx.Fail("no installer window");
    return Json();
  }
  const int width = args["width"].as_int();
  const int height = args["height"].as_int();
  if (width <= 0 || height <= 0) {
    ctx.Fail("win.setBounds requires positive 'width' and 'height'");
    return Json();
  }
  // Also drops out of the topmost band the splash put it in. Pass
  // topmost:true to keep it there.
  HWND after = args["topmost"].as_bool() ? HWND_TOPMOST : HWND_NOTOPMOST;
  // All four are logical pixels, including the origin: the page got x/y from
  // sys.screen, which reports logical. Scaling the size but not the position
  // would centre the card against the wrong rectangle.
  ::SetWindowPos(ctx.hwnd, after, ToPhysical(args["x"].as_int()),
                 ToPhysical(args["y"].as_int()), ToPhysical(width),
                 ToPhysical(height), SWP_NOACTIVATE);
  return Json(true);
}

// Starts a drag of the frameless window. The page calls this from mousedown on
// its title area; Windows then runs its own modal drag loop.
BK_ABILITY("win.startDrag", kAbilityUiThread) {
  if (!ctx.hwnd) {
    ctx.Fail("no installer window");
    return Json();
  }
  ::ReleaseCapture();
  ::PostMessageW(ctx.hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
  return Json(true);
}

// When enabled, the title-bar X and Alt+F4 raise a "window-closing" event
// instead of destroying the window, letting the page confirm first.
BK_ABILITY("win.setCloseGuard", kAbilityUiThread) {
  BlinkWindow::Get()->set_close_guard(args["enabled"].as_bool(true));
  return Json(true);
}

// Hands control back to NSIS so it can advance to the next page.
//
// This is the hinge of the whole flow and it is subtle. ShowPage runs our
// message loop; quitting that loop lets the NSIS page function return, and NSIS
// moves on to `Page instfiles` and starts the Section. The window is *not*
// destroyed — it stays on screen and keeps repainting, because from that point
// NSIS's own dialog loop is pumping messages for the same thread. That is how
// the page can show install progress it is no longer pumping for itself.
BK_ABILITY("installer.next", kAbilityUiThread) {
  ::PostQuitMessage(0);
  return Json(true);
}

BK_ABILITY("win.center", kAbilityUiThread) {
  if (!ctx.hwnd) {
    ctx.Fail("no installer window");
    return Json();
  }
  RECT rect = {};
  ::GetWindowRect(ctx.hwnd, &rect);
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;

  // Center on the monitor the window is currently on, not the primary one —
  // multi-monitor setups otherwise fling the installer to the wrong screen.
  HMONITOR monitor = ::MonitorFromWindow(ctx.hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info = {};
  info.cbSize = sizeof(info);
  if (!::GetMonitorInfoW(monitor, &info)) {
    ctx.Fail("GetMonitorInfo failed");
    return Json();
  }

  const int x = info.rcWork.left +
                ((info.rcWork.right - info.rcWork.left) - width) / 2;
  const int y = info.rcWork.top +
                ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
  ::SetWindowPos(ctx.hwnd, nullptr, x, y, 0, 0,
                 SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
  return Json(true);
}

}  // namespace bk
