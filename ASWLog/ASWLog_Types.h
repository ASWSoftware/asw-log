/* **************************************************************************
ASWLog_Types.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Source for the ASWLog types.

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

#ifndef ASWLog_TypesH
#define ASWLog_TypesH
//---------------------------------------------------------------------------
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWLog_Fields.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------

/*
  AsyncOverflowPolicy enum

  What an asynchronous logger does with a new entry while its queue is full (see TASWAsyncConfig).
*/
enum class AsyncOverflowPolicy
{
    // The logging call waits until the queue has room, so no entry is lost.
    Block,

    // The entry is dropped, unless it is at or above TASWAsyncConfig::WaitAtLevel (it then waits for room). The logger
    // reports the drops (ErrorKind::EntriesDropped) and writes a line saying how many were dropped where they would
    // have been.
    DropNewest,
};

[[nodiscard]] std::optional<AsyncOverflowPolicy> AsyncOverflowPolicy_FromString(std::string_view str) noexcept;

[[nodiscard]] constexpr std::string_view AsyncOverflowPolicy_ToString(AsyncOverflowPolicy policy) noexcept
{
    switch (policy)
    {
        case AsyncOverflowPolicy::Block:
            return "BLOCK";

        case AsyncOverflowPolicy::DropNewest:
            return "DROP_NEWEST";
    }

    return "UNKNOWN";
}

//---------------------------------------------------------------------------

/*
  ColorMode enum

  Whether TASWConsoleLog wraps its lines in ANSI color codes.
*/
enum class ColorMode
{
    // Color a stream (stdout or stderr) only if it's a console or terminal that shows colors (not a file or a pipe),
    // and the NO_COLOR environment variable isn't set (see https://no-color.org).
    Auto,

    // Always write color codes, e.g. for a viewer that shows them although it isn't detected as a terminal.
    Always,

    // Never write color codes.
    Never,
};

[[nodiscard]] std::optional<ColorMode> ColorMode_FromString(std::string_view str) noexcept;

[[nodiscard]] constexpr std::string_view ColorMode_ToString(ColorMode colorMode) noexcept
{
    switch (colorMode)
    {
        case ColorMode::Auto:
            return "AUTO";

        case ColorMode::Always:
            return "ALWAYS";

        case ColorMode::Never:
            return "NEVER";
    }

    return "UNKNOWN";
}

//---------------------------------------------------------------------------

/*
  ErrorKind enum

  What failed, in a TASWLogError reported to TASWLogConfig::OnError.
*/
enum class ErrorKind
{
    // The log file couldn't be opened (or its folder created), so entries are dropped until it can be.
    OpenFailed,

    // An entry couldn't be written to the output (e.g. the disk is full).
    WriteFailed,

    // The output's buffer couldn't be written out (e.g. the disk is full).
    FlushFailed,

    // The log file couldn't be synced to disk (see TASWFileConfig::SyncToDiskAtLevel), so entries may be lost if the
    // system stops (e.g. a power loss).
    SyncFailed,

    // The log file couldn't be closed cleanly; entries still in its buffer may be lost.
    CloseFailed,

    // The log file couldn't be renamed to a backup (e.g. another program holds it), so it grows on.
    RotationFailed,

    // Backup cleanup couldn't delete an old backup, or list the folder that holds them.
    DeleteFailed,

    // An unexpected exception (e.g. out of memory, or a formatter that throws) dropped an entry or the startup lines.
    Exception,

    // An asynchronous logger's queue was full, so entries were dropped (see AsyncOverflowPolicy::DropNewest). The
    // message says how many.
    EntriesDropped,
};

// Number of error kinds, e.g. for a table indexed by kind. Update this when a kind is added or removed.
constexpr std::size_t ErrorKindCount = 9;

[[nodiscard]] constexpr std::string_view ErrorKind_ToString(ErrorKind errorKind) noexcept
{
    switch (errorKind)
    {
        case ErrorKind::OpenFailed:
            return "OPEN_FAILED";

        case ErrorKind::WriteFailed:
            return "WRITE_FAILED";

        case ErrorKind::FlushFailed:
            return "FLUSH_FAILED";

        case ErrorKind::SyncFailed:
            return "SYNC_FAILED";

        case ErrorKind::CloseFailed:
            return "CLOSE_FAILED";

        case ErrorKind::RotationFailed:
            return "ROTATION_FAILED";

        case ErrorKind::DeleteFailed:
            return "DELETE_FAILED";

        case ErrorKind::Exception:
            return "EXCEPTION";

        case ErrorKind::EntriesDropped:
            return "ENTRIES_DROPPED";
    }

    return "UNKNOWN";
}

