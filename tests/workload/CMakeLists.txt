cmake_minimum_required(VERSION 3.20)
project(resource_limit_tests)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directory for all targets
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)

# Build all test source files
file(GLOB ISOLATION CONFIGURE_DEPENDS "isolation_tests/*.cpp")
file(GLOB LIMITS CONFIGURE_DEPENDS "limits_tests/*.cpp")
foreach(testsourcefile ${ISOLATION} ${LIMITS})
    get_filename_component(testname ${testsourcefile} NAME_WE)
    add_executable(${testname} ${testsourcefile})
    target_compile_options(${testname} PUBLIC -O0)
    target_include_directories(${testname} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
    
    # Ensure each target goes to the bin directory
    set_target_properties(${testname} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    )
endforeach()