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
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_FileLog.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_MultiLog.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Types.cpp"
    "${ASWLOG_TESTS_DIR}/Test_ASWLog_Utils.cpp"
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
