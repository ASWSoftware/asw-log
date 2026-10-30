cmake_minimum_required(VERSION 3.20)

project(ASWLogTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ASWLOG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(ASWLOG_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tests")
set(ASWUNITTESTS_ROOT "${ASWLOG_ROOT}/third_party/asw-unit-tests")

# The ASWUnitTests framework is a git submodule. Fail with instructions rather than falling back to a download, so a
# build (CI included) always uses exactly the commit the submodule pins.
if(NOT EXISTS "${ASWUNITTESTS_ROOT}/src/ASWUnitTests_Sources.cmake")
    message(FATAL_ERROR
        "The ASWUnitTests submodule is missing from: ${ASWUNITTESTS_ROOT}\n"
        "From the repository root, run:\n"
        "    git submodule update --init\n"
        "or clone with:\n"
        "    git clone --recurse-submodules https://github.com/ASWSoftware/asw-log.git")
endif()

include("${ASWUNITTESTS_ROOT}/src/ASWUnitTests_Sources.cmake")

# The ASWLog::ASWLog library target, from the repository root's CMakeLists.txt. It already exists when the root
# project builds the tests (ASWLOG_BUILD_TESTS).
if(NOT TARGET ASWLog::ASWLog)
    set(ASWLOG_WARNINGS ON)
    add_subdirectory("${ASWLOG_ROOT}" aswlog)
endif()

# The test modules and helpers, which both the console runner and, on Windows, the Win32 GUI runner run.
set(ASWLOG_TEST_SOURCES
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Base.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_CategoryLog.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Config.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_ConsoleLog.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_CrashHandler.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_DebuggerLog.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Fields.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_FileLog.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Formatter.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Limiter.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_MemoryLog.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_MultiLog.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_NullLog.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_SyslogLog.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_TextLogBase.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Types.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Unicode.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Utils.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Version.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_WindowsEventLog.cpp"
    "${ASWLOG_TESTS_DIR}/UT_Helper_DateTime.cpp"
    "${ASWLOG_TESTS_DIR}/UT_Helper_Environment.cpp"
    "${ASWLOG_TESTS_DIR}/UT_Helper_JSON.cpp"
    "${ASWLOG_TESTS_DIR}/UT_Helper_StdErr.cpp"
)

# ASWUnitTests' --test-timeout-seconds uses std::thread/std::future, which needs an explicit threading library on
# Linux and some MinGW-w64 distributions.
find_package(Threads REQUIRED)

# The settings both test runners share.
function(aswlog_configure_test_runner target)
    target_include_directories(${target} PRIVATE "${ASWLOG_TESTS_DIR}")
    target_link_libraries(${target} PRIVATE ASWLog::ASWLog Threads::Threads)

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(MINGW)
        # Statically link the MinGW runtime so the executable launches without MinGW's bin directory on PATH (e.g. on
        # a CI runner), and can't load another program's older libstdc++-6.dll from it.
        target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++ -static)
    endif()

    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endfunction()

add_executable(ASWLogTests
    ${ASWUNITTESTS_SOURCES}
    ${ASWLOG_TEST_SOURCES}
)

target_include_directories(ASWLogTests PRIVATE "${ASWUNITTESTS_SOURCE_DIR}")
aswlog_configure_test_runner(ASWLogTests)

if(WIN32)
    # The ASWUnitTests icon, which the resource compiler finds through the include path.
    target_sources(ASWLogTests PRIVATE "${ASWUNITTESTS_ROOT}/resource/ASWUnitTests_Icon.rc")
    target_include_directories(ASWLogTests PRIVATE "${ASWUNITTESTS_ROOT}/resource")

    # The Win32 GUI runner: the same tests in a window. Its WinMain takes the place of the console runner's main.cpp,
    # and its sources list has the icon.
    include("${ASWUNITTESTS_ROOT}/win32/gui/src/ASWUnitTests_Win32GUI_Sources.cmake")

    add_executable(ASWLogTests_Win32_GUI WIN32
        ${ASWUNITTESTS_WIN32_GUI_SOURCES}
        ${ASWLOG_TEST_SOURCES}
    )

    target_include_directories(ASWLogTests_Win32_GUI PRIVATE ${ASWUNITTESTS_WIN32_GUI_INCLUDE_DIRS})
    target_link_libraries(ASWLogTests_Win32_GUI PRIVATE ${ASWUNITTESTS_WIN32_GUI_LIBRARIES})
    target_link_options(ASWLogTests_Win32_GUI PRIVATE ${ASWUNITTESTS_WIN32_GUI_LINK_OPTIONS})
    aswlog_configure_test_runner(ASWLogTests_Win32_GUI)
endif()

# The *Fmt methods check a format string against its arguments at compile time, which a unit test can't show, so
# configuring checks it: tests/compile_checks/Check_FormatString.cpp must compile as case 0 (the control) and fail as
# every other case (a mismatched format string). Compiled only, not linked.
function(aswlog_check_format_string checkCase expectCompiles)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    try_compile(ASWLOG_FORMAT_CHECK_${checkCase}
        "${CMAKE_CURRENT_BINARY_DIR}/compile_checks/format_string_${checkCase}"
        "${ASWLOG_ROOT}/tests/compile_checks/Check_FormatString.cpp"
        CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${ASWLOG_ROOT}/ASWLog"
        COMPILE_DEFINITIONS "-DASWLOG_CHECK_CASE=${checkCase}"
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        OUTPUT_VARIABLE output
    )

    if(expectCompiles AND NOT ASWLOG_FORMAT_CHECK_${checkCase})
        message(FATAL_ERROR "Check_FormatString.cpp case ${checkCase} should compile but didn't:\n${output}")
    elseif(NOT expectCompiles AND ASWLOG_FORMAT_CHECK_${checkCase})
        message(FATAL_ERROR "Check_FormatString.cpp case ${checkCase} has a mismatched format string, which should "
            "fail to compile, but it compiled")
    endif()
endfunction()

aswlog_check_format_string(0 TRUE)
foreach(checkCase 1 2 3 4 5 6)
    aswlog_check_format_string(${checkCase} FALSE)
endforeach()
message(STATUS "Format string compile-time checks: passed")
