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

set(ASWLOG_SANITIZE "" CACHE STRING
    "Build ASWLog and everything that links to it with a sanitizer: thread, or address (with undefined behavior)")
set_property(CACHE ASWLOG_SANITIZE PROPERTY STRINGS "" thread address)

add_library(ASWLog STATIC
    ASWLog/ASWLog_Base.cpp
    ASWLog/ASWLog_Base.h
    ASWLog/ASWLog_CategoryLog.cpp
    ASWLog/ASWLog_CategoryLog.h
    ASWLog/ASWLog_Config.cpp
    ASWLog/ASWLog_Config.h
    ASWLog/ASWLog_ConsoleLog.cpp
    ASWLog/ASWLog_ConsoleLog.h
    ASWLog/ASWLog_CrashHandler.cpp
    ASWLog/ASWLog_CrashHandler.h
    ASWLog/ASWLog_DebuggerLog.cpp
    ASWLog/ASWLog_DebuggerLog.h
    ASWLog/ASWLog_Fields.cpp
    ASWLog/ASWLog_Fields.h
    ASWLog/ASWLog_FileLog.cpp
    ASWLog/ASWLog_FileLog.h
    ASWLog/ASWLog_Formatter.cpp
    ASWLog/ASWLog_Formatter.h
    ASWLog/ASWLog_Interface.cpp
    ASWLog/ASWLog_Interface.h
    ASWLog/ASWLog_MemoryLog.cpp
    ASWLog/ASWLog_MemoryLog.h
    ASWLog/ASWLog_MultiLog.cpp
    ASWLog/ASWLog_MultiLog.h
    ASWLog/ASWLog_NullLog.cpp
    ASWLog/ASWLog_NullLog.h
    ASWLog/ASWLog_SyslogLog.cpp
    ASWLog/ASWLog_SyslogLog.h
    ASWLog/ASWLog_TextLogBase.cpp
    ASWLog/ASWLog_TextLogBase.h
    ASWLog/ASWLog_Types.cpp
    ASWLog/ASWLog_Types.h
    ASWLog/ASWLog_Utils.cpp
    ASWLog/ASWLog_Utils.h
    ASWLog/ASWLog_Version.cpp
    ASWLog/ASWLog_Version.h
    ASWLog/ASWLog_WindowsEventLog.cpp
    ASWLog/ASWLog_WindowsEventLog.h
)
add_library(ASWLog::ASWLog ALIAS ASWLog)

target_compile_features(ASWLog PUBLIC cxx_std_20)
set_target_properties(ASWLog PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(ASWLog PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ASWLog")

if(WIN32)
    # ASWLog_Utils.cpp uses the process memory APIs from psapi, and ASWLog_WindowsEventLog.cpp the Event Log and
    # registry APIs from advapi32.
    target_link_libraries(ASWLog PRIVATE psapi advapi32)
endif()

# TASWTextLogBase runs a std::thread (e.g. for FlushMode::Periodic). Public, since the thread is part of the class
# layout in the headers.
find_package(Threads REQUIRED)
target_link_libraries(ASWLog PUBLIC Threads::Threads)

if(ASWLOG_WARNINGS)
    if(MSVC)
        target_compile_options(ASWLog PRIVATE /W4)
    else()
        target_compile_options(ASWLog PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

# A sanitizer must instrument the whole program (ThreadSanitizer misses races in, or misreports them through,
# uninstrumented code), so the flags are PUBLIC: they reach the compile and link of every target linking to ASWLog.
if(ASWLOG_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "ASWLOG_SANITIZE is supported only with GCC and Clang")
    endif()

    if(ASWLOG_SANITIZE STREQUAL "thread")
        set(ASWLOG_SANITIZE_FLAGS -fsanitize=thread)
    elseif(ASWLOG_SANITIZE STREQUAL "address")
        # Stop at the first undefined behavior report, as AddressSanitizer does, so the run fails.
        set(ASWLOG_SANITIZE_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined)
    else()
        message(FATAL_ERROR "ASWLOG_SANITIZE must be empty, thread, or address (got '${ASWLOG_SANITIZE}')")
    endif()

    # Debug information and frame pointers give the sanitizer reports complete call stacks.
    target_compile_options(ASWLog PUBLIC ${ASWLOG_SANITIZE_FLAGS} -g -fno-omit-frame-pointer)
    target_link_options(ASWLog PUBLIC ${ASWLOG_SANITIZE_FLAGS})
endif()

if(ASWLOG_BUILD_EXAMPLE)
    add_subdirectory(example/cmake example)
endif()

if(ASWLOG_BUILD_TESTS)
    add_subdirectory(tests/cmake tests)
endif()
