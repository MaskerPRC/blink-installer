// Filesystem and volume abilities.
#include <windows.h>
#include <shlobj.h>

#include <string>
#include <vector>

#include "ability.h"
#include "strings.h"

namespace bk {
namespace {

// Why three states rather than a bool.
//
// "The user cancelled" and "this picker is not available here" both used to
// come back as false, and the caller chained the two pickers with `&&`. So
// cancelling the modern dialog opened the old one immediately behind it: two
// dialogs for one request, and no way to decline. Cancel is an answer, not a
// failure, and only a genuine unavailability may fall through.
enum class PickOutcome { kPicked, kCancelled, kUnavailable };

// Vista-and-later folder picker. Much better UX than SHBrowseForFolder: the
// user gets a real explorer view with a path box and favourites.
PickOutcome PickWithFileDialog(HWND owner, const std::wstring& title,
                               const std::wstring& initial,
                               std::wstring* out) {
  IFileOpenDialog* dialog = nullptr;
  HRESULT hr = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
  if (FAILED(hr)) return PickOutcome::kUnavailable;

  bool ok = false;
  DWORD options = 0;
  if (SUCCEEDED(dialog->GetOptions(&options))) {
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                       FOS_PATHMUSTEXIST);
  }
  if (!title.empty()) dialog->SetTitle(title.c_str());

  if (!initial.empty()) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(::SHCreateItemFromParsingName(initial.c_str(), nullptr,
                                                IID_PPV_ARGS(&item)))) {
      dialog->SetFolder(item);
      item->Release();
    }
  }

  const HRESULT shown = dialog->Show(owner);
  if (SUCCEEDED(shown)) {
    IShellItem* result = nullptr;
    if (SUCCEEDED(dialog->GetResult(&result))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        out->assign(path);
        ::CoTaskMemFree(path);
        ok = true;
      }
      result->Release();
    }
  }
  dialog->Release();

  if (ok) return PickOutcome::kPicked;
  // The documented result for closing or cancelling the dialog. This is the
  // one case that must not fall through to the legacy picker.
  if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
    return PickOutcome::kCancelled;
  }
  // Shown but nothing usable came back, or it refused to open at all: let the
  // fallback try, since the user has not actually been asked anything yet.
  return PickOutcome::kUnavailable;
}

int CALLBACK BrowseCallback(HWND hwnd, UINT msg, LPARAM, LPARAM data) {
  if (msg == BFFM_INITIALIZED && data) {
    ::SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
  }
  return 0;
}

// Fallback for pre-Vista, where IFileOpenDialog does not exist.
bool PickWithShBrowse(HWND owner, const std::wstring& title,
                      const std::wstring& initial, std::wstring* out) {
  wchar_t display[MAX_PATH] = {};
  BROWSEINFOW info = {};
  info.hwndOwner = owner;
  info.pszDisplayName = display;
  info.lpszTitle = title.c_str();
  info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  info.lpfn = &BrowseCallback;
  info.lParam = reinterpret_cast<LPARAM>(initial.empty() ? nullptr : initial.c_str());

  LPITEMIDLIST idl = ::SHBrowseForFolderW(&info);
  if (!idl) return false;

  wchar_t path[MAX_PATH] = {};
  const bool ok = ::SHGetPathFromIDListW(idl, path) != FALSE;
  ::CoTaskMemFree(idl);
  if (!ok) return false;

  std::wstring picked(path);
  while (!picked.empty() && picked.back() == L'\\') picked.pop_back();
  out->assign(picked);
  return true;
}

}  // namespace

// Returns the chosen path, or null when the user cancelled.
//
// null rather than a sentinel string: anything a caller might compare against
// ("", "-1") is also a value the picker could legitimately produce, so
// cancellation has to be signalled out of band to stay unambiguous.
BK_ABILITY("fs.pickDirectory", kAbilityUiThread) {
  const std::wstring title = Utf8ToWide(args["title"].as_string());
  const std::wstring initial = Utf8ToWide(args["defaultPath"].as_string());

  std::wstring picked;
  PickOutcome outcome = PickWithFileDialog(ctx.hwnd, title, initial, &picked);

  // Only when the modern picker never got to ask. A cancel is the user's
  // answer and ends the request here.
  if (outcome == PickOutcome::kUnavailable &&
      PickWithShBrowse(ctx.hwnd, title, initial, &picked)) {
    outcome = PickOutcome::kPicked;
  }

  if (outcome != PickOutcome::kPicked) return Json();  // cancelled
  return Json(WideToUtf8(picked));
}

