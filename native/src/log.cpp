#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

#include "strings.h"

namespace bk {
namespace {

std::mutex g_mutex;
std::wstring g_log_path;
bool g_initialized = false;

const char* LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO ";
    case LogLevel::Warn: return "WARN ";
    case LogLevel::Error: return "ERROR";
  }
  return "?????";
}

// Strips the directory portion so log lines stay readable.
const char* BaseName(const char* path) {
  const char* base = path;
  for (const char* p = path; *p; ++p) {
    if (*p == '\\' || *p == '/') base = p + 1;
  }
  return base;
}

}  // namespace

void LogInit() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_initialized) return;
  g_initialized = true;

  wchar_t buffer[MAX_PATH] = {};
  const DWORD len =
      ::GetEnvironmentVariableW(L"BLINKKIT_LOG", buffer, MAX_PATH);
  if (len > 0 && len < MAX_PATH) g_log_path.assign(buffer, len);
}

std::string LogFormat(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  va_list probe;
  va_copy(probe, args);
  const int needed = std::vsnprintf(nullptr, 0, fmt, probe);
  va_end(probe);

  std::string out;
  if (needed > 0) {
    out.resize(static_cast<size_t>(needed));
    std::vsnprintf(out.data(), static_cast<size_t>(needed) + 1, fmt, args);
  }
  va_end(args);
  return out;
}

void LogWrite(LogLevel level, const char* file, int line,
              const std::string& message) {
  SYSTEMTIME st;
  ::GetLocalTime(&st);

  char header[128];
  std::snprintf(header, sizeof(header), "[blinkkit %02d:%02d:%02d.%03d %s %s:%d] ",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                LevelName(level), BaseName(file), line);

  const std::string line_text = std::string(header) + message + "\n";
  ::OutputDebugStringW(Utf8ToWide(line_text).c_str());

  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_log_path.empty()) return;

  HANDLE handle = ::CreateFileW(g_log_path.c_str(), FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return;

  // Lead a fresh file with a UTF-8 BOM. The log carries Win32 error strings in
  // the user's language, and without the BOM every editor and PowerShell on a
  // non-English system reads it as the local ANSI code page and mangles them.
  if (::GetFileSize(handle, nullptr) == 0) {
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    DWORD bom_written = 0;
    ::WriteFile(handle, bom, sizeof(bom), &bom_written, nullptr);
  }

  DWORD written = 0;
  ::WriteFile(handle, line_text.data(), static_cast<DWORD>(line_text.size()),
              &written, nullptr);
  ::CloseHandle(handle);
}

}  // namespace bk
