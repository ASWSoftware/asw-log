cmake_minimum_required(VERSION 3.20)

project(ASWLogExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ASWLOG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# The ASWLog::ASWLog library target, from the repository root's CMakeLists.txt. It already exists when the root
# project builds the example (ASWLOG_BUILD_EXAMPLE).
if(NOT TARGET ASWLog::ASWLog)
    set(ASWLOG_WARNINGS ON)
    add_subdirectory("${ASWLOG_ROOT}" aswlog)
endif()

add_executable(ASWLogExample
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/main.cpp"
)

target_include_directories(ASWLogExample PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(ASWLogExample PRIVATE ASWLog::ASWLog)

set_target_properties(ASWLogExample PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${ASWLOG_ROOT}/example/build/bin"
)

foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(ASWLogExample PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${ASWLOG_ROOT}/example/build/bin/${configuration}"
    )
endforeach()

if(MSVC)
    target_compile_options(ASWLogExample PRIVATE /W4)
else()
    target_compile_options(ASWLogExample PRIVATE -Wall -Wextra -Wpedantic)
endif()
