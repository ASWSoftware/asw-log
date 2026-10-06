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

add_executable(ASWLogTests
    ${ASWUNITTESTS_SOURCES}
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Base.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Config.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_ConsoleLog.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_CrashHandler.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_FileLog.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Formatter.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_MultiLog.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_TextLogBase.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Types.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Utils.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Version.cpp"
)

target_include_directories(ASWLogTests PRIVATE
    "${ASWUNITTESTS_SOURCE_DIR}"
    "${ASWLOG_TESTS_DIR}"
)

target_link_libraries(ASWLogTests PRIVATE ASWLog::ASWLog)

# ASWUnitTests' --test-timeout-seconds uses std::thread/std::future, which needs an explicit threading library on
# Linux and some MinGW-w64 distributions.
find_package(Threads REQUIRED)
target_link_libraries(ASWLogTests PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(ASWLogTests PRIVATE /W4 /Zc:__cplusplus)
else()
    target_compile_options(ASWLogTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MINGW)
    # Statically link the MinGW runtime so the executable launches without MinGW's bin directory on PATH (e.g. on a
    # CI runner).
    target_link_options(ASWLogTests PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

set_target_properties(ASWLogTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

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
foreach(checkCase 1 2 3)
    aswlog_check_format_string(${checkCase} FALSE)
endforeach()
message(STATUS "Format string compile-time checks: passed")
