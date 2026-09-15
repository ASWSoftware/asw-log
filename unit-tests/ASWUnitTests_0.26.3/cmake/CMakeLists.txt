cmake_minimum_required(VERSION 3.20)

project(ASWUnitTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ASWUnitTests
    ../src/ASWUnitTests_Exception.cpp
    ../src/ASWUnitTests_Handler.cpp
    ../src/ASWUnitTests_TestBase.cpp
    ../src/ASWUnitTests_Version.h
    ../src/main.cpp
    ../tests/Test_ASWTools_Random.cpp
    ../tests/Test_ASWTools_String.cpp
    ../toTest/ASWTools_Common.cpp
    ../toTest/ASWTools_Random.cpp
    ../toTest/ASWTools_String.cpp
)

target_include_directories(ASWUnitTests PRIVATE
    ../src
    ../tests
    ../toTest
)

if(MSVC)
    target_compile_options(ASWUnitTests PRIVATE /W4 /Zc:__cplusplus)
else()
    target_compile_options(ASWUnitTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(ASWUnitTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
