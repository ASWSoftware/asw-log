/* **************************************************************************
ASWLog_Utils.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Source for the ASWLog utility functions, etc.

Requires C++ 20 or higher.

Copyright 2026 Anthony S. West

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

#pragma once

#ifndef ASWLog_UtilsH
#define ASWLog_UtilsH
//---------------------------------------------------------------------------
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------

namespace ASWLog
{

struct TMemoryUsage
{
    std::size_t WorkingSetBytes = 0;
    std::size_t PeakWorkingSetBytes = 0;
};

struct TSystemMemoryUsage
{
    std::uintmax_t TotalBytes = 0;
    std::uintmax_t AvailableBytes = 0;
};

//---------------------------------------------------------------------------

/*
    GenerateLogFileName

    Generates a structured filename using system metrics.

    Param 'prefix': A user supplied string attached to the front (e.g., an app or module
        name), so files sharing the same prefix sort/group together alphabetically in a
        directory listing. Pass an empty string_view to omit it.
    Param 'customPostfix': A user supplied string attached to the end (e.g., "TraceLog.txt").
        Pass an empty string_view to omit it.

    returns a string formatted as: [prefix_]YYYYMMDD_HHMMSS_mmm_PID_TID[_customPostfix]
    (each bracketed segment, and its separating underscore, is left out entirely when empty)
*/
[[nodiscard]] std::string GenerateLogFileName(std::string_view prefix, std::string_view customPostfix);

[[nodiscard]] std::string GetApplicationInfoString();
[[nodiscard]] std::string GetCommandLineString();
[[nodiscard]] std::string GetDriveInfoString();
[[nodiscard]] std::filesystem::path GetExecutablePath();
[[nodiscard]] TMemoryUsage GetMemoryUsage();
[[nodiscard]] std::string GetMemoryUsageString();
[[nodiscard]] std::string GetOSInfoString();
[[nodiscard]] TSystemMemoryUsage GetSystemMemoryUsage();
[[nodiscard]] std::string GetSystemMemoryUsageString();
[[nodiscard]] std::string GetTimeInfoString();

[[nodiscard]] bool MatchesWildcard(std::string_view value, std::string_view pattern);

namespace Time
{

/*
    ToISO8601String

    Converts a high-precision system time point into a valid ISO 8601 UTC string.
        Format: YYYY-MM-DDTHH:mm:ss.mmmZ
*/
[[nodiscard]] std::string ToISO8601String(std::chrono::system_clock::time_point timePoint);

/*
    ToDateString

    Extracts just the calendar date segment needed for midnight rolling checks.
        Format: YYYY-MM-DD
*/
[[nodiscard]] std::string ToDateString(std::chrono::system_clock::time_point timePoint);

} // namespace Time

} // namespace ASWLog

#endif // #ifndef ASWLog_UtilsH
