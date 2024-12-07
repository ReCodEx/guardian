cmake_minimum_required (VERSION 3.20)

project("container")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TESTING OFF)

#main pogram
add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE )

#BOOST package for parsing cmdline options
FIND_PACKAGE( Boost 1.78 COMPONENTS program_options REQUIRED )
INCLUDE_DIRECTORIES( ${Boost_INCLUDE_DIR})

#spdlog logging library
file(GLOB spdlog_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../libs/spdlog/build)
FIND_PACKAGE(spdlog REQUIRED)

#yaml-cpp yaml parsing library
include(FetchContent)
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG master # Can be a tag (yaml-cpp-x.x.x), a commit hash, or a branch name (master)
)
FetchContent_MakeAvailable(yaml-cpp)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  Boost::program_options
  spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>
  yaml-cpp::yaml-cpp
)