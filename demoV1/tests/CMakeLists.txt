
cmake_minimum_required (VERSION 3.20)

project("rcdx_cntnr_tests")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#binaries for testing
file(GLOB TEST_SRC CONFIGURE_DEPENDS "test_src/*")
foreach( testsourcefile ${TEST_SRC} )
    # Cut off the file extension and directory path
    get_filename_component( testname ${testsourcefile} NAME_WE )
    add_executable( ${testname} ${testsourcefile} )
    target_compile_options(${testname} PUBLIC -O0)
    target_include_directories(${testname} PRIVATE container_src/cgrps container_src/process container_src)
endforeach( testsourcefile ${APP_SOURCES} )