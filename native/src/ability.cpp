#include "ability.h"

#include <thread>

#include "log.h"
#include "task_queue.h"

namespace bk {

AbilityRegistry& AbilityRegistry::Get() {
  // Deliberately leaked. Abilities register from static initializers and the
  // registry is consulted from NSIS callbacks that can fire late in process
  // teardown; never destroying it removes any static-destruction-order hazard.
  static AbilityRegistry* instance = new AbilityRegistry();
  return *instance;
}

void AbilityRegistry::Register(const char* name, AbilityFn fn, unsigned flags) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (entries_.count(name)) {
    // Two abilities claiming one name is a build-time mistake; keep the first
    // and make the collision visible rather than silently shadowing.
    BK_LOGF(Error, "duplicate ability registration: %s", name);
    return;
  }
  entries_[name] = Entry{fn, flags};
}

bool AbilityRegistry::Lookup(const std::string& name, Entry* out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(name);
  if (it == entries_.end()) return false;
  *out = it->second;
  return true;
}

bool AbilityRegistry::Has(const std::string& name) const {
  Entry ignored;
  return Lookup(name, &ignored);
}

std::vector<std::string> AbilityRegistry::Names() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> names;
  names.reserve(entries_.size());
  for (const auto& [name, entry] : entries_) names.push_back(name);
  return names;
}

Json AbilityRegistry::InvokeInline(const Entry& entry, const Json& args,
                                   HWND hwnd, std::string* error) {
  AbilityContext ctx;
  ctx.hwnd = hwnd;
  Json value = entry.fn(args, ctx);
  if (ctx.failed()) {
    if (error) *error = ctx.error;
    return Json();
  }
  return value;
}

Json AbilityRegistry::Invoke(const std::string& name, const Json& args,
                             HWND hwnd, std::string* error) {
  Entry entry;
  if (!Lookup(name, &entry)) {
    if (error) *error = "unknown ability: " + name;
    return Json();
  }

  const bool needs_ui = (entry.flags & kAbilityUiThread) != 0;
  if (!needs_ui || TaskQueue::Get().IsUiThread()) {
    return InvokeInline(entry, args, hwnd, error);
  }

  // Marshal onto the UI thread and wait. Send() runs inline when already
  // there, so this cannot deadlock against itself.
  Json value;
  std::string inner_error;
  const bool delivered = TaskQueue::Get().Send([&]() {
    value = InvokeInline(entry, args, hwnd, &inner_error);
  });

  if (!delivered) {
    if (error) *error = "ability '" + name + "' timed out waiting for the UI thread";
    return Json();
  }
  if (!inner_error.empty() && error) *error = inner_error;
  return value;
}

void AbilityRegistry::InvokeAsync(const std::string& name, const Json& args,
                                  HWND hwnd, DoneCallback done) {
  // Copy the arguments: the worker outlives this call.
  std::thread([this, name, args, hwnd, done = std::move(done)]() {
    std::string error;
    Json value = Invoke(name, args, hwnd, &error);
    TaskQueue::Get().Post(
        [done, value = std::move(value), error = std::move(error)]() {
          done(value, error);
        });
  }).detach();
}

}  // namespace bk