//---------------------------------------------------------------------------

enum class FlushMode
{
    EveryWrite,
    OnNewLine,
    Manual,
    Periodic,
};

[[nodiscard]] std::optional<FlushMode> FlushMode_FromString(std::string_view str) noexcept;

[[nodiscard]] constexpr std::string_view FlushMode_ToString(FlushMode flushMode) noexcept
{
    switch (flushMode)
    {
        case FlushMode::EveryWrite:
            return "EVERY_WRITE";

        case FlushMode::OnNewLine:
            return "ON_NEW_LINE";

        case FlushMode::Manual:
            return "MANUAL";

        case FlushMode::Periodic:
            return "PERIODIC";
    }

    return "UNKNOWN";
}

//---------------------------------------------------------------------------

/*
  Level enum

  Defines the severity thresholds for the logging system.

  Log levels are ordered by increasing severity.
  Filtering logic logs messages where: message_level >= logger_configured_minimum_level.
*/
enum class Level : std::uint8_t
{
    // Use for granular, step-by-step program execution details for low-level diagnostics, etc.
    // Extremely high volume.
    Trace = 0,

    // Use for diagnosing issues, verifying configurations, or tracking system variables.
    Debug = 1,

    // Use to track normal operational health (e.g., service start/stop, user login).
    // Should be used as the standard, safe default runtime level for production environments.
    Info = 2,

    // Use for unexpected or unusual anomalies that do not break current execution flow.
    // The system can self-recover, but the event warrants attention from operations.
    // Examples include network retries, high resource usage, or deprecated API usage.
    Warn = 3,

    // Use for severe operational failures preventing a specific task or request from completing.
    // Requires manual intervention or a code patch to resolve, but the application remains alive.
    // Examples include database connection failures, missing critical files, or caught exceptions.
    Error = 4,

    // Use for catastrophic, unrecoverable system crashes or massive data corruption events.
    // The application is unable to continue safely and will usually abort immediately.
    // Examples include out-of-memory states, hardware faults, or failed sanity checks.
    Critical = 5,

    // Not a severity. As a minimum level (SetMinimumLevel(), InitialMinimumLevel, OnLogEntryMinimumLevel), turns off
    // everything except LogForce()/LogForceRaw(), which ignore the minimum level. A message logged at Off is never
    // written, even when forced.
    Off = 6,
};

// Number of severity levels (Trace..Critical), e.g. for a table indexed by level. Doesn't count Off. Update this when
// a severity level is added or removed.
constexpr std::size_t LevelCount = 6;

[[nodiscard]] std::optional<Level> Level_FromString(std::string_view str) noexcept;

[[nodiscard]] constexpr std::string_view Level_ToString(Level level) noexcept
{
    switch (level)
    {
        case Level::Trace:
            return "TRACE";

        case Level::Debug:
            return "DEBUG";

        case Level::Info:
            return "INFO";

        case Level::Warn:
            return "WARN";

        case Level::Error:
            return "ERROR";

        case Level::Critical:
            return "CRITICAL";

        case Level::Off:
            return "OFF";
    }

    return "UNKNOWN";
}

//---------------------------------------------------------------------------

enum class LineEnding
{
    LF,
    CRLF,
};

[[nodiscard]] std::optional<LineEnding> LineEnding_FromString(std::string_view str) noexcept;

[[nodiscard]] constexpr std::string_view LineEnding_ToString(LineEnding lineEnding) noexcept
{
    switch (lineEnding)
    {
        case LineEnding::LF:
            return "LF";

        case LineEnding::CRLF:
            return "CRLF";
    }

    return "UNKNOWN";
}

//---------------------------------------------------------------------------

// How a text logger writes the line breaks inside an entry's line, e.g. of a message holding an HTTP body (see
// TASWLineConfig::Multiline)
enum class MultilineMode
{
    Preserve, // As they are, so a multi-line message spans several lines of the log
    Indent,   // Each "\n" or "\r\n" as the line ending followed by "    | ", so each line after the first is marked
    Escape,   // Each CR and LF as the two characters \r and \n, so the entry stays on one line
};

[[nodiscard]] std::optional<MultilineMode> MultilineMode_FromString(std::string_view str) noexcept;

