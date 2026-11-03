cmake_minimum_required(VERSION 3.20)
project(ModeratedLMS VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL 1.1.1 REQUIRED COMPONENTS Crypto)

add_library(sqlite3 STATIC sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sqlite)
set_target_properties(sqlite3 PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

add_library(lms_core STATIC src/lms.cpp)
target_include_directories(lms_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/sqlite)
target_link_libraries(lms_core PUBLIC sqlite3 OpenSSL::Crypto)

if(MSVC)
    target_compile_options(lms_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(lms_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(ModeratedLMS src/main.cpp)
target_link_libraries(ModeratedLMS PRIVATE lms_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(LMS_Tests tests/test_lms.cpp)
    target_link_libraries(LMS_Tests PRIVATE lms_core)
    add_test(NAME LMS_Tests COMMAND LMS_Tests)
endif()
