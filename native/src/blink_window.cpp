#include "blink_window.h"

#include <shellapi.h>  // ExtractIconEx

#include "ability.h"
#include "config_store.h"
#include "dpi.h"
#include "log.h"
#include "strings.h"
#include "task_queue.h"

namespace bk {
namespace {

// JSON is very nearly a subset of JS, but U+2028 and U+2029 are legal inside a
// JSON string while older JS parsers treat them as line terminators. Escape
// them before splicing a payload into a wkeRunJS expression.
std::string EscapeForJsLiteral(const std::string& json) {
  std::string out;
  out.reserve(json.size());
  for (size_t i = 0; i < json.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(json[i]);
    if (c == 0xE2 && i + 2 < json.size() &&
        static_cast<unsigned char>(json[i + 1]) == 0x80 &&
        (static_cast<unsigned char>(json[i + 2]) == 0xA8 ||
         static_cast<unsigned char>(json[i + 2]) == 0xA9)) {
      out += (static_cast<unsigned char>(json[i + 2]) == 0xA8) ? "\\u2028"
                                                               : "\\u2029";
      i += 2;
      continue;
    }
    out.push_back(json[i]);
  }
  return out;
}

// Reads argument `index` as a UTF-8 std::string. jsToTempString returns a
// pointer owned by the engine and only valid for this call, so copy it.
std::string ArgString(jsExecState es, int index) {
  if (index >= jsArgCount(es)) return std::string();
  const utf8* raw = jsToTempString(es, jsArg(es, index));
  return raw ? std::string(raw) : std::string();
}

Json ParseArgs(const std::string& text) {
  if (text.empty()) return Json::object();
  Json parsed;
  std::string error;
  if (!Json::parse(text, parsed, &error)) {
    BK_LOGF(Warn, "ability arguments were not valid JSON: %s", error.c_str());
    return Json::object();
  }
  return parsed;
}

// Every bridge call answers with the same envelope so the page SDK has one
// shape to unwrap: {ok:true,value:...} or {ok:false,error:"..."}.
std::string MakeEnvelope(const Json& value, const std::string& error) {
  Json envelope = Json::object();
  if (error.empty()) {
    envelope["ok"] = Json(true);
    envelope["value"] = value;
  } else {
    envelope["ok"] = Json(false);
    envelope["error"] = Json(error);
  }
  return envelope.dump();
}

// Gives the window the setup executable's own icon.
//
// miniblink registers its window class without one, so the taskbar button and
// Alt+Tab entry come up blank — the NSIS `Icon` directive only sets the icon
// on the .exe *file*, which is what Explorer shows, not what a window carries.
// Nothing sets WM_SETICON otherwise, and the two are unrelated.
//
// The icon is read back out of the running executable rather than shipped
// alongside, so it always matches whatever the build embedded, and a product
// that changes its icon does not have to remember a second place to change it.
// ExtractIconEx takes the first icon group by index rather than by resource ID,
// which is what makes this independent of how NSIS numbered it.
void ApplyWindowIcon(HWND hwnd) {
  if (!hwnd) return;

  wchar_t exe_path[MAX_PATH] = {};
  if (!::GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) {
    BK_LOG(Warn, "GetModuleFileName failed; window keeps the default icon");
    return;
  }

  HICON large = nullptr;
  HICON small = nullptr;
  if (::ExtractIconExW(exe_path, 0, &large, &small, 1) == UINT_MAX ||
      (!large && !small)) {
    // A build with no icon configured is a legitimate state, not an error.
    BK_LOG(Info, "no icon in the setup executable; leaving the default");
    return;
  }

  // Deliberately not destroyed: these live as long as the window does, and the
  // process is about to exit anyway. Freeing them here would blank the taskbar
  // button, which is the bug this function exists to fix.
  if (large) ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)large);
  if (small) ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
  BK_LOG(Info, "window icon taken from the setup executable");
}

}  // namespace

BlinkWindow* BlinkWindow::Get() {
  static BlinkWindow instance;
  return &instance;
}

