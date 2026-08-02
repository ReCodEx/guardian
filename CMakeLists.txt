cmake_minimum_required (VERSION 3.20)

project("recodex-guardian" VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Compiler gate. `std::format` (src/logs.hpp, src/terminate.hpp) is the first
# thing an under-spec toolchain breaks on, and without this it breaks deep in
# the build rather than here — on el9 the system GCC 11.5 configures happily
# and then dies on `#include <format>`. Probe the feature itself rather than a
# version or a platform, so the gate stays true across compilers.
# ---------------------------------------------------------------------------
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX23_STANDARD_COMPILE_OPTION}")
check_cxx_source_compiles("
  #include <format>
  #include <string>
  int main() { return std::format(\"{}\", 42) == \"42\" ? 0 : 1; }
" HAVE_STD_FORMAT)
unset(CMAKE_REQUIRED_FLAGS)

if(NOT HAVE_STD_FORMAT)
  set(_hint "")
  # A toolset installed but not entered is the usual cause on el9, so name the
  # exact command instead of leaving the reader to find it in the README.
  file(GLOB _toolsets /opt/rh/gcc-toolset-*/enable)
  if(_toolsets)
    list(SORT _toolsets)
    list(GET _toolsets -1 _newest)
    get_filename_component(_scl "${_newest}" DIRECTORY)
    get_filename_component(_scl "${_scl}" NAME)
    set(_hint "\n  This host has ${_scl} installed but not active. Enter it first:"
              "\n      scl enable ${_scl} -- bash"
              "\n  Then delete the build tree and configure again — CMake caches"
              "\n  the compiler path, so entering the toolset alone is not enough.")
  endif()
  message(FATAL_ERROR
    "This project needs a C++23 <format> implementation, which "
    "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} does not provide."
    "\n  Found:  ${CMAKE_CXX_COMPILER}"
    "\n  Needs:  GCC 13+ or Clang 17+ (with a matching libstdc++/libc++)"
    ${_hint})
endif()

# Build every object position-independent so the statically linked yaml-cpp is
# PIC-compatible with the -pie recodex-guardian executable (src/CMakeLists.txt).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Emit compile_commands.json for clangd / clang-tidy.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TESTING OFF)
# The `isolate` alias — symlink, man redirect and `Provides: isolate` (ADR 0002).
# Off by default: claiming /usr/bin/isolate displaces upstream Isolate, so it is
# opt-in.
option(ISOLATE_ALIAS "Install the Isolate-compatible \
`isolate` alias (symlink, man redirect, RPM Provides)" OFF)
include(FetchContent)

#Fetch yaml-cpp yaml parsing library
include(FetchContent)
# Pinned to a fixed commit (not the moving master branch) for reproducible
# builds (ADR 0004); statically linked into the self-contained setuid binary.
# A hermetic/offline build can set FETCHCONTENT_SOURCE_DIR_YAML-CPP
# to a pre-fetched tree.
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
# We static-link yaml-cpp into the binary; never ship its headers/lib in the RPM.
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG 356643ba267039367d87fa70ce3d8f87144f55e9  # post-0.8.0, cmake 3.15..4.3
)
FetchContent_MakeAvailable(yaml-cpp)

# Unit-test tier (host-side, no root): GoogleTest, fetched only when TESTING is
# ON so normal/production builds stay gtest-free. Mirrors the yaml-cpp block.
if(TESTING)
  enable_testing()
  # Never install GoogleTest artifacts (it is test-tier only, never in the RPM).
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
  )
  FetchContent_MakeAvailable(googletest)
endif()

add_subdirectory(tests)
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Installation (ADR 0002, ADR 0004). Configure with -DCMAKE_INSTALL_PREFIX=/usr
# for the RPM: GNUInstallDirs then resolves BINDIR to /usr/bin and MANDIR to
# /usr/share/man, matching the RPM-owned /usr prefix.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

# The setuid-root binary (mode 4755 — setuid, not setgid; ADR 0002).
install(TARGETS recodex-guardian
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PERMISSIONS
          SETUID
          OWNER_READ OWNER_WRITE OWNER_EXECUTE
          GROUP_READ GROUP_EXECUTE
          WORLD_READ WORLD_EXECUTE)

# The `isolate` alias: a relative symlink so a PATH-resolved `isolate` runs us
# unchanged (ADR 0002). CMAKE_INSTALL_PREFIX and DESTDIR are deferred to
# install time (escaped) so the link lands next to the binary regardless of the
# prefix; only the relative BINDIR is baked at configure.
if(ISOLATE_ALIAS)
  install(CODE "file(CREATE_LINK recodex-guardian \
\"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/isolate\" SYMBOLIC)")
endif()

# Persistent box tree, owned by the package. Absolute (not under the /usr
# prefix); also created lazily at --run (ADR 0007), this just tracks + cleans it.
install(DIRECTORY DESTINATION /var/lib/recodex-guardian/boxes)

# Man page, plus the `man isolate` .so redirect when the alias is installed.
install(FILES ${CMAKE_SOURCE_DIR}/docs/man/recodex-guardian.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
if(ISOLATE_ALIAS)
  install(FILES ${CMAKE_SOURCE_DIR}/docs/man/isolate.1
          DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()

# ---------------------------------------------------------------------------
# CPack / RPM (el9, CPACK generator "RPM"; ADR 0004).
# ---------------------------------------------------------------------------
set(CPACK_GENERATOR "RPM")
set(CPACK_PACKAGE_NAME "recodex-guardian")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Simon Kurz <simon.kurz@meteopress.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "ReCodEx Guardian — an Isolate-compatible sandbox for untrusted code")
# TODO: confirm the project license before release (placeholder).
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
# Satisfy the Worker's `Requires: isolate`; coexistence with the upstream
# `isolate` package is prevented by the shared /usr/bin/isolate file (ADR 0002).
# Both effects belong to the alias, so an alias-less package claims neither: it
# cannot satisfy the Worker, but it does install beside upstream Isolate.
if(ISOLATE_ALIAS)
  set(CPACK_RPM_PACKAGE_PROVIDES "isolate")
endif()
# CPack relocates the install tree under its *own* packaging prefix, which is
# independent of CMAKE_INSTALL_PREFIX. Pin it to /usr (the el9 target) and derive
# the filelist below from the same variable: deriving from CMAKE_INSTALL_FULL_BINDIR
# instead makes the spec name a path the package does not contain whenever the build
# tree was configured with another prefix, and rpmbuild then dies with
# "File not found: .../usr/local/bin/recodex-guardian".
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

# Force the packaged mode/ownership regardless of the build user's umask, and
# own the box tree as a directory.
set(CPACK_RPM_USER_FILELIST
    "%attr(4755,root,root) ${CPACK_PACKAGING_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/recodex-guardian"
    "%dir %attr(0755,root,root) /var/lib/recodex-guardian"
    "%dir %attr(0755,root,root) /var/lib/recodex-guardian/boxes")
# No -debuginfo subpackage for now: build the RPM as Release (no debug symbols
# to split out). A proper -debuginfo subpackage can be added later.
# Standard RPM naming (recodex-guardian-0.1.0-1.el9.x86_64.rpm) instead of CPack's
# default "<name>-<version>-Linux.rpm".
set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
set(CPACK_RPM_PACKAGE_RELEASE "1")
set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)
include(CPack)
