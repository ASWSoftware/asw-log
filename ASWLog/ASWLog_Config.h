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
// TASWAsyncConfig
//
// Asynchronous writing (TASWLogConfig::Async), for a text logger (file, console). Off by default: a synchronous logger
// has written each entry when the call returns, which is the safest for finding the cause of a crash. With it on, the
// logging call formats the entry and queues it, and a thread of the logger's own writes the queued entries in order,
// so the call doesn't wait for the output. Entries still queued when the application crashes are lost, except those
// at or above WaitAtLevel. Flush(), Close(), Reconfigure() and the logger's destructor first wait for the queued
// entries to be written. OnLogEntry is called on the logger's thread, after the entry was written. It pays off when
// writing is slow (e.g. File.Flush = EveryWrite, the default: a call then costs about as much as formatting its line);
// with buffered output (e.g. FlushMode::Manual), queuing an entry can cost the caller a little more than writing it.
/////////////////////////////////////////////////////////////////////////////
struct TASWAsyncConfig
{
    bool Enabled = false;
    // How many entries can wait in the queue. When it is full, OverflowPolicy decides.
    std::size_t QueueCapacity = 8192;
    AsyncOverflowPolicy OverflowPolicy = AsyncOverflowPolicy::Block;
    // The call of an entry at or above this level returns only once the entry, and every entry queued before it, has
    // been written and flushed (whatever File.Flush says; File.SyncToDiskAtLevel still syncs it), so it is in the file
    // if the application crashes right after. Such an entry is never dropped: it waits for room in the queue.
    // Level::Off = no entry waits.
    Level WaitAtLevel = Level::Error;
};


/////////////////////////////////////////////////////////////////////////////
// TASWBacktraceConfig
//
// A backtrace (TASWLogConfig::Backtrace): the logger keeps its most recent entries below its minimum level, at or
// above LowestLevel, in memory, and writes them when an entry at or above DumpAtLevel is written (just before that
// entry), when DumpBacktrace() is called, or when the application crashes (see InstallCrashHandlers()), then forgets
// them. So a log written at Info still shows the Debug and Trace entries that led up to an error. The kept entries are
// written between two Info lines, "Backtrace: the last N entries below the minimum level" and "Backtrace end", with
// their own time, level, thread and location. Off by default (Capacity 0). Keeping an entry costs about as much as
// copying its message, so the *Fmt methods also format the entries at the kept levels (see IASWLog::ShouldLog()).
//
// Each logger keeps its own backtrace, of what is below its own minimum level. A multi-log keeps none and filters with
// its own minimum level first: to give its loggers' backtraces the lower levels, set its minimum level to LowestLevel
// or lower.
/////////////////////////////////////////////////////////////////////////////
struct TASWBacktraceConfig
{
    // How many entries are kept; once full, a new one replaces the oldest. 0 = no backtrace.
    std::size_t Capacity = 0;
    Level LowestLevel = Level::Trace; // Entries below it aren't kept. Level::Off = no backtrace.
    // Writing an entry at or above this level (forced and raw entries too) writes the backtrace first. Level::Off =
    // only DumpBacktrace() and a crash write it.
    Level DumpAtLevel = Level::Error;
};


