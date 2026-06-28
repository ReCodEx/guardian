cmake_minimum_required (VERSION 3.20)

project("rcdx_isolator")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(test_suite)

# Host-side unit tier — only when TESTING is ON (needs GoogleTest, fetched at the
# top level under the same guard).
if(TESTING)
  add_subdirectory(unit)
endif()