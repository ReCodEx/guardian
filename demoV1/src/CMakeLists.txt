cmake_minimum_required (VERSION 3.20)

project("container")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TESTING OFF)

#main pogram
add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE )

FIND_PACKAGE( Boost 1.78 COMPONENTS program_options REQUIRED )
INCLUDE_DIRECTORIES( ${Boost_INCLUDE_DIR})

file(GLOB spdlog_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../libs/spdlog/build)
FIND_PACKAGE(spdlog REQUIRED)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  Boost::program_options
  spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>
)