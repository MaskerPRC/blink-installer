#include "config_store.h"

#include <windows.h>

#include <mutex>

#include "log.h"
#include "strings.h"

namespace bk {
namespace {

// Everything lives under one registry value as a JSON blob, matching how the
// original stored `js_setup_info`. Simple, atomic, and avoids inventing a
// mapping from JSON types onto registry value types.
constexpr wchar_t kValueName[] = L"BlinkInstallerState";

std::recursive_mutex g_mutex;

}  // namespace

ConfigStore& ConfigStore::Get() {
  static ConfigStore instance;
  return instance;
}

void ConfigStore::Set(const std::string& key, Json value) {
  ChangeListener listener;
  Json copy;
  {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    data_[key] = value;
    listener = listener_;
    copy = value;
  }
  if (listener) listener(key, copy);
}

void ConfigStore::SetString(const std::string& key, const std::string& value) {
  Set(key, Json(value));
}

Json ConfigStore::Value(const std::string& key) const {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  return data_[key];
}

std::string ConfigStore::String(const std::string& key,
                                const std::string& fallback) const {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  const Json& value = data_[key];
  if (value.is_string()) return value.as_string();
  if (value.is_null()) return fallback;
  // NSIS only speaks strings, so coerce numbers and bools rather than failing.
  if (value.is_number() || value.is_bool()) return value.dump();
  return fallback;
}

bool ConfigStore::Has(const std::string& key) const {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  return data_.has(key);
}

void ConfigStore::Remove(const std::string& key) {
  ChangeListener listener;
  {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!data_.has(key)) return;
    Json rebuilt = Json::object();
    for (const auto& [k, v] : data_.as_object()) {
      if (k != key) rebuilt[k] = v;
    }
    data_ = std::move(rebuilt);
    listener = listener_;
  }
  if (listener) listener(key, Json());
}

Json ConfigStore::Snapshot() const {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  return data_;
}

void ConfigStore::Merge(const Json& object) {
  if (!object.is_object()) return;
  for (const auto& [key, value] : object.as_object()) {
    Set(key, value);
  }
}

void ConfigStore::SetChangeListener(ChangeListener listener) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  listener_ = std::move(listener);
}

void ConfigStore::SetRegistryKey(const std::wstring& subkey, HKEY root) {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
  registry_subkey_ = subkey;
  registry_root_ = root;
}

bool ConfigStore::LoadFromRegistry() {
  std::wstring subkey;
  HKEY root = nullptr;
  {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    subkey = registry_subkey_;
    root = registry_root_;
  }
  if (subkey.empty()) return false;

  HKEY key = nullptr;
  const LSTATUS open = ::RegOpenKeyExW(root, subkey.c_str(), 0,
                                       KEY_READ | KEY_WOW64_64KEY, &key);
  if (open != ERROR_SUCCESS) {
    BK_LOGF(Debug, "ConfigStore: no persisted state at HKLM\\%s (%ld)",
            WideToUtf8(subkey).c_str(), open);
    return false;
  }

  DWORD type = 0;
  DWORD bytes = 0;
  LSTATUS status =
      ::RegQueryValueExW(key, kValueName, nullptr, &type, nullptr, &bytes);
  if (status != ERROR_SUCCESS || type != REG_SZ || bytes == 0) {
    ::RegCloseKey(key);
    return false;
  }

  std::wstring buffer(bytes / sizeof(wchar_t) + 1, L'\0');
  status = ::RegQueryValueExW(key, kValueName, nullptr, &type,
                              reinterpret_cast<LPBYTE>(buffer.data()), &bytes);
  ::RegCloseKey(key);
  if (status != ERROR_SUCCESS) return false;

  buffer.resize(::wcslen(buffer.c_str()));

  Json parsed;
  std::string error;
  if (!Json::parse(WideToUtf8(buffer), parsed, &error) || !parsed.is_object()) {
    BK_LOGF(Warn, "ConfigStore: persisted state is not valid JSON: %s",
            error.c_str());
    return false;
  }

  Merge(parsed);
  return true;
}

bool ConfigStore::SaveToRegistry() const {
  std::wstring subkey;
  std::string payload;
  HKEY root = nullptr;
  {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    subkey = registry_subkey_;
    payload = data_.dump();
    root = registry_root_;
  }
  if (subkey.empty()) return false;

  HKEY key = nullptr;
  DWORD disposition = 0;
  const LSTATUS create = ::RegCreateKeyExW(
      root, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
      KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, &disposition);
  if (create != ERROR_SUCCESS) {
    BK_LOGF(Warn, "ConfigStore: cannot open HKLM\\%s for write: %s",
            WideToUtf8(subkey).c_str(),
            FormatWin32Error(static_cast<unsigned long>(create)).c_str());
    return false;
  }

  const std::wstring wide = Utf8ToWide(payload);
  const LSTATUS status = ::RegSetValueExW(
      key, kValueName, 0, REG_SZ,
      reinterpret_cast<const BYTE*>(wide.c_str()),
      static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t)));
  ::RegCloseKey(key);

  if (status != ERROR_SUCCESS) {
    BK_LOGF(Warn, "ConfigStore: write failed: %s",
            FormatWin32Error(static_cast<unsigned long>(status)).c_str());
    return false;
  }
  return true;
}

}  // namespace bk