[[nodiscard]] constexpr std::string_view MultilineMode_ToString(MultilineMode mode) noexcept
{
    switch (mode)
    {
        case MultilineMode::Preserve:
            return "PRESERVE";

        case MultilineMode::Indent:
            return "INDENT";

        case MultilineMode::Escape:
            return "ESCAPE";
    }

    return "UNKNOWN";
}

//---------------------------------------------------------------------------

// How many digits of the second a timestamp shows (see TASWLineConfig::TimestampPrecision). The digits past the system
// clock's resolution are 0: with nanoseconds, the last two with MSVC and MinGW on Windows (100 ns), the last three with
// libc++ (1 us, e.g. RAD Studio).
enum class TimePrecision
{
    Milliseconds, // .mmm
    Microseconds, // .uuuuuu
    Nanoseconds,  // .nnnnnnnnn
};

[[nodiscard]] std::optional<TimePrecision> TimePrecision_FromString(std::string_view str) noexcept;

[[nodiscard]] constexpr std::string_view TimePrecision_ToString(TimePrecision precision) noexcept
{
    switch (precision)
    {
        case TimePrecision::Milliseconds:
            return "MILLISECONDS";

        case TimePrecision::Microseconds:
            return "MICROSECONDS";

        case TimePrecision::Nanoseconds:
            return "NANOSECONDS";
    }

    return "UNKNOWN";
}

//---------------------------------------------------------------------------

// The clock a timestamp is shown in (see TASWLineConfig::TimestampZone)
enum class TimeZone
{
    UTC,   // "Z" suffix, e.g. 2026-09-28T21:02:44.342Z
    Local, // The local time zone, with its offset from UTC, e.g. 2026-09-28T16:02:44.342-05:00
};

[[nodiscard]] std::optional<TimeZone> TimeZone_FromString(std::string_view str) noexcept;

[[nodiscard]] constexpr std::string_view TimeZone_ToString(TimeZone zone) noexcept
{
    switch (zone)
    {
        case TimeZone::UTC:
            return "UTC";

        case TimeZone::Local:
            return "LOCAL";
    }

    return "UNKNOWN";
}

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWLogError struct
//
// One of a logger's internal failures, as reported to TASWLogConfig::OnError.
/////////////////////////////////////////////////////////////////////////////
struct TASWLogError
{
    ErrorKind Kind = ErrorKind::Exception;
    std::string Message; // What failed, e.g. "Couldn't open the log file"
    std::filesystem::path Path; // The file or folder involved, if any
    std::error_code Code; // The operating system's or library's error, if known
    // How many failures of this kind the logger left unreported since its previous report of this kind (see
    // TASWLogConfig::ErrorReportInterval)
    std::size_t SuppressedCount = 0;

    // One line describing the failure: "<KIND>: <Message> '<Path>': <Code's message> (<N> more not reported)", leaving
    // out the parts that are empty. Can throw (e.g. out of memory).
    [[nodiscard]] std::string ToString() const;
};

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWLogRecord struct
//
// The data of one log entry, as passed to IASWLog::Write(), a formatter (see IASWLogFormatter) and the OnLogEntry
// callback. The Log* methods fill in the level, message, location and flags. Timestamp, ProcessId and ThreadId stay
// zero until a logger knows it will write the entry: TASWLogBase::Write() then fills in those still zero, on the
// calling thread before taking any lock, so they record the moment and thread of the call. A record passed on (e.g.
// by a multi-log to its loggers) keeps them. A category logger (see TASWCategoryLog) fills in Category and, if it has
// a level of its own, CategoryLevel.
//
// Message, Category, Location, Fields and Scope refer to the caller's data, so they are only valid during the call; a
// logger, formatter or callback that keeps a record beyond it must copy them (the fields with ForEachField(), since the
// scope is the thread's).
/////////////////////////////////////////////////////////////////////////////
struct TASWLogRecord
{
    // The small fields are together, so a logging call, which builds a record even when its logger filters the entry
    // out, fills them in with few stores
    std::chrono::system_clock::time_point Timestamp; // UTC (a system_clock time point counts from the UTC epoch)
    Level LogLevel = Level::Info;
    // The category's own level, which the logger receiving the entry uses in place of its minimum level; empty if the
    // category has none, or there is no category (see TASWCategoryLog::SetMinimumLevel()). A multi-log uses it for its
    // own check and empties it before passing the entry on, since its loggers apply their own minimum levels.
    std::optional<Level> CategoryLevel;
    // Written as is, without the line layout or a line ending (LogRaw(), LogForceRaw()), unless the formatter formats
    // it (see IASWLogFormatter::FormatsRawEntries())
    bool Raw = false;
    bool Forced = false; // Written whatever the minimum level (LogForce(), LogForceRaw())
    std::string_view Message;
    std::string_view Category; // The category's name, e.g. "Net" or "Net.Http"; empty if none (see TASWCategoryLog)
    std::source_location Location; // Where the entry was logged
    std::uint32_t ProcessId = 0; // The OS process id (see GetCurrentOSProcessId())
    std::uint32_t ThreadId = 0; // The OS id of the thread that logged the entry (see GetCurrentOSThreadId())
    // The entry's own fields, from the logging call (e.g. LogInfo("Order placed", {{"orderId", 17}})), or null. A
    // pointer to the list rather than the list itself, because every logging call builds a record, even when the entry
    // is filtered out, and a 16-byte member measurably slowed that down.
    const std::span<const TASWLogField>* Fields = nullptr;
    // The innermost scope of the thread that logged the entry (see TASWLogScope), filled in by TASWLogBase::Write()
    // like the Timestamp, if still null; null if the thread has none
    const TASWLogScope* Scope = nullptr;

