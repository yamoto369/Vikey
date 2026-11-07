add_library(vikey_tsf SHARED
  dllmain.cpp
  registration.cpp
  tip.cpp
  engine_state.cpp
  ../App/Prefs.cpp
  exports.def
)
target_include_directories(vikey_tsf PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../App
  ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine
  ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/platforms
)
target_link_libraries(vikey_tsf PRIVATE vikey_engine uuid ole32 oleaut32 advapi32 shell32)
target_compile_definitions(vikey_tsf PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(vikey_tsf PRIVATE /utf-8 /W4 /EHsc)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64" OR CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
  set(VIKEY_TSF_ARCH arm64)
else()
  set(VIKEY_TSF_ARCH x64)
endif()
set_target_properties(vikey_tsf PROPERTIES
  OUTPUT_NAME "VikeyTsf-${VIKEY_TSF_ARCH}"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/App"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/App"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/App"
  LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/App"
)
