// The installer window.
//
// A thin wrapper over one miniblink window, and deliberately little else.
//
// It is tempting to create the layered window yourself, host a child view,
// forward mouse/keyboard/IME messages into the engine by hand and blit its
// device context into your own render target each frame. That is several
// hundred lines to arrive at a browser filling a window.
//
// miniblink already does all of it: wkeCreateWebWindow with
// WKE_WINDOW_TYPE_TRANSPARENT returns a managed transparent layered window that
// handles its own painting and input. So we ask for one and get out of the way.
#pragma once

#include <windows.h>

#include <string>

#include "json.h"

// Pulls in miniblink's dynamic-binding declarations. ENABLE_WKE is deliberately
// left undefined so the header emits function *pointers* bound at runtime via
// LoadLibrary/GetProcAddress rather than an import-library dependency.
#include "wke.h"

namespace bk {

class BlinkWindow {
 public:
  struct Options {
    std::wstring miniblink_dll;  // full path to node.dll (miniblink)
    std::wstring html_file;      // full path to the inlined installer page
    std::wstring title = L"Setup";
    int width = 800;
    int height = 560;
    bool transparent = true;
    /**
     * Create covering the whole work area instead of `width` x `height`.
     *
     * Used for the splash. It has to be decided at creation time: growing a
     * small window to full screen afterwards shows the small one for a frame,
     * and on a layered window that frame is very visible.
     */
    bool fullscreen = false;
    std::wstring devtools_path;  // non-empty enables the miniblink inspector
  };

  static BlinkWindow* Get();

  bool Create(const Options& options);
  void Show();

  // Pumps messages until the window closes. The NSIS page function blocks here
  // for as long as the page is on screen.
  void RunMessageLoop();

  void Close(bool force);
  void Minimize();
  void Resize(int width, int height);

  HWND hwnd() const { return hwnd_; }
  wkeWebView view() const { return view_; }
  bool valid() const { return view_ != nullptr; }

  // Pushes an event into the page: window.__blinkDispatch(<event>).
  // Must be called on the UI thread; use TaskQueue if you are not on it.
  void DispatchEvent(const Json& event);

  // When set, a user-initiated close (title bar / Alt+F4) is turned into a
  // "window-closing" event instead of destroying the window, letting the page
  // show its own confirmation. The page then calls win.close with force.
  void set_close_guard(bool guard) { close_guard_ = guard; }

 private:
  BlinkWindow() = default;

  bool LoadMiniblink(const std::wstring& dll_path);
  void BindNativeFunctions();

  // JS-facing bridge. Both are bound as globals and wrapped by the page SDK.
  static jsValue JsInvoke(jsExecState es, void* param);
  static jsValue JsInvokeAsync(jsExecState es, void* param);
  static jsValue JsSetCloseGuard(jsExecState es, void* param);

  static bool OnWindowClosing(wkeWebView view, void* param);
  static void OnWindowDestroy(wkeWebView view, void* param);

  // Diagnostics: a page that fails to run is otherwise a silent blank window.
  static void OnConsole(wkeWebView view, void* param, wkeConsoleLevel level,
                        const wkeString message, const wkeString sourceName,
                        unsigned sourceLine, const wkeString stackTrace);
  static void OnLoadingFinish(wkeWebView view, void* param, const wkeString url,
                              wkeLoadingResult result,
                              const wkeString failedReason);

  wkeWebView view_ = nullptr;
  HWND hwnd_ = nullptr;
  HMODULE miniblink_ = nullptr;
  bool close_guard_ = false;
  bool closing_ = false;
};

}  // namespace bk
