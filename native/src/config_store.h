// Shared key/value state between NSIS, C++ and the installer page.
//
// C++ owns the state; the page and the NSIS script are both plain clients, and
// change notifications flow outward to them.
//
// The tempting alternative is to let the page hold it and have C++ read back
// through the bridge on demand. That cannot work here: the bridge may only be
// touched from the UI thread, so every read from an NSIS install thread would
// have to fail or block, and the two sides end up depending on each other.
#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "json.h"

namespace bk {

class ConfigStore {
 public:
  static ConfigStore& Get();

  void Set(const std::string& key, Json value);
  void SetString(const std::string& key, const std::string& value);

  Json Value(const std::string& key) const;
  std::string String(const std::string& key,
                     const std::string& fallback = {}) const;
  bool Has(const std::string& key) const;
  void Remove(const std::string& key);

  Json Snapshot() const;
  void Merge(const Json& object);

  // Invoked after every mutation, on the mutating thread. The listener is
  // responsible for hopping to the UI thread if it needs to talk to JS.
  using ChangeListener = std::function<void(const std::string& key, const Json& value)>;
  void SetChangeListener(ChangeListener listener);

  // Optional persistence under <root>\<subkey>. Opened with KEY_WOW64_64KEY so
  // a 32-bit installer reads and writes the same view a 64-bit application will
  // later see. Without the flag the writes land in Wow6432Node instead, and
  // the application reads back nothing with no error anywhere to explain it.
  //
  // The root matters: a per-user install has no rights to HKLM, so writing
  // there fails and the state silently never persists.
  void SetRegistryKey(const std::wstring& subkey, HKEY root = HKEY_LOCAL_MACHINE);
  bool LoadFromRegistry();
  bool SaveToRegistry() const;

 private:
  ConfigStore() = default;

  Json data_ = Json::object();
  std::wstring registry_subkey_;
  HKEY registry_root_ = HKEY_LOCAL_MACHINE;
  ChangeListener listener_;
};

}  // namespace bk
