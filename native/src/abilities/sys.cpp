// System information abilities.
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ability.h"
#include "dpi.h"
#include "strings.h"

namespace bk {
namespace {

// GetVersionEx and friends lie unless the executable carries a compatibility
// manifest naming each supported OS — and we are a DLL inside somebody else's
// stub, so we do not control that manifest. RtlGetNtVersionNumbers reports the
// real numbers regardless. It is undocumented but has been stable since XP.
using RtlGetNtVersionNumbersFn = void(WINAPI*)(DWORD*, DWORD*, DWORD*);

bool RealOsVersion(DWORD* major, DWORD* minor, DWORD* build) {
  const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
  if (!ntdll) return false;
  const auto fn = reinterpret_cast<RtlGetNtVersionNumbersFn>(
      ::GetProcAddress(ntdll, "RtlGetNtVersionNumbers"));
  if (!fn) return false;

  fn(major, minor, build);
  *build &= 0x0FFFFFFF;  // top nibble is flags, not part of the build number
  return true;
}

std::string FriendlyName(DWORD major, DWORD minor, DWORD build) {
  if (major == 10 && build >= 22000) return "Windows 11";
  if (major == 10) return "Windows 10";
  if (major == 6 && minor == 3) return "Windows 8.1";
  if (major == 6 && minor == 2) return "Windows 8";
  if (major == 6 && minor == 1) return "Windows 7";
  if (major == 6 && minor == 0) return "Windows Vista";
  if (major == 5) return "Windows XP";
  return "Windows";
}

int CALLBACK EnumFontProc(const LOGFONTW* font, const TEXTMETRICW*, DWORD,
                          LPARAM param) {
  auto* names = reinterpret_cast<std::vector<std::wstring>*>(param);
  // Names starting with '@' are the vertical-writing variants; the page never
  // wants those in a font picker.
  if (font && font->lfFaceName[0] != L'@') {
    names->emplace_back(font->lfFaceName);
  }
  return TRUE;
}

}  // namespace

BK_ABILITY("sys.osVersion", kAbilityDefault) {
  DWORD major = 0, minor = 0, build = 0;
  if (!RealOsVersion(&major, &minor, &build)) {
    ctx.Fail("cannot read OS version from ntdll");
    return Json();
  }

  Json out = Json::object();
  out["major"] = Json(static_cast<long long>(major));
  out["minor"] = Json(static_cast<long long>(minor));
  out["build"] = Json(static_cast<long long>(build));
  out["name"] = Json(FriendlyName(major, minor, build));

  // A 32-bit installer on 64-bit Windows sees itself as 32-bit; ask the OS
  // what it actually is so the page can pick the right payload.
  BOOL wow64 = FALSE;
  ::IsWow64Process(::GetCurrentProcess(), &wow64);
  out["is64bit"] = Json(wow64 == TRUE || sizeof(void*) == 8);
  return out;
}

// Installed font family names, deduplicated and sorted.
BK_ABILITY("sys.fonts", kAbilityUiThread) {
  const HDC dc = ::GetDC(ctx.hwnd);
  if (!dc) {
    ctx.Fail("GetDC failed");
    return Json();
  }

  std::vector<std::wstring> names;
  LOGFONTW query = {};
  query.lfCharSet = DEFAULT_CHARSET;
  ::EnumFontFamiliesExW(dc, &query, &EnumFontProc,
                        reinterpret_cast<LPARAM>(&names), 0);
  ::ReleaseDC(ctx.hwnd, dc);

  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());

  // An array, not an object keyed by face name: this is a list. Hand-building
  // JSON of the form {"name":"1",...} also breaks on any face name that
  // contains a quote.
  Json list = Json::array();
  for (const std::wstring& name : names) list.push_back(Json(WideToUtf8(name)));
  return list;
}

// Geometry of the monitor the installer is on.
//
// A full-screen splash needs real pixel bounds, and it must be *one* monitor's
// — spanning a multi-monitor desktop would mean compositing a layered window
// several thousand pixels wide every frame.
BK_ABILITY("sys.screen", kAbilityUiThread) {
  HMONITOR monitor = ctx.hwnd
                         ? ::MonitorFromWindow(ctx.hwnd, MONITOR_DEFAULTTONEAREST)
                         : ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO info = {};
  info.cbSize = sizeof(info);
  if (!::GetMonitorInfoW(monitor, &info)) {
    ctx.Fail("GetMonitorInfo failed: " + FormatWin32Error(::GetLastError()));
    return Json();
  }

  // Reported in logical pixels, the same units the page uses everywhere else.
  // A page computing `(screen.width - cardWidth) / 2` is mixing this with a
  // width it wrote in CSS, so the two have to be in the same unit or the
  // window lands off-centre by half the scaling error.
  Json work = Json::object();
  work["x"] = Json(ToLogical(info.rcWork.left));
  work["y"] = Json(ToLogical(info.rcWork.top));
  work["width"] = Json(ToLogical(info.rcWork.right - info.rcWork.left));
  work["height"] = Json(ToLogical(info.rcWork.bottom - info.rcWork.top));

  Json full = Json::object();
  full["x"] = Json(ToLogical(info.rcMonitor.left));
  full["y"] = Json(ToLogical(info.rcMonitor.top));
  full["width"] = Json(ToLogical(info.rcMonitor.right - info.rcMonitor.left));
  full["height"] = Json(ToLogical(info.rcMonitor.bottom - info.rcMonitor.top));

  Json out = Json::object();
  // Excludes the taskbar; what a splash should cover.
  out["work"] = work;
  out["full"] = full;
  // For the rare page that genuinely needs physical pixels.
  out["scale"] = Json(static_cast<double>(DpiScale()));
  out["primary"] = Json((info.dwFlags & MONITORINFOF_PRIMARY) != 0);
  return out;
}

// Expands %ENVIRONMENT% references, so a page can resolve paths NSIS handed it.
BK_ABILITY("sys.expandEnv", kAbilityDefault) {
  const std::wstring input = Utf8ToWide(args["value"].as_string());
  if (input.empty()) return Json(std::string());

  const DWORD needed = ::ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
  if (needed == 0) {
    ctx.Fail("ExpandEnvironmentStrings failed: " +
             FormatWin32Error(::GetLastError()));
    return Json();
  }
  std::wstring out(needed, L'\0');
  ::ExpandEnvironmentStringsW(input.c_str(), out.data(), needed);
  out.resize(::wcslen(out.c_str()));
  return Json(WideToUtf8(out));
}

}  // namespace bk
