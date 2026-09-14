/* **************************************************************************
ASWLog_Utils.cpp
Author: Anthony S. West - ASW Software

See header for info.

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

//---------------------------------------------------------------------------
// Module header
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------
// System includes here
#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <thread>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------
std::string GenerateLogFileName(std::string_view customPostfix)
{
    // 1. Capture exact high-precision time point
    auto now = std::chrono::system_clock::now();
    auto timeTimeT = std::chrono::system_clock::to_time_t(now);

    // 2. Safely extract millisecond fraction component
    auto durationSinceEpoch = now.time_since_epoch();
    auto secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(durationSinceEpoch);
    auto millisecondsFraction = std::chrono::duration_cast<std::chrono::milliseconds>(durationSinceEpoch - secondsSinceEpoch).count();

    // 3. Thread-safely unpack time_t into a calendar structure (Windows vs Linux)
    std::tm localCalendarTime{};
#if defined(_WIN32)
    localtime_s(&localCalendarTime, &timeTimeT);
#else
    localtime_r(&timeTimeT, &localCalendarTime);
#endif

    // 4. Safely pull platform Process ID
#if defined(_WIN32)
    auto processId = _getpid();
#else
    auto processId = getpid();
#endif

    // 5. Hash native thread object for identifier tracking
    auto threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());

    // 6. Generate the unique string output manually using zero-dependency layouts
    // Format target layout: YYYYMMDD_HHMMSS_mmm
    std::string timeStr = std::format("{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}",
        localCalendarTime.tm_year + 1900,
        localCalendarTime.tm_mon + 1,
        localCalendarTime.tm_mday,
        localCalendarTime.tm_hour,
        localCalendarTime.tm_min,
        localCalendarTime.tm_sec,
        millisecondsFraction);

    return std::format("{}_PID{}_TID{}_{}", timeStr, processId, threadId, customPostfix);
}

//---------------------------------------------------------------------------

namespace Time
{

//---------------------------------------------------------------------------
std::string ToISO8601String(std::chrono::system_clock::time_point timePoint)
{
    auto timeTimeT = std::chrono::system_clock::to_time_t(timePoint);

    // Track fractional milliseconds
    auto durationSinceEpoch = timePoint.time_since_epoch();
    auto secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(durationSinceEpoch);
    auto millisecondsFraction = std::chrono::duration_cast<std::chrono::milliseconds>(durationSinceEpoch - secondsSinceEpoch).count();

    std::tm utcTime{};
#if defined(_WIN32)
    gmtime_s(&utcTime, &timeTimeT); // Use secure gmtime variants for UTC processing
#else
    gmtime_r(&timeTimeT, &utcTime);
#endif

    // Strictly compliant ISO 8601 formatting with T separator and Z suffix
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
        utcTime.tm_year + 1900,
        utcTime.tm_mon + 1,
        utcTime.tm_mday,
        utcTime.tm_hour,
        utcTime.tm_min,
        utcTime.tm_sec,
        millisecondsFraction);
}

//---------------------------------------------------------------------------
std::string ToDateString(std::chrono::system_clock::time_point timePoint)
{
    auto timeTimeT = std::chrono::system_clock::to_time_t(timePoint);
    std::tm utcTime{};
#if defined(_WIN32)
    gmtime_s(&utcTime, &timeTimeT);
#else
    gmtime_r(&timeTimeT, &utcTime);
#endif

    return std::format("{:04}-{:02}-{:02}",
        utcTime.tm_year + 1900,
        utcTime.tm_mon + 1,
        utcTime.tm_mday);
}

} // namespace Time

//---------------------------------------------------------------------------

} // namespace ASWLog
