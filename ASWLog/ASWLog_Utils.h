/* **************************************************************************
ASWLog_Utils.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Source for the ASWLog utility functions, etc.

Requires C++ 20 or higher.

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

#pragma once

#ifndef ASWLog_UtilsH
#define ASWLog_UtilsH
//---------------------------------------------------------------------------
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
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

    returns a string formatted as: [prefix_]YYYYMMDD_HHMMSS_mmm_PID<pid>_TID<tid>[_customPostfix]
    (each bracketed segment, and its separating underscore, is left out entirely when empty), where <pid> and <tid>
    are the OS process and thread ids (see GetCurrentOSProcessId() and GetCurrentOSThreadId())
*/
[[nodiscard]] std::string GenerateLogFileName(std::string_view prefix, std::string_view customPostfix);

[[nodiscard]] std::string GetApplicationInfoString();
[[nodiscard]] std::string GetCommandLineString();

// The operating system's id for the current process, as shown by Task Manager, ps, and debuggers.
[[nodiscard]] std::uint32_t GetCurrentOSProcessId() noexcept;

// The operating system's id for the calling thread, as shown by debuggers, crash dumps, Process Explorer, and top -H
// (unlike std::thread::id, which has no numeric value outside the program).
[[nodiscard]] std::uint32_t GetCurrentOSThreadId() noexcept;

[[nodiscard]] std::string GetDriveInfoString();
[[nodiscard]] std::filesystem::path GetExecutablePath();
[[nodiscard]] TMemoryUsage GetMemoryUsage();
[[nodiscard]] std::string GetMemoryUsageString();
[[nodiscard]] std::string GetOSInfoString();
[[nodiscard]] TSystemMemoryUsage GetSystemMemoryUsage();
[[nodiscard]] std::string GetSystemMemoryUsageString();
[[nodiscard]] std::string GetTimeInfoString();

#if defined(_WIN32)
/*
    GetWindowsEditionName

    Names the Windows edition for a product type returned by GetProductInfo() (e.g. PRODUCT_CORE is "Home",
    PRODUCT_DATACENTER_SERVER is "Server Datacenter"), as used by GetOSInfoString().

    Param 'isServer': Whether the OS is a server (OSVERSIONINFOEXW::wProductType isn't VER_NT_WORKSTATION). Used only
        for a product type without a name here, which is returned as "Server (product type 0x...)" or
        "Unknown (product type 0x...)".
*/
[[nodiscard]] std::string GetWindowsEditionName(std::uint32_t productType, bool isServer);
#endif

/*
    IsRootFolder

    True if 'folder' is the root of a drive, network share, volume or file system (e.g. "C:\", "\\server\share\", "/"),
    or if that can't be determined (e.g. the path is empty or its server can't be reached). A relative path is resolved
    against the current folder, so "." is a root folder when the current folder is. Never throws.
*/
[[nodiscard]] bool IsRootFolder(const std::filesystem::path& folder) noexcept;

[[nodiscard]] bool MatchesWildcard(std::string_view value, std::string_view pattern);

/*
    PathToUTF8String

    Converts a path to a UTF-8 string. Unlike std::filesystem::path::string(), which converts to the Windows ANSI code
    page and can throw for characters it can't represent, this never throws; it returns an empty string if the path
    can't be converted.
*/
[[nodiscard]] std::string PathToUTF8String(const std::filesystem::path& path) noexcept;

/*
    RenameWithoutReplacing

    Renames the file 'from' to 'to', unless 'to' already exists: unlike std::filesystem::rename, it never replaces an
    existing file, even when another process creates 'to' at the same moment. Returns std::errc::file_exists in that
    case, another error if the rename fails, or an empty error_code on success. Never throws.
*/
[[nodiscard]] std::error_code RenameWithoutReplacing(const std::filesystem::path& from, const std::filesystem::path& to) noexcept;

namespace Time
{

/*
    GetUTCOffsetMinutes

    How many minutes the local time zone is ahead of UTC at 'timePoint' (negative west of UTC), including daylight
    saving time if it is in effect then, e.g. -300 for US Central daylight time. Returns 0 if the local time can't be
    determined.
*/
[[nodiscard]] int GetUTCOffsetMinutes(std::chrono::system_clock::time_point timePoint);

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

/*
    ToLocalISO8601String

    Converts a system time point into an ISO 8601 local time string with the local time zone's offset from UTC (see
    GetUTCOffsetMinutes()), with milliseconds like ToISO8601String().
        Format: YYYY-MM-DDTHH:mm:ss.mmm+hh:mm (e.g. 2026-09-28T21:02:44.123-05:00)
*/
[[nodiscard]] std::string ToLocalISO8601String(std::chrono::system_clock::time_point timePoint);

} // namespace Time

} // namespace ASWLog

#endif // #ifndef ASWLog_UtilsH
