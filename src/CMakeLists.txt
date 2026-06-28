cmake_minimum_required (VERSION 3.20)

project("isolator")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TESTING OFF)

#main pogram
add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE )
target_compile_options(${PROJECT_NAME} PRIVATE -Wall)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  yaml-cpp::yaml-cpp
)
