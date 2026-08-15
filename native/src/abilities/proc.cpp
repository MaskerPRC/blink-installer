// Process abilities.
//
// Installers need these to detect "the app you are about to replace is still
// running" and to offer to close it.
#include <windows.h>
#include <tlhelp32.h>

#include <vector>

#include "ability.h"
#include "strings.h"

namespace bk {
namespace {

std::vector<DWORD> FindProcessIds(const std::wstring& name) {
  std::vector<DWORD> pids;
  HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return pids;

  PROCESSENTRY32W entry = {};
  entry.dwSize = sizeof(entry);

  // Process32First yields the first entry, not merely a cursor. Discarding its
  // result and going straight to Process32Next silently skips whichever
  // process happens to be enumerated first.
  if (::Process32FirstW(snapshot, &entry)) {
    do {
      if (EqualsIgnoreCase(entry.szExeFile, name)) {
        pids.push_back(entry.th32ProcessID);
      }
      entry.dwSize = sizeof(entry);
    } while (::Process32NextW(snapshot, &entry));
  }

  ::CloseHandle(snapshot);
  return pids;
}

}  // namespace

// { name: "app.exe" } -> { running: bool, pids: [...] }
BK_ABILITY("proc.exists", kAbilityDefault) {
  const std::wstring name = Utf8ToWide(args["name"].as_string());
  if (name.empty()) {
    ctx.Fail("proc.exists requires a 'name'");
    return Json();
  }

  const std::vector<DWORD> pids = FindProcessIds(name);
  Json list = Json::array();
  for (DWORD pid : pids) list.push_back(Json(static_cast<long long>(pid)));

  Json out = Json::object();
  out["running"] = Json(!pids.empty());
  out["pids"] = list;
  return out;
}

// Terminates by pid or by name, and waits for the process to actually exit.
//
// TerminateProcess only requests termination. Returning as soon as it succeeds
// leaves a race where the caller's next file operation still hits a lock held
// by the dying process — and an installer kills an app precisely so it can
// overwrite that app's files, so the wait is the whole point.
BK_ABILITY("proc.kill", kAbilitySlow) {
  std::vector<DWORD> pids;
  if (args.has("pid")) {
    pids.push_back(static_cast<DWORD>(args["pid"].as_int64()));
  } else if (args.has("name")) {
    pids = FindProcessIds(Utf8ToWide(args["name"].as_string()));
  } else {
    ctx.Fail("proc.kill requires a 'pid' or a 'name'");
    return Json();
  }

  const DWORD timeout = static_cast<DWORD>(args["timeoutMs"].as_int(5000));
  int killed = 0;
  Json failures = Json::array();

  for (DWORD pid : pids) {
    HANDLE handle =
        ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!handle) {
      Json failure = Json::object();
      failure["pid"] = Json(static_cast<long long>(pid));
      failure["error"] = Json(FormatWin32Error(::GetLastError()));
      failures.push_back(failure);
      continue;
    }

    if (::TerminateProcess(handle, 0)) {
      // WAIT_TIMEOUT here means the kill did not take effect in time; report
      // it rather than claiming success.
      if (::WaitForSingleObject(handle, timeout) == WAIT_OBJECT_0) {
        ++killed;
      } else {
        Json failure = Json::object();
        failure["pid"] = Json(static_cast<long long>(pid));
        failure["error"] = Json("process did not exit within timeout");
        failures.push_back(failure);
      }
    } else {
      Json failure = Json::object();
      failure["pid"] = Json(static_cast<long long>(pid));
      failure["error"] = Json(FormatWin32Error(::GetLastError()));
      failures.push_back(failure);
    }
    ::CloseHandle(handle);
  }

  Json out = Json::object();
  out["killed"] = Json(killed);
  out["failures"] = failures;
  return out;
}

}  // namespace bk
