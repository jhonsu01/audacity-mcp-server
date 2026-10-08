# Native parts of audacity-mcp-server, for Windows, macOS and Linux:
#   aumcp-engine            headless audio engine used by the MCP server
#   audacity_mcp_native     library loaded by the Audacity 4 extension (MuseApi.Native)
# Nothing links against libsndfile / mpg123: they are loaded at runtime from Audacity
# (or the system), so the binaries run on any machine with Audacity 4 installed.
#
#   cmake -S native -B native/build-cmake -DCMAKE_BUILD_TYPE=Release
#   cmake --build native/build-cmake --config Release
# macOS universal binaries: -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake_minimum_required(VERSION 3.16)
project(audacity_mcp_native CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
endif()

set(CORE_SOURCES src/audio.cpp src/codecs.cpp src/dsp.cpp src/restore.cpp)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # static CRT
  add_compile_options(/utf-8 /W3)
  add_compile_definitions(UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wno-unused-parameter)
endif()

add_executable(aumcp-engine src/engine_main.cpp ${CORE_SOURCES})
add_library(audacity_mcp_native MODULE src/extension.cpp ${CORE_SOURCES})
set_target_properties(audacity_mcp_native PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
if(APPLE)
  # MuseApi.Native looks for <name>.dylib
  set_target_properties(audacity_mcp_native PROPERTIES SUFFIX ".dylib")
endif()

if(UNIX)
  set_target_properties(aumcp-engine audacity_mcp_native PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(aumcp-engine PRIVATE ${CMAKE_DL_LIBS})
  target_link_libraries(audacity_mcp_native PRIVATE ${CMAKE_DL_LIBS})
  if(NOT APPLE)
    # Portable Linux binaries: no dependency on the build machine's libstdc++ version
    target_link_options(aumcp-engine PRIVATE -static-libstdc++ -static-libgcc)
    target_link_options(audacity_mcp_native PRIVATE -static-libstdc++ -static-libgcc)
  endif()
endif()

# Smoke test of the extension library (loads it like Audacity does): ctest --test-dir <build>
option(AUMCP_TESTS "Build native tests" ON)
if(AUMCP_TESTS)
  enable_testing()
  add_executable(ext_smoke tests/ext_smoke.cpp)
  if(UNIX)
    target_link_libraries(ext_smoke PRIVATE ${CMAKE_DL_LIBS})
  endif()
  add_test(NAME ext_smoke COMMAND ext_smoke $<TARGET_FILE:audacity_mcp_native> ${CMAKE_CURRENT_BINARY_DIR})
endif()