bool BlinkWindow::LoadMiniblink(const std::wstring& dll_path) {
  // wkeInitialize() would LoadLibrary this itself, but it does not check the
  // result — every bound pointer would silently be null and the first call
  // would fault. Load it here so a missing DLL produces a real message.
  miniblink_ = ::LoadLibraryW(dll_path.c_str());
  if (!miniblink_) {
    BK_LOGF(Error, "cannot load miniblink from '%s': %s",
            WideToUtf8(dll_path).c_str(),
            FormatWin32Error(::GetLastError()).c_str());
    return false;
  }

  wkeSetWkeDllPath(dll_path.c_str());
  wkeInitialize();

  if (!wkeCreateWebWindow || !wkeLoadFileW || !wkeRunJS) {
    BK_LOG(Error, "miniblink loaded but its exports are missing — wrong or "
                  "corrupt node.dll?");
    return false;
  }
  return true;
}

bool BlinkWindow::Create(const Options& options) {
  if (!TaskQueue::Get().InitOnUiThread()) return false;
  if (!LoadMiniblink(options.miniblink_dll)) return false;

  int x = 0;
  int y = 0;

  // The config gives logical pixels; Win32 wants physical ones. The splash
  // skips this because its size comes from the monitor rectangle below, which
  // is already physical.
  const float scale = DpiScale();
  int width = ToPhysical(options.width);
  int height = ToPhysical(options.height);

  if (options.fullscreen) {
    // The work area of the primary monitor, not the union of all of them: a
    // layered window spanning a multi-monitor desktop has to composite several
    // thousand pixels of per-pixel alpha every frame.
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    HMONITOR monitor = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (::GetMonitorInfoW(monitor, &info)) {
      int work_width = info.rcWork.right - info.rcWork.left;
      int work_height = info.rcWork.bottom - info.rcWork.top;

      // Cap the surface.
      //
      // A per-pixel-alpha layered window is presented with UpdateLayeredWindow,
      // which reblits the *entire* surface every frame. On a 5120x2088 desktop
      // that is a 42 MB bitmap per frame; measured here, nothing was presented
      // at all. Capping keeps the splash on a surface the compositor can
      // actually push, and it is centred so it still reads as full-bleed on
      // ordinary displays (where the cap does not bite).
      const int kMaxWidth = 2560;
      const int kMaxHeight = 1440;
      width = work_width < kMaxWidth ? work_width : kMaxWidth;
      height = work_height < kMaxHeight ? work_height : kMaxHeight;

      x = info.rcWork.left + (work_width - width) / 2;
      y = info.rcWork.top + (work_height - height) / 2;
    }
  }

  // BLINKKIT_OPAQUE forces the non-layered window type. Only for measuring
  // what per-pixel alpha costs; the installer always wants transparency.
  bool transparent = options.transparent;
  {
    char buffer[8] = {};
    if (::GetEnvironmentVariableA("BLINKKIT_OPAQUE", buffer, sizeof(buffer)) > 0 &&
        buffer[0] == '1') {
      transparent = false;
    }
  }

  view_ = wkeCreateWebWindow(
      transparent ? WKE_WINDOW_TYPE_TRANSPARENT : WKE_WINDOW_TYPE_POPUP,
      nullptr, x, y, width, height);
  if (!view_) {
    BK_LOG(Error, "wkeCreateWebWindow returned null");
    return false;
  }

  hwnd_ = wkeGetWindowHandle(view_);
  wkeSetWindowTitleW(view_, options.title.c_str());
  ApplyWindowIcon(hwnd_);

  // Make one CSS pixel cover `scale` physical ones, so a page written in
  // ordinary CSS units comes out the intended physical size. Without this the
  // window above is merely bigger and the text inside it stays small — the
  // page would just get more room to be unreadable in.
  //
  // Guarded on the pointer: the export table is filled in at load time and an
  // older miniblink build may not have this one.
  if (scale != 1.0f) {
    if (wkeSetZoomFactor) {
      wkeSetZoomFactor(view_, scale);
      BK_LOGF(Info, "display scale %.2f applied as page zoom", scale);
    } else {
      BK_LOG(Warn,
             "wkeSetZoomFactor missing from this miniblink build; the page "
             "will render small on a scaled display");
    }
  }

  // Paint cadence.
  //
  // miniblink throttles how often it repaints, and on a layered window that
  // throttle is what the user sees: measured here, the page ticked
  // requestAnimationFrame at 59 Hz while only ~28 of those frames a second
  // were ever presented. `drawMinInterval` is the minimum gap in milliseconds
  // between draws and defaults high enough to halve a 60 Hz animation.
  //
  // Lowering it costs CPU on a surface this large, so it is a knob rather than
  // a constant: BLINKKIT_DRAW_INTERVAL overrides it for measurement.
  {
    char interval[16] = "3";
    char buffer[16] = {};
    if (::GetEnvironmentVariableA("BLINKKIT_DRAW_INTERVAL", buffer,
                                  sizeof(buffer)) > 0) {
      ::strncpy(interval, buffer, sizeof(interval) - 1);
    }
    if (wkeSetDebugConfig) {
      wkeSetDebugConfig(view_, "drawMinInterval", interval);
      // The timer wake interval bounds how soon a rAF callback can run at all.
      wkeSetDebugConfig(view_, "wakeMinInterval", interval);
    }
    BK_LOGF(Info, "paint cadence: drawMinInterval=%s", interval);
  }
  if (options.fullscreen) {
    // Z-order only — SWP_NOMOVE | SWP_NOSIZE matters here.
    //
    // Resizing a miniblink window with SetWindowPos after creation leaves its
    // layered surface at the old size and the window then presents nothing at
    // all: a completely transparent rectangle, no error anywhere. The geometry
    // has to come from wkeCreateWebWindow and be left alone.
    //
    // Topmost because a splash that opens behind whatever the user had in
    // front is not a splash. win.setBounds drops it again on the way out.
    ::SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  } else {
    wkeMoveToCenter(view_);
  }

  wkeOnWindowClosing(view_, &BlinkWindow::OnWindowClosing, this);
  wkeOnWindowDestroy(view_, &BlinkWindow::OnWindowDestroy, this);

  BindNativeFunctions();

  if (!options.devtools_path.empty()) {
    wkeSetDebugConfig(view_, "showDevTools",
                      WideToUtf8(options.devtools_path).c_str());
  }

  // Mirror config mutations into the page. The store may be written from the
  // NSIS install thread, so hop to the UI thread before touching JS.
  ConfigStore::Get().SetChangeListener(
      [](const std::string& key, const Json& value) {
        Json event = Json::object();
        event["type"] = Json("config");
        event["key"] = Json(key);
        event["value"] = value;
        TaskQueue::Get().Post(
            [event]() { BlinkWindow::Get()->DispatchEvent(event); });
      });

  // Surface page-side failures in the log. Without this a script error leaves
  // a blank window and no way to find out why.
  wkeOnConsole(view_, &BlinkWindow::OnConsole, this);
  wkeOnLoadingFinish(view_, &BlinkWindow::OnLoadingFinish, this);

  wkeLoadFileW(view_, options.html_file.c_str());
  // Report the rectangle actually created, not the one requested — they differ
  // whenever fullscreen is on, and printing the request made a fullscreen
  // window look like it had ignored the flag.
  BK_LOGF(Info, "installer window created %dx%d at %d,%d%s, page: %s", width,
          height, x, y, options.fullscreen ? " (fullscreen)" : "",
          WideToUtf8(options.html_file).c_str());
  return true;
}

