cmake_minimum_required (VERSION 3.20)

project("rcdx_cntnr_demo")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TESTING OFF)

#main pogram
file(GLOB PROJECT_SRC CONFIGURE_DEPENDS "container_src/*")
add_executable(${PROJECT_NAME} ${PROJECT_SRC})
target_include_directories(${PROJECT_NAME} PRIVATE container_src/cgrps container_src/process container_src)

FIND_PACKAGE( Boost 1.78 COMPONENTS program_options REQUIRED )
INCLUDE_DIRECTORIES( ${Boost_INCLUDE_DIR} )
target_link_libraries(
  ${PROJECT_NAME}
  Boost::program_options
)


#binaries for testing
file(GLOB TEST_SRC CONFIGURE_DEPENDS "test_src/*")
foreach( testsourcefile ${TEST_SRC} )
    # Cut off the file extension and directory path
    get_filename_component( testname ${testsourcefile} NAME_WE )
    add_executable( ${testname} ${testsourcefile} )
    target_compile_options(${testname} PUBLIC -O0)
endforeach( testsourcefile ${APP_SOURCES} )
