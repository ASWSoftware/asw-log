cmake_minimum_required(VERSION 3.20)

# The version lives only in ASWLog/ASWLog_Version.h, so the project version comes from its ASWLOG_VERSION_MAJOR,
# _MINOR, and _PATCH macros (a CMake version has no pre-release part).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/ASWLog/ASWLog_Version.h" ASWLOG_VERSION_DEFINES
    REGEX "^#define ASWLOG_VERSION_(MAJOR|MINOR|PATCH) ")
foreach(versionPart MAJOR MINOR PATCH)
    if(NOT ASWLOG_VERSION_DEFINES MATCHES "#define ASWLOG_VERSION_${versionPart} ([0-9]+)")
        message(FATAL_ERROR "ASWLog/ASWLog_Version.h doesn't define ASWLOG_VERSION_${versionPart} as a number")
    endif()
    set(ASWLOG_VERSION_${versionPart} "${CMAKE_MATCH_1}")
endforeach()

project(ASWLog
    VERSION ${ASWLOG_VERSION_MAJOR}.${ASWLOG_VERSION_MINOR}.${ASWLOG_VERSION_PATCH}
    LANGUAGES CXX
)

# Building ASWLog itself (cmake -S .) rather than consuming it through add_subdirectory() or FetchContent.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ASWLOG_IS_TOP_LEVEL ON)
else()
    set(ASWLOG_IS_TOP_LEVEL OFF)
endif()

option(ASWLOG_BUILD_EXAMPLE "Build the ASWLog example (example/cmake)" OFF)
option(ASWLOG_BUILD_TESTS "Build the ASWLog unit tests (tests/cmake; needs the asw-unit-tests submodule)" OFF)
option(ASWLOG_INSTALL "Generate the install rules for the ASWLog library, headers, and CMake package files"
    ${ASWLOG_IS_TOP_LEVEL})
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
    ASWLog/ASWLog_Limiter.cpp
    ASWLog/ASWLog_Limiter.h
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
    ASWLog/ASWLog_Unicode.cpp
    ASWLog/ASWLog_Unicode.h
    ASWLog/ASWLog_Utils.cpp
    ASWLog/ASWLog_Utils.h
    ASWLog/ASWLog_Version.cpp
    ASWLog/ASWLog_Version.h
    ASWLog/ASWLog_WindowsEventLog.cpp
    ASWLog/ASWLog_WindowsEventLog.h
)
add_library(ASWLog::ASWLog ALIAS ASWLog)

target_compile_features(ASWLog PUBLIC cxx_std_20)
# A Debug build is named ASWLogd, so Debug and Release can be installed to the same prefix.
set_target_properties(ASWLog PROPERTIES
    CXX_EXTENSIONS OFF
    DEBUG_POSTFIX d
)

# The installed target's include folder comes from install(TARGETS) below.
target_include_directories(ASWLog PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ASWLog>")

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

# After `cmake --install`, find_package(ASWLog CONFIG) gives the ASWLog::ASWLog target. The headers are installed to
# <prefix>/include/ASWLog, the installed target's include folder, so code includes them the same way as from the
# sources (e.g. #include "ASWLog_FileLog.h").
if(ASWLOG_INSTALL)
    include(CMakePackageConfigHelpers)
    include(GNUInstallDirs)

    set(ASWLOG_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/ASWLog")

    install(TARGETS ASWLog
        EXPORT ASWLogTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ASWLog"
    )

    # Every header is public.
    get_target_property(ASWLOG_HEADERS ASWLog SOURCES)
    list(FILTER ASWLOG_HEADERS INCLUDE REGEX "\\.h$")
    install(FILES ${ASWLOG_HEADERS} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ASWLog")

    install(EXPORT ASWLogTargets
        NAMESPACE ASWLog::
        DESTINATION "${ASWLOG_INSTALL_CMAKEDIR}"
    )

    configure_package_config_file(cmake/ASWLogConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/ASWLogConfig.cmake"
        INSTALL_DESTINATION "${ASWLOG_INSTALL_CMAKEDIR}"
    )

    # As in Semantic Versioning, find_package(ASWLog 1.2) accepts version 1.2.0 or any later 1.x, but not 2.0.
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/ASWLogConfigVersion.cmake"
        VERSION "${ASWLog_VERSION}"
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/ASWLogConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ASWLogConfigVersion.cmake"
        DESTINATION "${ASWLOG_INSTALL_CMAKEDIR}"
    )
endif()

if(ASWLOG_BUILD_EXAMPLE)
    add_subdirectory(example/cmake example)
endif()

if(ASWLOG_BUILD_TESTS)
    add_subdirectory(tests/cmake tests)
endif()
