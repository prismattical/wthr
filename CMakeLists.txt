cmake_minimum_required(VERSION 3.21.0)
project(wthr VERSION 0.1.0 LANGUAGES C)

include(CTest)
enable_testing()

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(json-c CONFIG REQUIRED)
find_package(Check)

set(CMAKE_C_STANDARD 17)

set(PROJECT_FILES requests.h requests.c)

add_executable(wthr main.c ${PROJECT_FILES})
add_executable(wthr_test test.c ${PROJECT_FILES})

include_directories(${CURL_INCLUDE_DIR})
target_link_libraries(wthr Threads::Threads)
target_link_libraries(wthr ${CURL_LIBRARIES})
target_link_libraries(wthr json-c::json-c)

target_link_libraries(wthr_test ${CURL_LIBRARIES})
target_link_libraries(wthr_test json-c::json-c)
target_link_libraries(wthr_test check)

add_test(wthr_test wthr_test)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
