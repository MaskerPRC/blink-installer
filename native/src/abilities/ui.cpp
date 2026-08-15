// Native dialog abilities.
//
// The page can draw its own dialogs in HTML and usually should. This exists for
// the cases where it cannot: errors raised before the page has loaded, and
// prompts driven from an NSIS Section while the page is mid-transition.
#include <windows.h>

#include "ability.h"
#include "strings.h"
#include "taskbar.h"

namespace bk {
namespace {

UINT ButtonsFromString(const std::string& buttons) {
  if (buttons == "okCancel") return MB_OKCANCEL;
  if (buttons == "yesNo") return MB_YESNO;
  if (buttons == "yesNoCancel") return MB_YESNOCANCEL;
  if (buttons == "retryCancel") return MB_RETRYCANCEL;
  return MB_OK;
}

UINT IconFromString(const std::string& icon) {
  if (icon == "warning") return MB_ICONWARNING;
  if (icon == "error") return MB_ICONERROR;
  if (icon == "question") return MB_ICONQUESTION;
  if (icon == "none") return 0;
  return MB_ICONINFORMATION;
}

const char* ResultName(int result) {
  switch (result) {
    case IDOK: return "ok";
    case IDCANCEL: return "cancel";
    case IDYES: return "yes";
    case IDNO: return "no";
    case IDRETRY: return "retry";
    default: return "cancel";
  }
}

}  // namespace

// Blocks the UI thread, so it is flagged slow: callers on the page should reach
// it through invokeAsync and keep animating.
BK_ABILITY("ui.messageBox", kAbilityUiThread | kAbilitySlow) {
  const std::wstring text = Utf8ToWide(args["message"].as_string());
  const std::wstring title = Utf8ToWide(args["title"].as_string("Setup"));
  const UINT flags = ButtonsFromString(args["buttons"].as_string("ok")) |
                     IconFromString(args["icon"].as_string("info")) |
                     MB_SETFOREGROUND;

  const int result = ::MessageBoxW(ctx.hwnd, text.c_str(), title.c_str(), flags);
  if (result == 0) {
    ctx.Fail("MessageBox failed: " + FormatWin32Error(::GetLastError()));
    return Json();
  }
  return Json(std::string(ResultName(result)));
}

// Sets the taskbar button's progress state, so the installer shows progress
// even when minimized.
BK_ABILITY("ui.taskbarProgress", kAbilityUiThread) {
  SetTaskbarProgress(ctx.hwnd, args["percent"].as_int(-1),
                     args["indeterminate"].as_bool());
  return Json(true);
}

}  // namespace bk
