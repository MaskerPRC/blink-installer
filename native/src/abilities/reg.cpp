// Registry abilities.
//
// Every call goes through KEY_WOW64_64KEY on 64-bit Windows. The installer is
// a 32-bit process, so without that flag HKLM\Software writes land silently in
// Wow6432Node — where the 64-bit application it just installed will never look.
// Nothing reports an error; the values simply are not where they are expected.
#include <windows.h>

#include <string>

#include "ability.h"
#include "strings.h"

namespace bk {
namespace {

bool ParseRoot(const std::string& name, HKEY* out) {
  if (name == "HKLM" || name == "HKEY_LOCAL_MACHINE") { *out = HKEY_LOCAL_MACHINE; return true; }
  if (name == "HKCU" || name == "HKEY_CURRENT_USER") { *out = HKEY_CURRENT_USER; return true; }
  if (name == "HKCR" || name == "HKEY_CLASSES_ROOT") { *out = HKEY_CLASSES_ROOT; return true; }
  if (name == "HKU" || name == "HKEY_USERS") { *out = HKEY_USERS; return true; }
  return false;
}

// `view` selects which registry view to touch: "64" (default), "32", or
// "default" to inherit the process's own bitness.
REGSAM ViewFlag(const std::string& view) {
  if (view == "32") return KEY_WOW64_32KEY;
  if (view == "default") return 0;
  return KEY_WOW64_64KEY;
}

}  // namespace

BK_ABILITY("reg.read", kAbilityDefault) {
  HKEY root = nullptr;
  if (!ParseRoot(args["root"].as_string("HKLM"), &root)) {
    ctx.Fail("reg.read: unknown 'root'");
    return Json();
  }
  const std::wstring subkey = Utf8ToWide(args["key"].as_string());
  const std::wstring name = Utf8ToWide(args["name"].as_string());
  const REGSAM view = ViewFlag(args["view"].as_string("64"));

  HKEY key = nullptr;
  if (::RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ | view, &key) !=
      ERROR_SUCCESS) {
    return Json();  // missing key reads as null rather than an error
  }

  DWORD type = 0;
  DWORD bytes = 0;
  LSTATUS status = ::RegQueryValueExW(key, name.c_str(), nullptr, &type,
                                      nullptr, &bytes);
  if (status != ERROR_SUCCESS) {
    ::RegCloseKey(key);
    return Json();
  }

  Json result;
  if (type == REG_SZ || type == REG_EXPAND_SZ) {
    std::wstring buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    status = ::RegQueryValueExW(key, name.c_str(), nullptr, &type,
                                reinterpret_cast<LPBYTE>(buffer.data()), &bytes);
    if (status == ERROR_SUCCESS) {
      buffer.resize(::wcslen(buffer.c_str()));
      result = Json(WideToUtf8(buffer));
    }
  } else if (type == REG_DWORD) {
    DWORD value = 0;
    bytes = sizeof(value);
    status = ::RegQueryValueExW(key, name.c_str(), nullptr, &type,
                                reinterpret_cast<LPBYTE>(&value), &bytes);
    if (status == ERROR_SUCCESS) result = Json(static_cast<long long>(value));
  } else {
    ctx.Fail("reg.read: unsupported value type");
  }

  ::RegCloseKey(key);
  return result;
}

BK_ABILITY("reg.write", kAbilityDefault) {
  HKEY root = nullptr;
  if (!ParseRoot(args["root"].as_string("HKLM"), &root)) {
    ctx.Fail("reg.write: unknown 'root'");
    return Json();
  }
  const std::wstring subkey = Utf8ToWide(args["key"].as_string());
  const std::wstring name = Utf8ToWide(args["name"].as_string());
  const REGSAM view = ViewFlag(args["view"].as_string("64"));

  HKEY key = nullptr;
  DWORD disposition = 0;
  LSTATUS status = ::RegCreateKeyExW(root, subkey.c_str(), 0, nullptr,
                                     REG_OPTION_NON_VOLATILE, KEY_WRITE | view,
                                     nullptr, &key, &disposition);
  if (status != ERROR_SUCCESS) {
    ctx.Fail("reg.write: cannot open key: " +
             FormatWin32Error(static_cast<unsigned long>(status)));
    return Json();
  }

  const Json& value = args["value"];
  if (value.is_number()) {
    const DWORD dword = static_cast<DWORD>(value.as_int64());
    status = ::RegSetValueExW(key, name.c_str(), 0, REG_DWORD,
                              reinterpret_cast<const BYTE*>(&dword),
                              sizeof(dword));
  } else {
    const std::wstring text = Utf8ToWide(value.as_string());
    status = ::RegSetValueExW(
        key, name.c_str(), 0, REG_SZ,
        reinterpret_cast<const BYTE*>(text.c_str()),
        static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
  }
  ::RegCloseKey(key);

  if (status != ERROR_SUCCESS) {
    ctx.Fail("reg.write failed: " +
             FormatWin32Error(static_cast<unsigned long>(status)));
    return Json();
  }
  return Json(true);
}

// Deletes a single value, or the whole key when no 'name' is given.
BK_ABILITY("reg.delete", kAbilityDefault) {
  HKEY root = nullptr;
  if (!ParseRoot(args["root"].as_string("HKLM"), &root)) {
    ctx.Fail("reg.delete: unknown 'root'");
    return Json();
  }
  const std::wstring subkey = Utf8ToWide(args["key"].as_string());
  const REGSAM view = ViewFlag(args["view"].as_string("64"));

  if (!args.has("name")) {
    const LSTATUS status =
        ::RegDeleteKeyExW(root, subkey.c_str(), view, 0);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
      ctx.Fail("reg.delete failed: " +
               FormatWin32Error(static_cast<unsigned long>(status)));
      return Json();
    }
    return Json(true);
  }

  HKEY key = nullptr;
  if (::RegOpenKeyExW(root, subkey.c_str(), 0, KEY_SET_VALUE | view, &key) !=
      ERROR_SUCCESS) {
    return Json(true);  // nothing to delete
  }
  const std::wstring name = Utf8ToWide(args["name"].as_string());
  const LSTATUS status = ::RegDeleteValueW(key, name.c_str());
  ::RegCloseKey(key);

  if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
    ctx.Fail("reg.delete failed: " +
             FormatWin32Error(static_cast<unsigned long>(status)));
    return Json();
  }
  return Json(true);
}

}  // namespace bk