// Free/total bytes for the volume containing `path`.
//
// GetDiskFreeSpaceExW, not GetDiskFreeSpace: the latter reports cluster counts
// that overflow a 32-bit multiply on large volumes, and ignores per-user
// quotas. Both matter when the question being asked is "will this install fit".
BK_ABILITY("fs.diskSpace", kAbilityDefault) {
  std::wstring path = Utf8ToWide(args["path"].as_string());
  if (path.empty()) {
    ctx.Fail("fs.diskSpace requires a 'path'");
    return Json();
  }
  // Accept a bare drive letter as well as a full path.
  if (path.size() == 1) path += L":\\";
  if (path.size() == 2 && path[1] == L':') path += L'\\';

  // The question an installer asks is always about a directory that does not
  // exist yet, and GetDiskFreeSpaceEx fails outright on a missing path. Walk
  // up to the nearest existing ancestor, which is on the same volume and so
  // gives the answer the caller actually wanted.
  std::wstring probe = path;
  while (::GetFileAttributesW(probe.c_str()) == INVALID_FILE_ATTRIBUTES) {
    const size_t slash = probe.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash < 2) break;
    probe.erase(slash);
    // "C:" alone is the current directory on that drive, not its root.
    if (probe.size() == 2 && probe[1] == L':') probe += L'\\';
  }

  ULARGE_INTEGER available = {}, total = {}, free = {};
  if (!::GetDiskFreeSpaceExW(probe.c_str(), &available, &total, &free)) {
    ctx.Fail("GetDiskFreeSpaceEx failed for '" + WideToUtf8(probe) +
             "': " + FormatWin32Error(::GetLastError()));
    return Json();
  }

  Json out = Json::object();
  out["total"] = Json(static_cast<unsigned long long>(total.QuadPart));
  out["free"] = Json(static_cast<unsigned long long>(free.QuadPart));
  // What this user may actually consume, which is what an installer should
  // check against — it differs from `free` when disk quotas are in effect.
  out["available"] = Json(static_cast<unsigned long long>(available.QuadPart));
  return out;
}

// Lists fixed drives with their capacity.
BK_ABILITY("fs.drives", kAbilityDefault) {
  wchar_t buffer[512] = {};
  const DWORD len = ::GetLogicalDriveStringsW(
      static_cast<DWORD>(std::size(buffer)), buffer);
  if (len == 0 || len > std::size(buffer)) {
    ctx.Fail("GetLogicalDriveStrings failed: " +
             FormatWin32Error(::GetLastError()));
    return Json();
  }

  Json list = Json::array();
  for (const wchar_t* p = buffer; *p; p += ::wcslen(p) + 1) {
    if (::GetDriveTypeW(p) != DRIVE_FIXED) continue;

    Json entry = Json::object();
    entry["root"] = Json(WideToUtf8(p));
    entry["letter"] = Json(WideToUtf8(std::wstring(1, p[0])));

    ULARGE_INTEGER available = {}, total = {}, free = {};
    if (::GetDiskFreeSpaceExW(p, &available, &total, &free)) {
      entry["total"] = Json(static_cast<unsigned long long>(total.QuadPart));
      entry["free"] = Json(static_cast<unsigned long long>(free.QuadPart));
      entry["available"] =
          Json(static_cast<unsigned long long>(available.QuadPart));
    }
    list.push_back(entry);
  }
  return list;
}

BK_ABILITY("fs.exists", kAbilityDefault) {
  const std::wstring path = Utf8ToWide(args["path"].as_string());
  if (path.empty()) {
    ctx.Fail("fs.exists requires a 'path'");
    return Json();
  }
  const DWORD attrs = ::GetFileAttributesW(path.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return Json(false);

  Json out = Json::object();
  out["exists"] = Json(true);
  out["directory"] = Json((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
  return out;
}

}  // namespace bk
