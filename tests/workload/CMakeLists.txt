cmake_minimum_required(VERSION 3.20)
project(workload_probes)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Workloads: small in-box payload programs the workload tests run inside the
# sandbox to verify a resource limit or isolation boundary actually bites. They
# are compiled to build/ (mounted into the box as /build by the yml configs);
# they are NOT host-side unit tests (those live in tests/unit/).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)

file(GLOB WORKLOADS CONFIGURE_DEPENDS "workloads/*.cpp")
foreach(src ${WORKLOADS})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_compile_options(${name} PUBLIC -O0)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    )
endforeach()
