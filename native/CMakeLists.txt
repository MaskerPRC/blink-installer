cmake_minimum_required(VERSION 3.20)
project(blinkkit LANGUAGES CXX)

# ---------------------------------------------------------------------------
# blinkkit.dll — the NSIS plugin that hosts the miniblink-rendered installer UI.
#
# Must be built as x86 (Win32): the NSIS installer stub is 32-bit and the
# miniblink binary we ship (node.dll) is 32-bit only. Configure with:
#     cmake -S native -B native/build -A Win32
# ---------------------------------------------------------------------------

if(NOT WIN32)
  message(FATAL_ERROR "blinkkit is a Windows-only target.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR
    "blinkkit must be built for x86. Re-run CMake with:  -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT so the plugin DLL has no VC++ redistributable dependency —
# an installer cannot assume the target machine has one.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(BLINKKIT_SOURCES
  src/dllmain.cpp
  src/json.cpp
  src/strings.cpp
  src/log.cpp
  src/task_queue.cpp
  src/config_store.cpp
  src/dpi.cpp
  src/ability.cpp
  src/blink_window.cpp
  src/taskbar.cpp

  src/nsis/plugin_api.cpp
  src/nsis/nsis_bridge.cpp
  src/nsis/progress.cpp
  src/nsis/exports.cpp

  src/abilities/config.cpp
  src/abilities/fs.cpp
  src/abilities/nsis.cpp
  src/abilities/proc.cpp
  src/abilities/reg.cpp
  src/abilities/shell.cpp
  src/abilities/sys.cpp
  src/abilities/ui.cpp
  src/abilities/win.cpp
)

add_library(blinkkit SHARED ${BLINKKIT_SOURCES})

target_include_directories(blinkkit PRIVATE
  src
  third_party/miniblink
)

target_compile_definitions(blinkkit PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(blinkkit PRIVATE
  user32 gdi32 shell32 ole32 shlwapi advapi32 comctl32
)

set_target_properties(blinkkit PROPERTIES
  OUTPUT_NAME "blinkkit"
  RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_CURRENT_SOURCE_DIR}/out"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_CURRENT_SOURCE_DIR}/out"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/out"
)

if(MSVC)
  target_compile_options(blinkkit PRIVATE /W4 /permissive- /utf-8)
  # No manifest: NSIS plugins are loaded by the stub, which owns the manifest.
  target_link_options(blinkkit PRIVATE /MANIFEST:NO)
endif()
