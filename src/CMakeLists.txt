cmake_minimum_required (VERSION 3.20)

project("isolator")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TESTING OFF)

#main pogram
add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE )

# Compiler hardening for the setuid binary — the full upstream-Isolate set
# (ADR 0002, ADR 0004). _FORTIFY_SOURCE is a no-op without optimization and
# warns under -O0, so it is gated to the optimized configs (the RPM builds
# RelWithDebInfo); every other flag is safe at any level. PIE comes from
# CMAKE_POSITION_INDEPENDENT_CODE (set at the top level, so the statically
# linked yaml-cpp is PIC too) plus the explicit -pie below.
target_compile_options(${PROJECT_NAME} PRIVATE
  -Wall
  -fstack-protector-strong
  -fstack-clash-protection
  -fPIE
  $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-D_FORTIFY_SOURCE=3>
)

target_link_options(${PROJECT_NAME} PRIVATE
  -pie
  # Self-contained binary: no dependency on the gcc-toolset SCL runtime at
  # execution time (ADR 0004).
  -static-libstdc++
  -static-libgcc
  -Wl,-z,nodlopen
  -Wl,-z,noexecstack
  -Wl,-z,relro
  -Wl,-z,now
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  yaml-cpp::yaml-cpp
  cap  # libcap: CAP_SYS_ADMIN for the drop-priv bind mount (credentials.hpp)
)
