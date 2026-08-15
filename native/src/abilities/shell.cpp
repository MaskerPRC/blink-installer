// Shell abilities: launching things.
#include <windows.h>
#include <shellapi.h>

#include "ability.h"
#include "strings.h"

namespace bk {
namespace {

// ShellExecuteW returns a value <= 32 on failure; the codes overlap with
// Win32 error codes, which is what FormatWin32Error expects.
bool RunShellExecute(HWND owner, const std::wstring& verb,
                     const std::wstring& target, const std::wstring& params,
                     const std::wstring& directory, int show,
                     std::string* error) {
  const HINSTANCE result = ::ShellExecuteW(
      owner, verb.empty() ? nullptr : verb.c_str(), target.c_str(),
      params.empty() ? nullptr : params.c_str(),
      directory.empty() ? nullptr : directory.c_str(), show);

  const auto code = reinterpret_cast<INT_PTR>(result);
  if (code > 32) return true;
  *error = "ShellExecute failed: " +
           FormatWin32Error(static_cast<unsigned long>(code));
  return false;
}

}  // namespace

// Opens a URL in the user's default browser.
BK_ABILITY("shell.openUrl", kAbilityDefault) {
  const std::string url = args["url"].as_string();
  if (url.empty()) {
    ctx.Fail("shell.openUrl requires a 'url'");
    return Json();
  }
  // Only hand the shell schemes a browser should handle. Without this an
  // installer page could be talked into launching an arbitrary local
  // executable through the same entry point.
  if (!StartsWith(url, "http://") && !StartsWith(url, "https://") &&
      !StartsWith(url, "mailto:")) {
    ctx.Fail("shell.openUrl only accepts http, https and mailto URLs");
    return Json();
  }

  std::string error;
  if (!RunShellExecute(ctx.hwnd, L"open", Utf8ToWide(url), L"", L"",
                       SW_SHOWNORMAL, &error)) {
    ctx.Fail(error);
    return Json();
  }
  return Json(true);
}

// Launches a program. Used at the end of an install to start the app.
BK_ABILITY("shell.exec", kAbilityDefault) {
  const std::wstring path = Utf8ToWide(args["path"].as_string());
  if (path.empty()) {
    ctx.Fail("shell.exec requires a 'path'");
    return Json();
  }
  const std::wstring params = Utf8ToWide(args["args"].as_string());
  const std::wstring cwd = Utf8ToWide(args["cwd"].as_string());
  // "runas" triggers the UAC prompt; useful when the installer itself is not
  // elevated but the launched step needs to be.
  const std::wstring verb = args["elevated"].as_bool() ? L"runas" : L"open";
  const int show = args["hidden"].as_bool() ? SW_HIDE : SW_SHOWNORMAL;

  std::string error;
  if (!RunShellExecute(ctx.hwnd, verb, path, params, cwd, show, &error)) {
    ctx.Fail(error);
    return Json();
  }
  return Json(true);
}

// Opens an explorer window with the given item selected.
BK_ABILITY("shell.showInFolder", kAbilityDefault) {
  const std::wstring path = Utf8ToWide(args["path"].as_string());
  if (path.empty()) {
    ctx.Fail("shell.showInFolder requires a 'path'");
    return Json();
  }
  const std::wstring params = L"/select,\"" + path + L"\"";

  std::string error;
  if (!RunShellExecute(ctx.hwnd, L"open", L"explorer.exe", params, L"",
                       SW_SHOWNORMAL, &error)) {
    ctx.Fail(error);
    return Json();
  }
  return Json(true);
}

}  // namespace bk
