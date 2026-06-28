cmake_minimum_required (VERSION 3.20)

project("rcdx_cntnr")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Emit compile_commands.json for clangd / clang-tidy.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TESTING OFF)
option(ENABLE_SYSTEMD_SERVICE "Enable the systemd service during installation" OFF)
include(FetchContent)

FIND_PACKAGE( Boost 1.75 COMPONENTS program_options REQUIRED )

#Fetch yaml-cpp yaml parsing library
include(FetchContent)
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG master
)
FetchContent_MakeAvailable(yaml-cpp)

add_subdirectory(tests)
add_subdirectory(src)

# Installation configuration
include(GNUInstallDirs)

# Install the main executable from src directory
install(TARGETS isolator
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})