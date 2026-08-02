cmake_minimum_required (VERSION 3.20)

project("guardian_tests")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Both tiers are TESTING-only: a production/RPM build must never compile test
# code, and the workloads are what the workload tier expects -DTESTING=ON to
# build (tests/workload/conftest.py skips when they're absent).
if(TESTING)
  add_subdirectory(workload)
  # Host-side unit tier — needs GoogleTest, fetched at the top level under the
  # same guard.
  add_subdirectory(unit)
endif()