// Shared config abilities — the page's view of the NSIS/C++/JS state store.
#include "ability.h"
#include "config_store.h"

namespace bk {

BK_ABILITY("config.get", kAbilityDefault) {
  const std::string key = args["key"].as_string();
  if (key.empty()) {
    ctx.Fail("config.get requires a 'key'");
    return Json();
  }
  return ConfigStore::Get().Value(key);
}

BK_ABILITY("config.set", kAbilityDefault) {
  const std::string key = args["key"].as_string();
  if (key.empty()) {
    ctx.Fail("config.set requires a 'key'");
    return Json();
  }
  ConfigStore::Get().Set(key, args["value"]);
  return Json(true);
}

BK_ABILITY("config.all", kAbilityDefault) {
  return ConfigStore::Get().Snapshot();
}

// Writes the whole store to the registry. The script decides when this is
// worth doing; it is not automatic on every mutation.
BK_ABILITY("config.persist", kAbilityDefault) {
  if (!ConfigStore::Get().SaveToRegistry()) {
    ctx.Fail("config.persist failed — no registry key configured, or the "
             "installer lacks the rights to write it");
    return Json();
  }
  return Json(true);
}

}  // namespace bk