void BlinkWindow::BindNativeFunctions() {
  // Bound as bare globals; the page SDK wraps them into a typed API and does
  // not expect callers to touch these directly.
  wkeJsBindFunction("__blinkInvoke", &BlinkWindow::JsInvoke, this, 2);
  wkeJsBindFunction("__blinkInvokeAsync", &BlinkWindow::JsInvokeAsync, this, 3);
  wkeJsBindFunction("__blinkSetCloseGuard", &BlinkWindow::JsSetCloseGuard, this, 1);
}

jsValue BlinkWindow::JsInvoke(jsExecState es, void* param) {
  auto* self = static_cast<BlinkWindow*>(param);
  const std::string name = ArgString(es, 0);
  const Json args = ParseArgs(ArgString(es, 1));

  BK_LOGF(Debug, "invoke %s", name.c_str());

  std::string error;
  const Json value =
      AbilityRegistry::Get().Invoke(name, args, self->hwnd_, &error);
  if (!error.empty()) {
    BK_LOGF(Warn, "ability '%s' failed: %s", name.c_str(), error.c_str());
  }
  return jsString(es, MakeEnvelope(value, error).c_str());
}

jsValue BlinkWindow::JsInvokeAsync(jsExecState es, void* param) {
  auto* self = static_cast<BlinkWindow*>(param);
  const std::string name = ArgString(es, 0);
  const Json args = ParseArgs(ArgString(es, 1));
  const std::string call_id = ArgString(es, 2);

  AbilityRegistry::Get().InvokeAsync(
      name, args, self->hwnd_,
      [call_id, name](Json value, std::string error) {
        if (!error.empty()) {
          BK_LOGF(Warn, "async ability '%s' failed: %s", name.c_str(),
                  error.c_str());
        }
        Json event = Json::object();
        event["type"] = Json("invoke-result");
        event["callId"] = Json(call_id);
        if (error.empty()) {
          event["ok"] = Json(true);
          event["value"] = value;
        } else {
          event["ok"] = Json(false);
          event["error"] = Json(error);
        }
        BlinkWindow::Get()->DispatchEvent(event);
      });

  return jsUndefined();
}

