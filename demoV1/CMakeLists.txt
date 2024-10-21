cmake_minimum_required (VERSION 3.20)

project("rcdx_cntnr")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TESTING OFF)
FIND_PACKAGE( Boost 1.78 COMPONENTS program_options REQUIRED )

file(GLOB spdlog_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/spdlog/build)
FIND_PACKAGE(spdlog REQUIRED)

add_subdirectory(tests)
add_subdirectory(src)
