// UTF-8 <-> UTF-16 conversion at the Win32 boundary.
//
// Everything above this layer (JSON, ability arguments, the config store) is
// UTF-8 std::string. Everything below it (Win32, NSIS, miniblink's wide APIs)
// is UTF-16 std::wstring. Convert once, at the edge.
#pragma once

#include <string>

namespace bk {

std::wstring Utf8ToWide(const std::string& utf8);
std::string WideToUtf8(const std::wstring& wide);

// Formats a Win32 error code (from GetLastError) as "code (message)".
std::string FormatWin32Error(unsigned long code);

bool StartsWith(const std::string& s, const std::string& prefix);
bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b);

}  // namespace bk
