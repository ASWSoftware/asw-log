cmake_minimum_required(VERSION 3.20)

project(ASWLog LANGUAGES CXX)

# Building ASWLog itself (cmake -S .) rather than consuming it through add_subdirectory() or FetchContent.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ASWLOG_IS_TOP_LEVEL ON)
else()
    set(ASWLOG_IS_TOP_LEVEL OFF)
endif()

option(ASWLOG_BUILD_EXAMPLE "Build the ASWLog example (example/cmake)" OFF)
option(ASWLOG_BUILD_TESTS "Build the ASWLog unit tests (tests/cmake; needs the asw-unit-tests submodule)" OFF)
option(ASWLOG_WARNINGS "Compile the ASWLog sources with extra warnings" ${ASWLOG_IS_TOP_LEVEL})

add_library(ASWLog STATIC
    ASWLog/ASWLog_Base.cpp
    ASWLog/ASWLog_Base.h
    ASWLog/ASWLog_Config.cpp
    ASWLog/ASWLog_Config.h
    ASWLog/ASWLog_ConsoleLog.cpp
    ASWLog/ASWLog_ConsoleLog.h
    ASWLog/ASWLog_FileLog.cpp
    ASWLog/ASWLog_FileLog.h
    ASWLog/ASWLog_Interface.cpp
    ASWLog/ASWLog_Interface.h
    ASWLog/ASWLog_MultiLog.cpp
    ASWLog/ASWLog_MultiLog.h
    ASWLog/ASWLog_Types.cpp
    ASWLog/ASWLog_Types.h
    ASWLog/ASWLog_Utils.cpp
    ASWLog/ASWLog_Utils.h
)
add_library(ASWLog::ASWLog ALIAS ASWLog)

target_compile_features(ASWLog PUBLIC cxx_std_20)
set_target_properties(ASWLog PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(ASWLog PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ASWLog")

if(WIN32)
    # ASWLog_Utils.cpp uses the process memory APIs from psapi.
    target_link_libraries(ASWLog PRIVATE psapi)
endif()

if(ASWLOG_WARNINGS)
    if(MSVC)
        target_compile_options(ASWLog PRIVATE /W4)
    else()
        target_compile_options(ASWLog PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(ASWLOG_BUILD_EXAMPLE)
    add_subdirectory(example/cmake example)
endif()

if(ASWLOG_BUILD_TESTS)
    add_subdirectory(tests/cmake tests)
endif()