/////////////////////////////////////////////////////////////////////////////
// TASWBackupInfo
//
// A backup a file logger made by rotating its log, as passed to TASWFileConfig::OnBackupCreated.
/////////////////////////////////////////////////////////////////////////////
struct TASWBackupInfo
{
    std::filesystem::path LogPath; // The log file that was rotated
    std::filesystem::path BackupPath; // What it was renamed to: "<stem>.<reason>.<time>.bak"
    std::string Reason; // "size", "daily", or the reason tag given to TASWFileLog::RotateLogFiles()
};


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
    // FlushMode::Periodic: a thread of the logger's own flushes the file this often, also when nothing more is logged.
    // 0 or less = every entry is flushed, as with EveryWrite. Not used with AutoOpenClosePerWrite, which closes (and so
    // flushes) the file after every entry.
    std::chrono::milliseconds FlushInterval{ 1000 };
    // An entry at or above this level (raw ones too) is flushed as soon as it is written, whatever Flush says, so it
    // is in the file if the application crashes right after. Level::Off = only Flush decides.
    Level FlushImmediatelyAtLevel = Level::Error;
    // An entry at or above this level is flushed and then synced to disk (FlushFileBuffers on Windows, fsync on POSIX),
    // so it survives a system crash or power loss too. A sync often takes milliseconds, so keep this level for rare
    // entries. Level::Off = never synced.
    Level SyncToDiskAtLevel = Level::Off;

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

    // --- Backup Event ---
    // Called once for each backup that rotation makes, e.g. to compress it, upload it or move it elsewhere. Called
    // outside the logger's lock, on the thread whose call rotated the log (a logging call, Initialize(), Reconfigure()
    // or RotateLogFiles(); with Async.Enabled, the logger's own thread writes the entries and so rotates the log), so
    // that call waits for it: hand slow work to another thread. Exceptions are swallowed. The
    // backup cleanup below runs after it, so the backup still exists during the call. A backup it renames out of the
    // "<stem>.<reason>.<time>.bak" form (e.g. to ".bak.gz") is the application's to clean up from then on. With
    // AutoOpenClosePerWrite, only the process that rotated the shared log calls it. Not called if the rotation failed
    // (see TASWLogConfig::OnError).
    using BackupCallback = std::function<void (const TASWBackupInfo& backup)>;
    BackupCallback OnBackupCreated;

    // --- Backup Cleanup Options (applied after each successful rotation, after OnBackupCreated) ---
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
// The lines a text logger writes when it shuts down, or when the application crashes (TASWLogConfig::Shutdown).
/////////////////////////////////////////////////////////////////////////////
struct TASWShutdownConfig
{
    bool WriteLine = true; // "Logger shutdown: <time>", followed by the banner if set
    std::string Banner;
    // A Critical "Crash: <reason>" line when the application crashes (see InstallCrashHandlers() and HandleCrash()). The
    // logger is flushed on a crash either way.
    bool WriteCrashLine = true;
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
    TASWAsyncConfig Async;
    TASWBacktraceConfig Backtrace;

    // --- Log Entry Callback Options ---
    // Receives the entry's record (with its time and ids) and the line as written. Both are only valid during the call.
    using LogCallback = std::function<void (const TASWLogRecord& record, std::string_view formattedLine)>;
    LogCallback OnLogEntry; // Optional hook invoked after a successful write (e.g. alerting/crash-reporting). Invoked outside the sink's internal lock; exceptions are swallowed. With Async.Enabled, invoked on the logger's own thread.
    Level OnLogEntryMinimumLevel = Level::Error; // Independent threshold gating OnLogEntry (Off = never); unrelated to InitialMinimumLevel or the Force* APIs.

    // --- Error Reporting Options ---
    // Receives the logger's internal failures (e.g. the log file can't be opened, written, flushed or rotated), which
    // would otherwise only show as missing entries. Empty: each report is written to stderr as one line (see
    // TASWLogError::ToString()). Called outside the logger's lock, on a thread that was using the logger (e.g. logging,
    // or calling Flush() or Close()) or on the logger's own thread (e.g. a failed FlushMode::Periodic flush), so it may
    // log to another logger; exceptions are swallowed. Failures caused by the handler itself, on its own thread, aren't
    // reported again, so a handler that logs to the failing logger doesn't recurse. A multi-log passes it on to its
    // loggers, which report their own failures. A *Fmt format error isn't a failure: the entry is written with the
    // error in its line (see RuntimeFormat()).
    using ErrorCallback = std::function<void (const TASWLogError& error)>;
    ErrorCallback OnError;
    // Limits the reports, to OnError or to stderr alike: a logger reports each ErrorKind at most once per interval,
    // and counts the failures it leaves out in its next report of that kind (TASWLogError::SuppressedCount). Measured
    // on the logger's clock (see TASWLogBase::NowUTC()). 0 = report every failure.
    std::chrono::milliseconds ErrorReportInterval{ std::chrono::minutes(1) };
};

} // namespace ASWLog

#endif // #ifndef ASWLog_ConfigH
