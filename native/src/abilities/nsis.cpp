// Abilities that reach back into the NSIS script.
#include "ability.h"
#include "nsis/nsis_bridge.h"

namespace bk {

// Runs a script function the .nsi registered with blinkkit::RegisterAbility.
//
// Flagged UI-thread because NSIS executes the code segment on the thread that
// owns its window, and slow because the script can do anything — file copies,
// downloads, reboots — so the page should await it rather than block on it.
BK_ABILITY("nsis.call", kAbilityUiThread | kAbilitySlow) {
  const std::string name = args["name"].as_string();
  if (name.empty()) {
    ctx.Fail("nsis.call requires a 'name'");
    return Json();
  }

  std::string error;
  if (!nsis::CallFunction(name, &error)) {
    ctx.Fail(error);
    return Json();
  }
  return Json(true);
}

// Lets a page discover what the script exposed, so a shared UI template can
// light up optional steps only when the script actually implements them.
BK_ABILITY("nsis.functions", kAbilityDefault) {
  Json list = Json::array();
  for (const std::string& name : nsis::RegisteredFunctions()) {
    list.push_back(Json(name));
  }
  return list;
}

}  // namespace bk
