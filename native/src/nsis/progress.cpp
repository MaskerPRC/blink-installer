#include "nsis/progress.h"

#include <commctrl.h>

#include <atomic>
#include <map>
#include <string>
#include <thread>

#include "blink_window.h"
#include "json.h"
#include "log.h"
#include "strings.h"
#include "taskbar.h"
#include "task_queue.h"

namespace bk {
namespace nsis {
namespace {

std::map<HWND, WNDPROC> g_original_procs;
HWND g_parent_window = nullptr;
HWND g_progress_window = nullptr;
int g_range_min = 0;
int g_range_max = 30000;  // NSIS's default; corrected on bind and on SETRANGE
int g_last_percent = -1;
bool g_finished = false;
std::atomic<int> g_page_epoch{0};
std::atomic<bool> g_advancing{false};

// Moves NSIS off the instfiles page once the install has finished.
//
// NSIS expects the user to click Next there. Our whole point is that its window
// is never seen, so nobody can — and without this the installer sits on a
// completed progress bar forever and the page after it never runs.
//
// The click has to be retried: NSIS only enables that button after the entire
// section returns, which is later than the progress bar reaching 100%, and
// there is no notification for it. Posting to a disabled button is harmless, so
// we keep offering until the page actually changes.
void StartAutoAdvance(HWND parent) {
  bool expected = false;
  if (!g_advancing.compare_exchange_strong(expected, true)) return;

  const int epoch = g_page_epoch.load();
  std::thread([parent, epoch]() {
    for (int attempt = 0; attempt < 150; ++attempt) {
      if (g_page_epoch.load() != epoch) break;  // NSIS moved on
      if (!::IsWindow(parent)) break;
      // Control 1 is the Next/Close button on the NSIS shell dialog.
      ::PostMessageW(parent, WM_COMMAND, MAKEWPARAM(1, BN_CLICKED), 0);
      ::Sleep(200);
    }
    g_advancing.store(false);
  }).detach();
}

void Emit(Json event) {
  // Posted rather than dispatched inline: these arrive from inside a
  // cross-thread SendMessage, and running page JS reentrantly there would let
  // the page call back into a window procedure that is still on the stack.
  TaskQueue::Get().Post(
      [event]() { BlinkWindow::Get()->DispatchEvent(event); });
}

void EmitProgress(int position) {
  const int span = g_range_max - g_range_min;
  if (span <= 0) return;

  int percent = static_cast<int>((static_cast<long long>(position - g_range_min) * 100) / span);
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  // NSIS ticks the bar far more often than the page needs to repaint; only
  // speak up when the whole-number percentage actually moves.
  if (percent == g_last_percent) return;
  g_last_percent = percent;

  Json event = Json::object();
  event["type"] = Json("progress");
  event["percent"] = Json(percent);
  Emit(event);

  SetTaskbarProgress(BlinkWindow::Get()->hwnd(), percent, false);

  if (percent >= 100 && !g_finished) {
    g_finished = true;
    Json finish = Json::object();
    finish["type"] = Json("finish");
    Emit(finish);
    StartAutoAdvance(g_parent_window);
  }
}

void EmitLogLine(const std::wstring& text) {
  if (text.empty()) return;
  Json event = Json::object();
  event["type"] = Json("log");
  event["message"] = Json(WideToUtf8(text));
  Emit(event);
}

void RefreshRange() {
  if (!g_progress_window) return;
  PBRANGE range = {};
  ::SendMessageW(g_progress_window, PBM_GETRANGE, TRUE,
                 reinterpret_cast<LPARAM>(&range));
  if (range.iHigh > range.iLow) {
    g_range_min = range.iLow;
    g_range_max = range.iHigh;
  }
}

// Parks the NSIS shell window: hidden, zero-sized, and off-screen.
//
// Any one of those alone is defeatable — NSIS calls ShowWindow again on page
// transitions — so all three are applied, and reapplied whenever it tries to
// come back.
void ParkWindow(HWND hwnd) {
  ::ShowWindow(hwnd, SW_HIDE);
  ::SetWindowPos(hwnd, nullptr, -32000, -32000, 0, 0,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
}

LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
                              LPARAM lparam) {
  auto it = g_original_procs.find(hwnd);
  if (it == g_original_procs.end()) {
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  const WNDPROC original = it->second;

  if (hwnd == g_parent_window) {
    // Intercept the request to become visible *before* it takes effect.
    // Reacting to WM_SHOWWINDOW is too late: by then the window has already
    // been painted on screen, which is exactly the flash of stock NSIS UI we
    // are trying to prevent.
    if (msg == WM_WINDOWPOSCHANGING) {
      auto* pos = reinterpret_cast<WINDOWPOS*>(lparam);
      if (pos) {
        pos->flags &= ~SWP_SHOWWINDOW;
        pos->flags |= SWP_HIDEWINDOW | SWP_NOACTIVATE;
        pos->cx = 0;
        pos->cy = 0;
        pos->x = -32000;
        pos->y = -32000;
      }
    } else if (msg == WM_SHOWWINDOW && wparam == TRUE) {
      ParkWindow(hwnd);
      return 0;
    }
  } else if (hwnd == g_progress_window) {
    switch (msg) {
      case PBM_SETRANGE:
        g_range_min = LOWORD(lparam);
        g_range_max = HIWORD(lparam);
        break;
      case PBM_SETRANGE32:
        g_range_min = static_cast<int>(wparam);
        g_range_max = static_cast<int>(lparam);
        break;
      case PBM_SETPOS:
        // Derived from the range the control actually reports. Hardcoding
        // the common 0..30000 range as a divide-by-300 produces silent
        // nonsense the moment a script sets a different one.
        EmitProgress(static_cast<int>(wparam));
        break;
      case PBM_DELTAPOS:
        EmitProgress(static_cast<int>(::SendMessageW(hwnd, PBM_GETPOS, 0, 0)) +
                     static_cast<int>(wparam));
        break;
      default:
        break;
    }
  } else {
    // The detail list: each inserted row is one "Extracting: ..." line. The
    // original intercepted this message and then did nothing with it.
    if (msg == LVM_INSERTITEMW || msg == LVM_INSERTITEMA) {
      const auto* item = reinterpret_cast<const LVITEMW*>(lparam);
      if (item && (item->mask & LVIF_TEXT) && item->pszText) {
        if (msg == LVM_INSERTITEMW) {
          EmitLogLine(item->pszText);
        } else {
          const auto* narrow = reinterpret_cast<const char*>(item->pszText);
          EmitLogLine(Utf8ToWide(narrow));
        }
      }
    }
  }

  return ::CallWindowProcW(original, hwnd, msg, wparam, lparam);
}

void Subclass(HWND hwnd) {
  if (!hwnd || g_original_procs.count(hwnd)) return;
  auto* original = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(
      hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&SubclassProc)));
  if (original) g_original_procs[hwnd] = original;
}

}  // namespace

void HideHostWindow(HWND parent) {
  if (!parent) {
    BK_LOG(Warn, "HideHostWindow called with no parent window");
    return;
  }
  g_parent_window = parent;

  // Drop it out of the taskbar too, so the installer shows one button rather
  // than two — one of which opens a window the user can never see.
  const LONG_PTR ex = ::GetWindowLongPtrW(parent, GWL_EXSTYLE);
  ::SetWindowLongPtrW(parent, GWL_EXSTYLE,
                      (ex & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW);

  Subclass(parent);
  ParkWindow(parent);
  BK_LOG(Info, "NSIS host window parked");
}

void BindProgress(HWND parent) {
  if (!parent) {
    BK_LOG(Warn, "BindProgress called with no parent window");
    return;
  }

  // Idempotent: HideHostWindow normally ran at InitWindow time.
  HideHostWindow(parent);
  g_last_percent = -1;
  g_finished = false;

  // NSIS's layout: the outer window hosts an inner #32770 dialog which owns
  // the progress bar and the detail list view.
  HWND inner = ::FindWindowExW(parent, nullptr, L"#32770", nullptr);
  if (!inner) {
    BK_LOG(Warn, "BindProgress: NSIS inner dialog not found; "
                 "progress will not be reported");
    return;
  }

  g_progress_window =
      ::FindWindowExW(inner, nullptr, L"msctls_progress32", nullptr);
  HWND list = ::FindWindowExW(inner, nullptr, L"SysListView32", nullptr);

  if (g_progress_window) {
    Subclass(g_progress_window);
    RefreshRange();
  } else {
    BK_LOG(Warn, "BindProgress: progress bar not found");
  }
  if (list) Subclass(list);

  BK_LOGF(Info, "progress bound (range %d..%d)", g_range_min, g_range_max);
}

void NotePageShown() { g_page_epoch.fetch_add(1); }

void UnbindProgress() {
  for (auto& [hwnd, original] : g_original_procs) {
    if (::IsWindow(hwnd)) {
      ::SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(original));
    }
  }
  g_original_procs.clear();
  g_parent_window = nullptr;
  g_progress_window = nullptr;
}

}  // namespace nsis
}  // namespace bk
