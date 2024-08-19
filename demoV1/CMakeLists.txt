cmake_minimum_required (VERSION 3.20)

project("rcdx_cntnr_demo")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TESTING OFF)

#main pogram
file(GLOB PROJECT_SRC CONFIGURE_DEPENDS "container_src/*")
add_executable(${PROJECT_NAME} ${PROJECT_SRC})


#binaries for testing
file(GLOB TEST_SRC CONFIGURE_DEPENDS "test_src/*")
foreach( testsourcefile ${TEST_SRC} )
    # Cut off the file extension and directory path
    get_filename_component( testname ${testsourcefile} NAME_WE )
    add_executable( ${testname} ${testsourcefile} )
endforeach( testsourcefile ${APP_SOURCES} )
