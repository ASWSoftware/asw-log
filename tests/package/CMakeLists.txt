cmake_minimum_required(VERSION 3.20)

project(ASWLogPackageCheck LANGUAGES CXX)

# Checks the installed CMake package: builds Check_Package.cpp against an ASWLog installed with `cmake --install`
# (point CMAKE_PREFIX_PATH at the install prefix), and `ctest` runs it. See the README's CMake section.
find_package(ASWLog CONFIG REQUIRED)

add_executable(ASWLogPackageCheck
    "${CMAKE_CURRENT_SOURCE_DIR}/Check_Package.cpp"
)

target_link_libraries(ASWLogPackageCheck PRIVATE ASWLog::ASWLog)

# The version the package's version file reported, which the check compares with the installed headers' version
target_compile_definitions(ASWLogPackageCheck PRIVATE "ASWLOG_PACKAGE_VERSION=\"${ASWLog_VERSION}\"")

if(MSVC)
    target_compile_options(ASWLogPackageCheck PRIVATE /W4)
else()
    target_compile_options(ASWLogPackageCheck PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MINGW)
    # Statically link the MinGW runtime so the executable launches without MinGW's bin directory on PATH (e.g. on a
    # CI runner).
    target_link_options(ASWLogPackageCheck PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

enable_testing()
add_test(NAME ASWLogPackageCheck COMMAND ASWLogPackageCheck)
