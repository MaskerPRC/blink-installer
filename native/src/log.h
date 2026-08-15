// Lightweight logging.
//
// The code this replaces debugged with MessageBox popups and, when a config
// file was missing, called exit(-1) from library code. Neither is acceptable in
// a DLL loaded by someone else's installer stub: log and degrade instead.
//
// Output goes to OutputDebugString (visible in DebugView / the VS debugger) and,
// when BLINKKIT_LOG is set in the environment, appended to that file path.
#pragma once

#include <string>

namespace bk {

enum class LogLevel { Debug, Info, Warn, Error };

void LogInit();
void LogWrite(LogLevel level, const char* file, int line, const std::string& message);

#define BK_LOG(level, msg) \
  ::bk::LogWrite(::bk::LogLevel::level, __FILE__, __LINE__, (msg))

#define BK_LOGF(level, ...) \
  ::bk::LogWrite(::bk::LogLevel::level, __FILE__, __LINE__, ::bk::LogFormat(__VA_ARGS__))

std::string LogFormat(const char* fmt, ...);

}  // namespace bk
