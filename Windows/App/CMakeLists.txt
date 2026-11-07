# Vikey.exe: tray app + hooks. Built only on Windows (MSVC).
# One version for every platform: project.yml's MARKETING_VERSION, unless -DVIKEY_VERSION overrides it.
unset(VIKEY_VERSION CACHE)   # never a stale cached value
if(NOT VIKEY_VERSION)
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../project.yml" _mv REGEX "MARKETING_VERSION: \"")
  string(REGEX REPLACE ".*MARKETING_VERSION: \"([^\"]+)\".*" "\\1" VIKEY_VERSION "${_mv}")
endif()
if(NOT VIKEY_BUILD)
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../project.yml" _bv REGEX "CURRENT_PROJECT_VERSION: \"")
  string(REGEX REPLACE ".*CURRENT_PROJECT_VERSION: \"([^\"]+)\".*" "\\1" VIKEY_BUILD "${_bv}")
endif()
message(STATUS "Vikey version ${VIKEY_VERSION} (${VIKEY_BUILD})")
add_executable(vikey WIN32 main.cpp Core.cpp Prefs.cpp Site.cpp Settings.cpp CrashReporter.cpp TsfController.cpp Vikey.rc)
set_source_files_properties(third_party/tweetnacl.c PROPERTIES COMPILE_FLAGS "/w")
set_target_properties(vikey PROPERTIES OUTPUT_NAME Vikey)
target_include_directories(vikey PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vikey PRIVATE UNICODE _UNICODE VIKEY_VERSION="${VIKEY_VERSION}" VIKEY_BUILD="${VIKEY_BUILD}")
target_link_libraries(vikey PRIVATE vikey_engine user32 shell32 gdi32 advapi32 imm32 psapi ole32 oleaut32 comctl32 gdiplus winmm dwmapi normaliz uuid)
target_link_options(vikey PRIVATE /MANIFEST:NO)
# Live test runner: a window with an edit control, types through SendInput while Vikey runs, reads the text back.
add_executable(vikey_livetest ../Tests/livetest.cpp Prefs.cpp)
target_include_directories(vikey_livetest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vikey_livetest PRIVATE vikey_engine user32 advapi32 shell32)
target_compile_definitions(vikey_livetest PRIVATE UNICODE _UNICODE)
# Pure unit test for Windows virtual-key -> shared-engine key mapping; no desktop or hooks required.
add_executable(vikey_keymap_test ../Tests/keymap_test.cpp)
target_include_directories(vikey_keymap_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vikey_keymap_test PRIVATE vikey_engine)
target_compile_definitions(vikey_keymap_test PRIVATE UNICODE _UNICODE)

# Pure input policy tests for hotkey matching and app blacklist normalization.
add_executable(vikey_input_policy_test ../Tests/input_policy_test.cpp)
target_include_directories(vikey_input_policy_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vikey_input_policy_test PRIVATE vikey_engine)
target_compile_definitions(vikey_input_policy_test PRIVATE UNICODE _UNICODE)

# Pure TSF context policy tests; no COM registration or desktop required.
add_executable(vikey_tsf_policy_test ../Tests/tsf_policy_test.cpp)
target_include_directories(vikey_tsf_policy_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vikey_tsf_policy_test PRIVATE UNICODE _UNICODE)

# Thread-aware adapter from TSF key events to the shared typing engine.
add_library(vikey_tsf_engine_adapter STATIC TsfEngineAdapter.cpp TsfEngineRuntime.cpp)
target_include_directories(vikey_tsf_engine_adapter PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine
  ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/platforms)
target_link_libraries(vikey_tsf_engine_adapter PUBLIC vikey_engine)

# In-process TSF COM DLL. Text mutations are performed only in synchronous
# TSF read/write edit sessions; unsafe contexts are passed through.
add_library(vikey_tsf MODULE TSFTip.cpp)
target_link_libraries(vikey_tsf PRIVATE vikey_tsf_engine_adapter)
target_include_directories(vikey_tsf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vikey_tsf PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(vikey_tsf PRIVATE ole32 oleaut32 advapi32 uuid)
set_target_properties(vikey_tsf PROPERTIES PREFIX "" OUTPUT_NAME "VikeyTsf")

# Export the standard COM DLL entry points with their undecorated names.
target_link_options(vikey_tsf PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/TSFTip.def")

# COM ABI/lifetime smoke test loads the built TIP DLL without machine registration.
add_executable(vikey_tsf_com_test ../Tests/tsf_com_test.cpp)
target_compile_definitions(vikey_tsf_com_test PRIVATE UNICODE _UNICODE)
target_link_libraries(vikey_tsf_com_test PRIVATE ole32 uuid)
add_dependencies(vikey_tsf_com_test vikey_tsf)

# Pure tests for Telex output plans and per-thread engine state isolation.
add_executable(vikey_tsf_engine_test ../Tests/tsf_engine_test.cpp)
target_include_directories(vikey_tsf_engine_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vikey_tsf_engine_test PRIVATE vikey_tsf_engine_adapter)

# Bundle the matching in-process TSF DLL next to the portable tray executable.
if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64" OR CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
  set(VIKEY_TSF_RUNTIME_NAME "VikeyTsf-arm64.dll")
else()
  set(VIKEY_TSF_RUNTIME_NAME "VikeyTsf-x64.dll")
endif()
add_dependencies(vikey vikey_tsf)
add_custom_command(TARGET vikey POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:vikey_tsf>
    "$<TARGET_FILE_DIR:vikey>/${VIKEY_TSF_RUNTIME_NAME}"
  VERBATIM)

# Exercise the real tray-side TSF registration and session activation controller.
add_executable(vikey_tsf_controller_test ../Tests/tsf_controller_test.cpp TsfController.cpp)
target_include_directories(vikey_tsf_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vikey_tsf_controller_test PRIVATE UNICODE _UNICODE)
target_link_libraries(vikey_tsf_controller_test PRIVATE ole32 oleaut32 advapi32 user32 shell32 uuid)
add_dependencies(vikey_tsf_controller_test vikey)

# End-to-end typing through the real TSF stack: registers and activates the TIP, then drives
# ITfKeystrokeMgr against a mock text store and checks the Vietnamese text that comes out.
add_executable(vikey_tsf_typing_test ../Tests/tsf_typing_test.cpp TsfController.cpp)
target_include_directories(vikey_tsf_typing_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vikey_tsf_typing_test PRIVATE UNICODE _UNICODE)
target_link_libraries(vikey_tsf_typing_test PRIVATE ole32 oleaut32 advapi32 user32 shell32 uuid)
add_dependencies(vikey_tsf_typing_test vikey)