jsValue BlinkWindow::JsSetCloseGuard(jsExecState es, void* param) {
  auto* self = static_cast<BlinkWindow*>(param);
  self->close_guard_ = ArgString(es, 0) == "true";
  return jsUndefined();
}

bool BlinkWindow::OnWindowClosing(wkeWebView, void* param) {
  auto* self = static_cast<BlinkWindow*>(param);
  if (!self->close_guard_ || self->closing_) return true;

  // The page asked to handle this itself: tell it, and veto the close. It is
  // expected to call win.close({force:true}) once the user confirms.
  Json event = Json::object();
  event["type"] = Json("window-closing");
  self->DispatchEvent(event);
  return false;
}

void BlinkWindow::OnWindowDestroy(wkeWebView, void* param) {
  auto* self = static_cast<BlinkWindow*>(param);
  self->view_ = nullptr;
  self->hwnd_ = nullptr;
  BK_LOG(Info, "installer window destroyed");
  ::PostQuitMessage(0);
}

void BlinkWindow::OnConsole(wkeWebView, void*, wkeConsoleLevel level,
                            const wkeString message, const wkeString sourceName,
                            unsigned sourceLine, const wkeString stackTrace) {
  const char* text = message ? wkeGetString(message) : "";
  const char* source = sourceName ? wkeGetString(sourceName) : "";
  const char* stack = stackTrace ? wkeGetString(stackTrace) : "";

  const LogLevel mapped =
      (level >= wkeLevelError) ? LogLevel::Error
      : (level == wkeLevelWarning) ? LogLevel::Warn
                                   : LogLevel::Debug;
  LogWrite(mapped, "page", static_cast<int>(sourceLine),
           std::string(text ? text : "") +
               (source && *source ? std::string("  [") + source + "]" : "") +
               (stack && *stack ? std::string("\n") + stack : ""));
}

void BlinkWindow::OnLoadingFinish(wkeWebView, void*, const wkeString url,
                                  wkeLoadingResult result,
                                  const wkeString failedReason) {
  if (result == WKE_LOADING_SUCCEEDED) {
    BK_LOG(Info, "page loaded");
    return;
  }
  BK_LOGF(Error, "page failed to load: %s (%s)",
          url ? wkeGetString(url) : "?",
          failedReason ? wkeGetString(failedReason) : "no reason given");
}

void BlinkWindow::DispatchEvent(const Json& event) {
  if (!view_) return;
  const std::string script =
      "if(window.__blinkDispatch)window.__blinkDispatch(" +
      EscapeForJsLiteral(event.dump()) + ");";
  wkeRunJS(view_, script.c_str());
}

void BlinkWindow::Show() {
  if (!view_) return;
  wkeShowWindow(view_, true);
  if (hwnd_) {
    ::SetForegroundWindow(hwnd_);
  }
}

void BlinkWindow::RunMessageLoop() {
  MSG msg;
  while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
    ::TranslateMessage(&msg);
    ::DispatchMessageW(&msg);
  }
}

void BlinkWindow::Close(bool force) {
  if (force) closing_ = true;
  if (hwnd_) ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void BlinkWindow::Minimize() {
  if (hwnd_) ::ShowWindow(hwnd_, SW_MINIMIZE);
}

void BlinkWindow::Resize(int width, int height) {
  if (!hwnd_) return;
  ::SetWindowPos(hwnd_, nullptr, 0, 0, width, height,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
}

}  // namespace bk
