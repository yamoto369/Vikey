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
