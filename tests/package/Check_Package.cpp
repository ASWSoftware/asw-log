/* **************************************************************************
Check_Package.cpp
Author: Anthony S. West - ASW Software

Check of the installed CMake package: tests/package/CMakeLists.txt builds this file against an ASWLog installed with
`cmake --install` and found with find_package(), and running it returns 0 when the package works.

Copyright 2026 ASW Software

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

************************************************************************** */

#include <format>
#include <iostream>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_MemoryLog.h"
#include "ASWLog_Version.h"
//---------------------------------------------------------------------------

#if !defined(ASWLOG_PACKAGE_VERSION)
#error Define ASWLOG_PACKAGE_VERSION
#endif

int main()
{
    // The version find_package() found (from ASWLogConfigVersion.cmake) must be the installed headers' version
    const std::string headerVersion = std::format("{}.{}.{}", ASWLog::VersionMajor, ASWLog::VersionMinor,
        ASWLog::VersionPatch);
    if (headerVersion != ASWLOG_PACKAGE_VERSION)
    {
        std::cerr << std::format("The package version {} isn't the headers' version {}\n", ASWLOG_PACKAGE_VERSION,
            headerVersion);
        return 1;
    }

    // The default configuration writes the startup lines, which use the system libraries the installed target links to
    // (e.g. the memory usage)
    constexpr std::string_view message = "Logged through the installed ASWLog package";
    ASWLog::TASWMemoryLog log;
    if (!log.Initialize(ASWLog::TASWLogConfig()))
    {
        std::cerr << "TASWMemoryLog::Initialize() failed\n";
        return 1;
    }

    log.LogInfo(message);
    const auto lines = log.GetLines();
    if (lines.size() < 2 || !lines.back().ends_with(message))
    {
        std::cerr << "The memory logger should have kept the startup lines and the message\n";
        return 1;
    }

    std::cout << "ASWLog " << ASWLog::Version << " package works: " << lines.size() << " lines logged\n";

    return 0;
}
