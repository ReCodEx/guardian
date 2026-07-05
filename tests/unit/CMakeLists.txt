cmake_minimum_required(VERSION 3.20)
project(isolator_unit_tests)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# One executable per test file. The src/ headers are header-only, so each test
# just includes what it needs; but file_utils in utils.hpp defines non-inline
# free functions, so two TUs including config.hpp in ONE binary would collide at
# link time. Keeping each test file as its own single-TU executable avoids that
# (and matches the repo convention of one binary per probe / per worker suite).
set(UNIT_TESTS
  cli_options_test
  dir_rule_test
  env_rule_test
  config_test
  lock_test
  meta_file_test
)

foreach(t ${UNIT_TESTS})
  add_executable(${t} ${t}.cpp)
  target_compile_options(${t} PRIVATE -Wall)
  target_include_directories(${t} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(${t} PRIVATE
    GTest::gtest GTest::gtest_main
    yaml-cpp::yaml-cpp
  )
  add_test(NAME ${t} COMMAND ${t})
endforeach()
