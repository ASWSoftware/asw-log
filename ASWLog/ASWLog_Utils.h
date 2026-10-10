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
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
//---------------------------------------------------------------------------
#include "ASWLog_Types.h"
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

namespace Detail
{

// A piece of a JSON string's text (see NextJSONPiece())
struct TJSONPiece
{
    std::string_view Text;
    bool IsEscape = false; // An escape or U+FFFD, which must not be cut; otherwise ASCII and valid UTF-8, as they are
};

// A piece of a line as a MultilineMode writes it (see NextMultilinePiece())
struct TMultilinePiece
{
    std::string_view Text;
    bool IsLineBreak = false; // A line break as the mode writes it, which must not be cut; otherwise text as it is
};

//---------------------------------------------------------------------------

/*
    ApplyMultilineMode

    Rewrites the line breaks inside 'line' as 'mode' asks (see MultilineMode), Indent with 'ending' as the line ending.
    Leaves 'line' as it is, without allocating, if it has no line break to rewrite.
*/
void ApplyMultilineMode(std::string& line, MultilineMode mode, LineEnding ending);

/*
    CutToFit

    Cuts 'text' to at most 'maxBytes' bytes, between UTF-8 characters, ending with " [cut]" if that fits too. Leaves a
    text that fits as it is.
*/
void CutToFit(std::string& text, std::size_t maxBytes);

/*
    EqualsIgnoringCase

    True if 'a' and 'b' are the same text, ignoring the case of ASCII letters
*/
[[nodiscard]] bool EqualsIgnoringCase(std::string_view a, std::string_view b) noexcept;

/*
    NextJSONPiece

    The next piece of 'text', from 'index' on, as JSON::AppendString() writes it between the quotes, and moves 'index'
    past it: a run of characters written as they are (a view of 'text', which can be cut between UTF-8 sequences), or
    one escape or U+FFFD, written into 'escape'. An empty piece at the end of 'text'. Allocates nothing, so a crash
    handler can write a JSON string piece by piece into a fixed buffer.
*/
[[nodiscard]] TJSONPiece NextJSONPiece(std::string_view text, std::size_t& index, char (& escape)[6]) noexcept;

/*
    NextMultilinePiece

    The next piece of 'text', from 'index' on, as ApplyMultilineMode() writes it, and moves 'index' past it: a run of
    text without a line break to rewrite (a view of 'text'; with Preserve, the rest of it), or one line break as 'mode'
    writes it (Indent: 'ending' and the marker of the next line, "    | ", or "    |" when that line is empty). An empty
    piece at the end of 'text'. Allocates nothing, so a crash handler can use it too.
*/
[[nodiscard]] TMultilinePiece NextMultilinePiece(std::string_view text, std::size_t& index, MultilineMode mode,
    LineEnding ending) noexcept;

/*
    ReadEnvironmentVariable

    The value of the environment variable 'name' in UTF-8 (read with the wide Windows API on Windows, which the C
    runtime's getenv() doesn't use), or nullopt if it isn't set or can't be read. A variable set to an empty value
    gives an empty string. Never throws.
*/
[[nodiscard]] std::optional<std::string> ReadEnvironmentVariable(std::string_view name) noexcept;

/*
    TrimSpaces

    'text' without the spaces and tabs at its start and end
*/
[[nodiscard]] std::string_view TrimSpaces(std::string_view text) noexcept;

} // namespace Detail

namespace JSON
{

/*
    AppendString

    Appends 'text' to 'output' as a JSON string, in double quotes, escaped as RFC 8259 asks: '"' and '\' with a
    backslash, the control characters below 0x20 as \b, \f, \n, \r, \t or \u00XX. Valid UTF-8 is written as it is, and
    each invalid UTF-8 sequence (the longest start of a valid sequence, or else one byte, as Unicode recommends) is
    replaced with U+FFFD, so the result is always valid UTF-8 JSON.
*/
void AppendString(std::string& output, std::string_view text);

} // namespace JSON

namespace Time
{

// The size of the buffer WriteISO8601() writes to: enough for the longest form, a 6-character year with nanoseconds
// and an offset
inline constexpr std::size_t ISO8601BufferSize = 40;

//---------------------------------------------------------------------------

/*
    GetUTCOffsetMinutes

    How many minutes the local time zone is ahead of UTC at 'timePoint' (negative west of UTC), including daylight
    saving time if it is in effect then, e.g. -300 for US Central daylight time. Returns 0 if the local time can't be
    determined.
*/
[[nodiscard]] int GetUTCOffsetMinutes(std::chrono::system_clock::time_point timePoint);

/*
    ToISO8601String

    Converts a system time point into an ISO 8601 string, as WriteISO8601() writes it.
        Format: YYYY-MM-DDTHH:mm:ss.mmmZ (the defaults)
*/
[[nodiscard]] std::string ToISO8601String(std::chrono::system_clock::time_point timePoint, TimeZone zone = TimeZone::UTC,
    TimePrecision precision = TimePrecision::Milliseconds);

/*
    ToDateString

    The date part of ToISO8601String(), in 'zone', e.g. for daily rolling.
        Format: YYYY-MM-DD
*/
[[nodiscard]] std::string ToDateString(std::chrono::system_clock::time_point timePoint, TimeZone zone = TimeZone::UTC);

/*
    ToLocalISO8601String

    Converts a system time point into an ISO 8601 local time string with the local time zone's offset from UTC (see
    GetUTCOffsetMinutes()), with milliseconds: ToISO8601String() with TimeZone::Local.
        Format: YYYY-MM-DDTHH:mm:ss.mmm+hh:mm (e.g. 2026-09-28T21:02:44.123-05:00)
*/
[[nodiscard]] std::string ToLocalISO8601String(std::chrono::system_clock::time_point timePoint);

/*
    WriteISO8601

    Writes 'timePoint' as an ISO 8601 string into 'buffer' and returns the number of characters written: the date, the
    time with 3, 6 or 9 digits of the second ('precision'), and "Z" for TimeZone::UTC, or the local time zone's offset
    from UTC for TimeZone::Local (see GetUTCOffsetMinutes()). A 4-digit year for the years 0 to 9999, the year in full
    outside them. Allocates nothing. UTC is calendar arithmetic only, so a crash handler can use it, even in a POSIX
    signal handler; Local calls localtime_s/localtime_r, which isn't safe there, and falls back to UTC if the local
    time can't be determined.
        Format: YYYY-MM-DDTHH:mm:ss.mmmZ (the defaults), or e.g. 2026-09-28T16:02:44.342519-05:00
*/
[[nodiscard]] std::size_t WriteISO8601(char (& buffer)[ISO8601BufferSize], std::chrono::system_clock::time_point timePoint,
    TimeZone zone = TimeZone::UTC, TimePrecision precision = TimePrecision::Milliseconds) noexcept;

} // namespace Time

} // namespace ASWLog

#endif // #ifndef ASWLog_UtilsH
