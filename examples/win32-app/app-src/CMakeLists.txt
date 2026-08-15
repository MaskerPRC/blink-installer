cmake_minimum_required(VERSION 3.20)
project(DemoApp LANGUAGES CXX)

# Built into ../app/, which is what the installer config points `source` at.
add_executable(DemoApp WIN32 main.cpp)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

target_compile_definitions(DemoApp PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_link_libraries(DemoApp PRIVATE user32)

set_target_properties(DemoApp PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_CURRENT_SOURCE_DIR}/../app"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_CURRENT_SOURCE_DIR}/../app"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../app"
)