    // Calls visit(field) for each of the entry's fields, its scopes' and its own, each key once, the innermost value
    // winning (see ForEachLogField()). Allocates nothing.
    template<typename TVisit>
    void ForEachField(TVisit&& visit) const
    {
        ForEachLogField(Scope, GetOwnFields(), visit);
    }

    // The entry's own fields (see Fields), which may be empty
    [[nodiscard]] std::span<const TASWLogField> GetOwnFields() const noexcept
    {
        return Fields != nullptr ? *Fields : std::span<const TASWLogField>();
    }

    // True if the entry may have fields (its own, or a scope; a scope may have none)
    [[nodiscard]] bool HasFields() const noexcept
    {
        return (Fields != nullptr && !Fields->empty()) || Scope != nullptr;
    }
};

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWPendingEntry
//
// An entry about to be written, as TASWLogConfig::OnBeforeWrite gets it: the hook reads the record and may change its
// message and fields before the logger writes it. The entry owns what the hook sets, so the hook's own strings may be
// temporaries. Its level, time, ids, category, location and flags can't be changed (the logger has already checked
// the level). Only valid during the hook's call.
/////////////////////////////////////////////////////////////////////////////
class TASWPendingEntry
{
private:
    TASWLogRecord m_Record; // Its Message and Fields refer to the copies below once the hook has set them
    std::string m_Message;
    Detail::TOwnedFields m_Fields; // After a field change: all the entry's fields, each key once
    std::span<const TASWLogField> m_FieldsView; // What m_Record.Fields points to after a field change
    std::vector<Detail::TOwnedFields> m_ReplacedFields; // Kept, so a ForEachField() under way can finish

private:
    void ReplaceFields(std::span<const TASWLogField> fields);

public:
    explicit TASWPendingEntry(const TASWLogRecord& record) noexcept;

    TASWPendingEntry(const TASWPendingEntry&) = delete;
    TASWPendingEntry& operator=(const TASWPendingEntry&) = delete;

    // The value of the entry's field 'key' (its own, or its scopes'; see TASWLogRecord::ForEachField()), or null if it
    // has none. Valid until the hook changes the fields or returns.
    [[nodiscard]] const TASWLogValue* FindField(std::string_view key) const noexcept;

    // Calls visit(field) for each of the entry's fields, each key once (see TASWLogRecord::ForEachField()). The hook
    // may change the fields from inside visit; the visit then goes on over the fields as they were when it started.
    template<typename TVisit>
    void ForEachField(TVisit&& visit) const
    {
        m_Record.ForEachField(visit);
    }

    // The entry as it will be written, with the hook's changes so far
    [[nodiscard]] const TASWLogRecord& GetRecord() const noexcept;

    // Removes the field 'key' (its own, or a scope's, for this entry only), if it has one. Throws std::bad_alloc if
    // the fields can't be copied; they are then unchanged.
    void RemoveField(std::string_view key);

    // Sets the field 'key' to 'value' (copied, text included): replaces its value where it is, or adds it after the
    // others. Throws like RemoveField().
    void SetField(std::string_view key, const TASWLogValue& value);

    // Replaces the entry's message, e.g. with a redacted copy
    void SetMessage(std::string message) noexcept;
};

//---------------------------------------------------------------------------

} // namespace ASWLog

#endif // #ifndef ASWLog_TypesH
