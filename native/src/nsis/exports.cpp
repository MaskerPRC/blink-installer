// The functions an .nsi script can call as blinkkit::Name.
//
// NSIS pushes plugin arguments in reverse, so popping left to right yields them
// in written order: for `blinkkit::SetConfig "key" "value"` the first Pop gives
// "key". Each export below pops in that same order.
#include <windows.h>

#include <string>

#include "ability.h"
#include "blink_window.h"
#include "config_store.h"
#include "json.h"
#include "log.h"
#include "nsis/nsis_bridge.h"
#include "nsis/plugin_api.h"
#include "nsis/progress.h"
#include "strings.h"
#include "taskbar.h"
#include "task_queue.h"

using namespace bk;
using namespace bk::nsis;

namespace {

bool g_window_ready = false;

UINT_PTR PluginCallbackProc(NSPIM message) {
  // NSPIM_UNLOAD is the last thing we hear before NSIS frees the DLL: drop the
  // subclasses and COM objects while their windows are still alive.
  if (message == NSPIM_UNLOAD || message == NSPIM_GUIUNLOAD) {
    UnbindProgress();
    ReleaseTaskbar();
  }
  return 0;
}

// Joins a directory and a file name without caring whether the caller supplied
// a trailing separator.
std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
  if (dir.empty()) return name;
  std::wstring out = dir;
  if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
  return out + name;
}

}  // namespace

// blinkkit::InitWindow "$PLUGINSDIR" "title" "width" "height" ["fullscreen"]
//
// Loads miniblink and creates the window, but does not show it: the script gets
// a chance to seed config first. Pass "1" as the fifth argument to cover the
// work area, which is what the splash needs.
NSIS_EXPORT(InitWindow) {
  NSIS_INIT();
  LogInit();

  const std::wstring plugins_dir = [] {
    std::wstring value;
    PopString(&value);
    return value;
  }();

  std::wstring title;
  PopString(&title);
  std::wstring width_text;
  PopString(&width_text);
  std::wstring height_text;
  PopString(&height_text);
  std::wstring fullscreen_text;
  PopString(&fullscreen_text);

  if (plugins_dir.empty()) {
    BK_LOG(Error, "InitWindow: no plugins directory given");
    PushUtf8("error: missing plugins directory");
    return;
  }

  BlinkWindow::Options options;
  options.miniblink_dll = JoinPath(plugins_dir, L"node.dll");
  options.html_file = JoinPath(plugins_dir, L"index.min.html");
  options.title = title.empty() ? L"Setup" : title;
  if (!width_text.empty()) options.width = ::_wtoi(width_text.c_str());
  if (!height_text.empty()) options.height = ::_wtoi(height_text.c_str());
  if (options.width <= 0) options.width = 800;
  if (options.height <= 0) options.height = 560;
  options.fullscreen = fullscreen_text == L"1";

  // Opt-in inspector: set BLINKKIT_DEVTOOLS to the miniblink front_end folder.
  wchar_t devtools[MAX_PATH] = {};
  if (::GetEnvironmentVariableW(L"BLINKKIT_DEVTOOLS", devtools, MAX_PATH)) {
    options.devtools_path = devtools;
  }

  if (extra && extra->RegisterPluginCallback) {
    extra->RegisterPluginCallback(::GetModuleHandleW(L"blinkkit.dll"),
                                  &PluginCallbackProc);
  }

  // Seed the store with what the script already knows.
  ConfigStore::Get().SetString("__pluginsDir", WideToUtf8(plugins_dir));

  if (!BlinkWindow::Get()->Create(options)) {
    // Fail loudly: a silent no-op here leaves the user staring at nothing.
    ::MessageBoxW(hwndParent,
                  L"The installer UI could not start.\n\n"
                  L"Set BLINKKIT_LOG to a file path and run again for details.",
                  L"Setup", MB_ICONERROR | MB_OK);
    PushUtf8("error: window creation failed");
    return;
  }

  // Take the NSIS shell out of sight for the rest of the run. Doing it here
  // rather than per page means it never gets a chance to flash into view on a
  // page transition.
  HideHostWindow(hwndParent);

  g_window_ready = true;
  PushUtf8("ok");
}

// blinkkit::ShowPage — shows the window and pumps messages until it closes.
//
// A script calls this more than once: once for the welcome page, again for the
// completion page. Each time, push the whole config across before pumping.
//
// That resync matters. Between two ShowPage calls the window is not pumping
// its own messages — NSIS is — so a config change posted in that window can be
// delivered late or coalesced, and a page that only listens for change events
// can be left sitting on a progress bar that reached 100% a second ago. Giving
// it the current state on every entry makes the page's screen a function of
// the state rather than of the events it happened to catch.
NSIS_EXPORT(ShowPage) {
  NSIS_INIT();
  if (!g_window_ready) {
    BK_LOG(Error, "ShowPage called before a successful InitWindow");
    return;
  }

  // Tells the auto-advance that NSIS reached the next page, so it stops
  // offering to click Next.
  NotePageShown();

  Json event = Json::object();
  event["type"] = Json("sync");
  event["config"] = ConfigStore::Get().Snapshot();
  BlinkWindow::Get()->DispatchEvent(event);

  BlinkWindow::Get()->Show();
  BlinkWindow::Get()->RunMessageLoop();
}

