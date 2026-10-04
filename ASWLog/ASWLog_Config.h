/* **************************************************************************
ASWLog_Config.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Source for the ASWLog config options.

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

#ifndef ASWLog_ConfigH
#define ASWLog_ConfigH
//---------------------------------------------------------------------------
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_Types.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

class IASWLogFormatter; // See ASWLog_Formatter.h

/////////////////////////////////////////////////////////////////////////////
// TASWFileConfig
//
// The log file's location, flushing, reopening, rotation and retention (TASWLogConfig::File). Only a file logger
// uses these.
/////////////////////////////////////////////////////////////////////////////
struct TASWFileConfig
{
    // The log file is FilePath inside FolderPath, or FilePath alone if it is absolute (see ResolvePath())
    std::filesystem::path FolderPath = "logs";
    std::filesystem::path FilePath = "aswlog.txt";
    // Opens and closes the log file for every entry. Required when several processes write to one log file: with the
    // file kept open, other processes can't open it on Windows, and on POSIX they keep appending to the renamed backup
    // after a rotation.
    bool AutoOpenClosePerWrite = false;

    FlushMode Flush = FlushMode::EveryWrite;
    std::chrono::milliseconds FlushInterval{ 1000 }; // Used by FlushMode::Periodic

    // Retry log entry options
    int OpenRetryCount = 5;
    std::chrono::milliseconds OpenRetryDelay{ 50 };

    // Circuit breaker for a log file that couldn't be reopened (e.g. after a rotation while another program held it).
    // Entries are dropped, without trying to open the file, until this long after the last failed attempt. The next
    // entry then tries again, with OpenRetryCount/OpenRetryDelay. 0 = try on every entry. Not used with
    // AutoOpenClosePerWrite, which opens the file for every entry anyway.
    std::chrono::milliseconds CircuitBreakerResetDelay{ 500 };

    // --- Log Rotation and Rolling Options ---
    bool EnableRotation       = false;
    std::uintmax_t MaxFileSizeBytes = 10 * 1024 * 1024; // Default 10MB
    // After a failed size rotation (e.g. another program holds the file), entries keep going to the current file, and
    // rotating isn't tried again until this long after the failure. 0 = try on every entry.
    std::chrono::milliseconds RotationRetryDelay{ 500 };
    // Rolls the file over at UTC midnight, and at Initialize() if the file was last written on an earlier UTC day. The
    // backup is named for the day it holds.
    bool EnableDailyRolling   = false;

    // --- Backup Cleanup Options (applied after each successful rotation) ---
    // Each rule deletes some of this log's backups: the files in its folder named "<stem>.<reason>.<time>.bak", as
    // rotation names them (not those of another log whose name starts the same way). A backup is deleted if any rule
    // says so. Backups are ordered by when their newest entry was written (their last write time).
    std::chrono::hours RetentionMaxAge{ 0 }; // 0 = disabled. Deletes the backups older than this.
    std::size_t MaxBackupFiles = 0; // 0 = unlimited. Keeps the newest this many backups, deleting the older ones.
    // 0 = unlimited. Keeps the newest backups that together take at most this many bytes, deleting the older ones, and
    // even the backup just made if it alone is larger, so set it above MaxFileSizeBytes.
    std::uintmax_t MaxBackupTotalBytes = 0;

    // The folder that holds the log file
    [[nodiscard]] std::filesystem::path ResolveFolder() const
    {
        return ResolvePath().parent_path();
    }

    // The log file's path: FilePath inside FolderPath, or FilePath if it is absolute. An empty FolderPath or FilePath
    // means its default ("logs", "aswlog.txt").
    [[nodiscard]] std::filesystem::path ResolvePath() const
    {
        auto candidate = FilePath.empty() ? std::filesystem::path("aswlog.txt") : FilePath;
        auto baseFolder = FolderPath.empty() ? std::filesystem::path("logs") : FolderPath;

        if (candidate.is_absolute())
        {
            return candidate.lexically_normal();
        }

        auto resolvedBase = baseFolder.lexically_normal();
        return (resolvedBase / candidate).lexically_normal();
    }
};


/////////////////////////////////////////////////////////////////////////////
// TASWLineConfig
//
// How a text logger writes each entry's line (TASWLogConfig::Line).
/////////////////////////////////////////////////////////////////////////////
struct TASWLineConfig
{
    // Formats each entry's line (not LogRaw entries). Empty: TASWTextFormatter's layout, from the Show* options below.
    // One formatter can be shared by several loggers.
    std::shared_ptr<const IASWLogFormatter> Formatter;
    LineEnding Ending = LineEnding::LF; // Added after each line (not after LogRaw entries)

    // The fields TASWTextFormatter writes before the message
    bool ShowTimestamp      = true; // The entry's time, in UTC
    bool ShowLevel          = true;
    bool ShowProcessId      = true;
    bool ShowThreadId       = true;
    bool ShowWorkingSet     = false; // The process's memory use
    bool ShowPeakWorkingSet = false;
    bool ShowFunctionName   = false; // The function that logged the entry
    bool ShowSourceLine     = false; // The source file and line that logged the entry
};


/////////////////////////////////////////////////////////////////////////////
// TASWShutdownConfig
//
// The line a text logger writes when it shuts down (TASWLogConfig::Shutdown).
/////////////////////////////////////////////////////////////////////////////
struct TASWShutdownConfig
{
    bool WriteLine = true; // "Logger shutdown: <time>", followed by the banner if set
    std::string Banner;
};


/////////////////////////////////////////////////////////////////////////////
// TASWStartupConfig
//
// The lines a text logger writes when Initialize() succeeds (TASWLogConfig::Startup).
/////////////////////////////////////////////////////////////////////////////
struct TASWStartupConfig
{
    std::string Banner; // Written first, if set
    bool WriteApplicationInfo  = true;
    bool WriteCommandLine      = false; // Added to the application info line
    bool WriteDriveInfo        = true;
    bool WriteMemoryUsage      = true;
    bool WriteOSInfo           = true;
    bool WriteSystemMemoryInfo = true;
    bool WriteTimeInfo         = true;
};


/////////////////////////////////////////////////////////////////////////////
// TASWLogConfig
//
// A logger's settings, passed to IASWLog::Initialize(). The groups hold the settings for one concern each; a logger
// ignores the groups it doesn't use (e.g. a console logger ignores File, and a multi-log uses only
// InitialMinimumLevel, passing the whole config on to its loggers).
/////////////////////////////////////////////////////////////////////////////
struct TASWLogConfig
{
    // Seeds a logger's lock-free runtime level gate at Initialize() time only.
    // Use the logger's SetMinimumLevel()/GetMinimumLevel() to read or change the
    // effective level afterward; this field does not track later changes.
    Level InitialMinimumLevel = Level::Info;

    TASWLineConfig Line;
    TASWStartupConfig Startup;
    TASWShutdownConfig Shutdown;
    TASWFileConfig File;

    // --- Log Entry Callback Options ---
    // Receives the entry's record (with its time and ids) and the line as written. Both are only valid during the call.
    using LogCallback = std::function<void (const TASWLogRecord& record, std::string_view formattedLine)>;
    LogCallback OnLogEntry; // Optional hook invoked after a successful write (e.g. alerting/crash-reporting). Invoked outside the sink's internal lock; exceptions are swallowed.
    Level OnLogEntryMinimumLevel = Level::Error; // Independent threshold gating OnLogEntry (Off = never); unrelated to InitialMinimumLevel or the Force* APIs.

    // --- Error Reporting Options ---
    // Receives the logger's internal failures (e.g. the log file can't be opened, written, flushed or rotated), which
    // would otherwise only show as missing entries. Empty: each report is written to stderr as one line (see
    // TASWLogError::ToString()). Called outside the logger's lock, on a thread that was using the logger (e.g. logging,
    // or calling Flush() or Close()), so it may log to another logger; exceptions are swallowed. Failures caused by the
    // handler itself, on its own thread, aren't reported again, so a handler that logs to the failing logger doesn't
    // recurse. A multi-log passes it on to its loggers, which report their own failures. A *Fmt format error isn't a
    // failure: the entry is written with the error in its line (see RuntimeFormat()).
    using ErrorCallback = std::function<void (const TASWLogError& error)>;
    ErrorCallback OnError;
    // Limits the reports, to OnError or to stderr alike: a logger reports each ErrorKind at most once per interval,
    // and counts the failures it leaves out in its next report of that kind (TASWLogError::SuppressedCount). Measured
    // on the logger's clock (see TASWLogBase::NowUTC()). 0 = report every failure.
    std::chrono::milliseconds ErrorReportInterval{ std::chrono::minutes(1) };
};

} // namespace ASWLog

#endif // #ifndef ASWLog_ConfigH
