// A minimal application, purely so the example installs something real:
// shortcuts point at a genuine exe and "launch after install" actually runs.
#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  wchar_t path[MAX_PATH] = {};
  ::GetModuleFileNameW(nullptr, path, MAX_PATH);

  wchar_t message[MAX_PATH + 128] = {};
  ::wsprintfW(message,
              L"Demo App is running.\n\nInstalled at:\n%s",
              path);

  ::MessageBoxW(nullptr, message, L"Demo App", MB_ICONINFORMATION | MB_OK);
  return 0;
}