// blinkkit::ClosePage
NSIS_EXPORT(ClosePage) {
  NSIS_INIT();
  BlinkWindow::Get()->Close(true);
}

// blinkkit::SetConfig "key" "value"
NSIS_EXPORT(SetConfig) {
  NSIS_INIT();
  const std::string key = PopUtf8();
  const std::string value = PopUtf8();
  if (key.empty()) {
    BK_LOG(Warn, "SetConfig called with an empty key");
    return;
  }
  ConfigStore::Get().SetString(key, value);
}

// blinkkit::GetConfig "key"  →  Pop $0
NSIS_EXPORT(GetConfig) {
  NSIS_INIT();
  const std::string key = PopUtf8();
  PushUtf8(ConfigStore::Get().String(key));
}

// blinkkit::SetRegistryKey "HKLM|HKCU" "Software\\Vendor\\Product"
//
// The hive is explicit because a per-user install has no rights to HKLM;
// writing there would fail and the shared state would silently never persist.
NSIS_EXPORT(SetRegistryKey) {
  NSIS_INIT();
  const std::string root_name = PopUtf8();
  std::wstring subkey;
  PopString(&subkey);

  HKEY root = HKEY_LOCAL_MACHINE;
  if (root_name == "HKCU" || root_name == "HKEY_CURRENT_USER") {
    root = HKEY_CURRENT_USER;
  } else if (!root_name.empty() && root_name != "HKLM" &&
             root_name != "HKEY_LOCAL_MACHINE") {
    BK_LOGF(Warn, "SetRegistryKey: unknown root '%s', using HKLM",
            root_name.c_str());
  }

  ConfigStore::Get().SetRegistryKey(subkey, root);
  ConfigStore::Get().LoadFromRegistry();
}

// blinkkit::RegisterAbility "name" $addressFromGetFunctionAddress
NSIS_EXPORT(RegisterAbility) {
  NSIS_INIT();
  const std::string name = PopUtf8();
  std::wstring address_text;
  PopString(&address_text);
  RegisterFunction(name, ::_wtoi(address_text.c_str()));
}

// blinkkit::BindProgress — start mirroring NSIS install progress to the page.
NSIS_EXPORT(BindProgress) {
  NSIS_INIT();
  bk::nsis::BindProgress(hwndParent);
}

// blinkkit::Call "ability.name" "jsonArgs"  →  Pop $0  (JSON envelope)
//
// Lets the script use the same abilities the page has.
//
// A name plus a JSON blob, rather than a fixed row of positional parameters:
// abilities take different arguments, so a fixed arity forces every caller to
// pad and every new argument to break the signature.
NSIS_EXPORT(Call) {
  NSIS_INIT();
  const std::string name = PopUtf8();
  const std::string args_text = PopUtf8();

  Json args = Json::object();
  if (!args_text.empty()) {
    std::string parse_error;
    if (!Json::parse(args_text, args, &parse_error)) {
      BK_LOGF(Warn, "Call('%s'): arguments are not valid JSON: %s",
              name.c_str(), parse_error.c_str());
      args = Json::object();
    }
  }

  std::string error;
  const Json value = AbilityRegistry::Get().Invoke(
      name, args, BlinkWindow::Get()->hwnd(), &error);

  Json envelope = Json::object();
  if (error.empty()) {
    envelope["ok"] = Json(true);
    envelope["value"] = value;
  } else {
    envelope["ok"] = Json(false);
    envelope["error"] = Json(error);
    BK_LOGF(Warn, "Call('%s') failed: %s", name.c_str(), error.c_str());
  }
  PushUtf8(envelope.dump());
}

// blinkkit::Emit "eventType" "jsonPayload" — push a custom event to the page.
NSIS_EXPORT(Emit) {
  NSIS_INIT();
  const std::string type = PopUtf8();
  const std::string payload_text = PopUtf8();

  Json event = Json::object();
  event["type"] = Json(type);
  if (!payload_text.empty()) {
    Json payload;
    std::string parse_error;
    if (Json::parse(payload_text, payload, &parse_error)) {
      event["payload"] = payload;
    } else {
      event["payload"] = Json(payload_text);
    }
  }

  TaskQueue::Get().Post(
      [event]() { BlinkWindow::Get()->DispatchEvent(event); });
}
