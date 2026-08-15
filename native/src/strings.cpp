#include "strings.h"

#include <windows.h>

namespace bk {

std::wstring Utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) return std::wstring();
  const int needed = ::MultiByteToWideChar(
      CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (needed <= 0) return std::wstring();
  std::wstring out(static_cast<size_t>(needed), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        out.data(), needed);
  return out;
}

std::string WideToUtf8(const std::wstring& wide) {
  if (wide.empty()) return std::string();
  const int needed = ::WideCharToMultiByte(
      CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
      nullptr, nullptr);
  if (needed <= 0) return std::string();
  std::string out(static_cast<size_t>(needed), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        out.data(), needed, nullptr, nullptr);
  return out;
}

std::string FormatWin32Error(unsigned long code) {
  LPWSTR buffer = nullptr;
  const DWORD len = ::FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

  std::string message;
  if (len && buffer) {
    std::wstring wide(buffer, len);
    while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n')) {
      wide.pop_back();
    }
    message = WideToUtf8(wide);
  }
  if (buffer) ::LocalFree(buffer);

  return std::to_string(code) + (message.empty() ? "" : " (" + message + ")");
}

bool StartsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b) {
  return a.size() == b.size() &&
         ::CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                b.c_str(), static_cast<int>(b.size()),
                                TRUE) == CSTR_EQUAL;
}

}  // namespace bk
