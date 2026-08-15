// The ability registry.
//
// Replaces the ~120-line `if (abilityName == "X") ... else if ...` chain that
// dispatched every native call. Abilities now self-register from their own
// translation unit, so adding one means adding a file — there is no central
// switch to edit and no way to forget a branch.
//
//   // abilities/fs.cpp
//   BK_ABILITY("fs.pickDirectory", bk::kAbilityUiThread) {
//     return Json(PickDirectory(ctx.hwnd, args["title"].as_string()));
//   }
//
// Inside the body, `args` and `ctx` are in scope and the return value is the
// ability's result. Throwing is not supported; report failure with ctx.Fail().
#pragma once

#include <windows.h>

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "json.h"

namespace bk {

enum AbilityFlags : unsigned {
  kAbilityDefault = 0,
  // Touches the UI: dialogs, window geometry, anything owned by the UI thread.
  // Calls arriving on another thread are marshaled before the body runs.
  kAbilityUiThread = 1u << 0,
  // May block for a noticeable time. Callers should prefer InvokeAsync so the
  // installer window keeps repainting.
  kAbilitySlow = 1u << 1,
};

struct AbilityContext {
  // Main installer window; use as the owner for modal dialogs so they center
  // correctly and block the right window.
  HWND hwnd = nullptr;

  // Records a failure. The registry turns this into an error result rather
  // than a value, and the page sees a rejected promise.
  void Fail(std::string message) { error = std::move(message); }
  bool failed() const { return !error.empty(); }

  std::string error;
};

using AbilityFn = Json (*)(const Json& args, AbilityContext& ctx);

class AbilityRegistry {
 public:
  static AbilityRegistry& Get();

  void Register(const char* name, AbilityFn fn, unsigned flags);
  bool Has(const std::string& name) const;
  std::vector<std::string> Names() const;

  // Runs the ability, marshaling to the UI thread when it asks for that.
  // Returns the ability's value; on failure returns null and fills `error`.
  Json Invoke(const std::string& name, const Json& args, HWND hwnd,
              std::string* error);

  // Runs on a worker thread and delivers the result via `done`, which is
  // invoked on the UI thread. Use for abilities flagged kAbilitySlow so the
  // window keeps pumping messages while they run.
  using DoneCallback = std::function<void(Json value, std::string error)>;
  void InvokeAsync(const std::string& name, const Json& args, HWND hwnd,
                   DoneCallback done);

 private:
  AbilityRegistry() = default;

  struct Entry {
    AbilityFn fn = nullptr;
    unsigned flags = 0;
  };

  Json InvokeInline(const Entry& entry, const Json& args, HWND hwnd,
                    std::string* error);
  bool Lookup(const std::string& name, Entry* out) const;

  std::map<std::string, Entry> entries_;
  mutable std::mutex mutex_;
};

namespace detail {
struct AbilityRegistrar {
  AbilityRegistrar(const char* name, AbilityFn fn, unsigned flags) {
    AbilityRegistry::Get().Register(name, fn, flags);
  }
};
}  // namespace detail

#define BK_ABILITY_CAT_(a, b) a##b
#define BK_ABILITY_CAT(a, b) BK_ABILITY_CAT_(a, b)

#define BK_ABILITY(NAME, FLAGS)                                              \
  static ::bk::Json BK_ABILITY_CAT(bk_ability_fn_, __LINE__)(                \
      const ::bk::Json& args, ::bk::AbilityContext& ctx);                    \
  static ::bk::detail::AbilityRegistrar BK_ABILITY_CAT(bk_ability_reg_,      \
                                                       __LINE__)(            \
      NAME, &BK_ABILITY_CAT(bk_ability_fn_, __LINE__), (FLAGS));             \
  static ::bk::Json BK_ABILITY_CAT(bk_ability_fn_, __LINE__)(                \
      [[maybe_unused]] const ::bk::Json& args,                               \
      [[maybe_unused]] ::bk::AbilityContext& ctx)

}  // namespace bk
