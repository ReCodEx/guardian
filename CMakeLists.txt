cmake_minimum_required (VERSION 3.20)

project("rcdx_cntnr" VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build every object position-independent so the statically linked yaml-cpp is
# PIC-compatible with the -pie isolator executable (src/CMakeLists.txt).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Emit compile_commands.json for clangd / clang-tidy.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TESTING OFF)
include(FetchContent)

#Fetch yaml-cpp yaml parsing library
include(FetchContent)
# Pinned to a fixed commit (not the moving master branch) for reproducible
# builds (ADR 0004); statically linked into the self-contained setuid binary.
# We pin a post-0.8.0 commit rather than the 0.8.0 tag because 0.8.0 declares
# `cmake_minimum_required(VERSION 3.4)`, which CMake 4.x rejects; this commit
# declares `3.15..4.3`, working on both the el9 target (CMake 3.20) and modern
# dev hosts. A hermetic/offline build can set FETCHCONTENT_SOURCE_DIR_YAML-CPP
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
install(TARGETS isolator
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
install(CODE "file(CREATE_LINK isolator \
\"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/isolate\" SYMBOLIC)")

# Persistent box tree, owned by the package. Absolute (not under the /usr
# prefix); also created lazily at --run (ADR 0007), this just tracks + cleans it.
install(DIRECTORY DESTINATION /var/lib/isolator_boxes)

# Man page + the `man isolate` .so redirect alias.
install(FILES ${CMAKE_SOURCE_DIR}/docs/man/isolator.1
              ${CMAKE_SOURCE_DIR}/docs/man/isolate.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

# ---------------------------------------------------------------------------
# CPack / RPM (el9, CPACK generator "RPM"; ADR 0004).
# ---------------------------------------------------------------------------
set(CPACK_GENERATOR "RPM")
set(CPACK_PACKAGE_NAME "isolator")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Simon Kurz <simon.kurz@meteopress.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "ReCodEx isolator — an Isolate-compatible sandbox for untrusted code")
# TODO: confirm the project license before release (placeholder).
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
# Satisfy the Worker's `Requires: isolate`; coexistence with the upstream
# `isolate` package is prevented by the shared /usr/bin/isolate file (ADR 0002).
set(CPACK_RPM_PACKAGE_PROVIDES "isolate")
# Force the packaged mode/ownership regardless of the build user's umask, and
# own the box tree as a directory.
set(CPACK_RPM_USER_FILELIST
    "%attr(4755,root,root) ${CMAKE_INSTALL_FULL_BINDIR}/isolator"
    "%dir %attr(0755,root,root) /var/lib/isolator_boxes")
# No -debuginfo subpackage for now: build the RPM as Release (no debug symbols
# to split out). A proper -debuginfo subpackage can be added later.
include(CPack)