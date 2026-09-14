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
#include <string>
#include <string_view>
//---------------------------------------------------------------------------

namespace ASWLog
{

/*
    GenerateLogFileName

    Generates a structured filename prefix using system metrics.

    Param 'customPostfix': A user supplied string attached to the end (e.g., "TraceLog.txt").

    returns a string formatted as: YYYYMMDD_HHMMSS_mmm_PID_TID_customPostfix
*/
[[nodiscard]] std::string GenerateLogFileName(std::string_view customPostfix);

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
