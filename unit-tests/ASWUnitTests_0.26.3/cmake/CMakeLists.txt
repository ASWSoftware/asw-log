cmake_minimum_required(VERSION 3.20)

project(ASWUnitTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ASWUnitTests
    ../../../ASWLog/ASWLog_Base.cpp
    ../../../ASWLog/ASWLog_Config.cpp
    ../../../ASWLog/ASWLog_ConsoleLog.cpp
    ../../../ASWLog/ASWLog_FileLog.cpp
    ../../../ASWLog/ASWLog_Interface.cpp
    ../../../ASWLog/ASWLog_MultiLog.cpp
    ../../../ASWLog/ASWLog_Types.cpp
    ../../../ASWLog/ASWLog_Utils.cpp
    ../src/ASWUnitTests_Exception.cpp
    ../src/ASWUnitTests_Handler.cpp
    ../src/ASWUnitTests_TestBase.cpp
    ../src/ASWUnitTests_Version.h
    ../src/main.cpp
    ../tests/Test_ASWLog_Base.cpp
    ../tests/Test_ASWLog_Config.cpp
    ../tests/Test_ASWLog_ConsoleLog.cpp
    ../tests/Test_ASWLog_FileLog.cpp
    ../tests/Test_ASWLog_MultiLog.cpp
    ../tests/Test_ASWLog_Types.cpp
    ../tests/Test_ASWLog_Utils.cpp
)

target_include_directories(ASWUnitTests PRIVATE
    ../../../ASWLog
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
