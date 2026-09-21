cmake_minimum_required(VERSION 3.20)

project(ASWLogExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ASWLOG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(ASWLogExample
    "${ASWLOG_ROOT}/ASWLog/ASWLog_Base.cpp"
    "${ASWLOG_ROOT}/ASWLog/ASWLog_Config.cpp"
    "${ASWLOG_ROOT}/ASWLog/ASWLog_ConsoleLog.cpp"
    "${ASWLOG_ROOT}/ASWLog/ASWLog_FileLog.cpp"
    "${ASWLOG_ROOT}/ASWLog/ASWLog_Interface.cpp"
    "${ASWLOG_ROOT}/ASWLog/ASWLog_MultiLog.cpp"
    "${ASWLOG_ROOT}/ASWLog/ASWLog_Types.cpp"
    "${ASWLOG_ROOT}/ASWLog/ASWLog_Utils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/main.cpp"
)

target_include_directories(ASWLogExample PRIVATE
    "${ASWLOG_ROOT}/ASWLog"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

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

if(WIN32)
    target_link_libraries(ASWLogExample PRIVATE psapi)
endif()
