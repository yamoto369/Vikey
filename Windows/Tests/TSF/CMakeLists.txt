add_executable(vikey_tsf_driver_test driver_contract_test.cpp)
target_include_directories(vikey_tsf_driver_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../Engine
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../Engine/platforms
  ${CMAKE_CURRENT_SOURCE_DIR}/../../App
)
target_link_libraries(vikey_tsf_driver_test PRIVATE vikey_engine)
add_test(NAME tsf_driver_contract COMMAND vikey_tsf_driver_test)

add_executable(vikey_tsf_window_test window_test.cpp)
target_link_libraries(vikey_tsf_window_test PRIVATE ole32 user32)
add_test(NAME tsf_window_smoke COMMAND vikey_tsf_window_test)
set_tests_properties(tsf_window_smoke PROPERTIES SKIP_RETURN_CODE 77)

add_executable(vikey_tsf_textstore_test textstore_host_test.cpp)
target_link_libraries(vikey_tsf_textstore_test PRIVATE uuid ole32 oleaut32 user32 advapi32)
add_dependencies(vikey_tsf_textstore_test vikey_tsf)
add_test(NAME tsf_textstore_end_to_end COMMAND vikey_tsf_textstore_test)
