# Vikey.exe: tray/settings control process. Keyboard input is handled exclusively by the TSF TIP.
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
add_executable(vikey WIN32 main.cpp Core.cpp Prefs.cpp Site.cpp Settings.cpp CrashReporter.cpp Vikey.rc)
set_source_files_properties(third_party/tweetnacl.c PROPERTIES COMPILE_FLAGS "/w")
set_target_properties(vikey PROPERTIES OUTPUT_NAME Vikey)
target_include_directories(vikey PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vikey PRIVATE UNICODE _UNICODE VIKEY_VERSION="${VIKEY_VERSION}" VIKEY_BUILD="${VIKEY_BUILD}")
target_link_libraries(vikey PRIVATE vikey_engine user32 shell32 gdi32 advapi32 imm32 psapi ole32 oleaut32 comctl32 gdiplus winmm dwmapi normaliz)
target_link_options(vikey PRIVATE /MANIFEST:NO)
