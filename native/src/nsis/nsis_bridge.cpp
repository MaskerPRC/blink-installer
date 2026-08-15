#include "nsis/nsis_bridge.h"

#include <map>
#include <mutex>

#include "log.h"
#include "nsis/plugin_api.h"

namespace bk {
namespace nsis {
namespace {

std::mutex g_mutex;
std::map<std::string, int> g_functions;

}  // namespace

void RegisterFunction(const std::string& name, int address) {
  if (name.empty()) {
    BK_LOG(Warn, "RegisterAbility called with an empty name");
    return;
  }
  // GetFunctionAddress returns the code offset plus one, so zero means the
  // script passed something that was never a function address.
  if (address <= 0) {
    BK_LOGF(Warn, "RegisterAbility '%s' got a non-positive address (%d) — "
                  "did the script call GetFunctionAddress?",
            name.c_str(), address);
    return;
  }

  std::lock_guard<std::mutex> lock(g_mutex);
  g_functions[name] = address;
  BK_LOGF(Info, "registered NSIS function '%s'", name.c_str());
}

bool HasFunction(const std::string& name) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_functions.count(name) != 0;
}

std::vector<std::string> RegisteredFunctions() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<std::string> names;
  names.reserve(g_functions.size());
  for (const auto& [name, address] : g_functions) names.push_back(name);
  return names;
}

bool CallFunction(const std::string& name, std::string* error) {
  int address = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_functions.find(name);
    if (it == g_functions.end()) {
      if (error) {
        *error = "no NSIS function registered as '" + name +
                 "' — the script must call blinkkit::RegisterAbility first";
      }
      return false;
    }
    address = it->second;
  }

  if (!g_extra || !g_extra->ExecuteCodeSegment) {
    if (error) *error = "NSIS plugin parameters are not available";
    return false;
  }

  // Undo the +1 that GetFunctionAddress applied.
  g_extra->ExecuteCodeSegment(address - 1, g_parent);
  return true;
}

void ClearFunctions() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_functions.clear();
}

}  // namespace nsis
}  // namespace bk
