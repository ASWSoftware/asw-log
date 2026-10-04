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
#include <string>
#include <string_view>
#include <system_error>
//---------------------------------------------------------------------------

namespace ASWLog
{

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

    // The log file couldn't be closed cleanly; entries still in its buffer may be lost.
    CloseFailed,

    // The log file couldn't be renamed to a backup (e.g. another program holds it), so it grows on.
    RotationFailed,

    // Retention couldn't delete an old backup, or list the folder that holds them.
    DeleteFailed,

    // An unexpected exception (e.g. out of memory, or a formatter that throws) dropped an entry or the startup lines.
    Exception,
};

// Number of error kinds, e.g. for a table indexed by kind. Update this when a kind is added or removed.
constexpr std::size_t ErrorKindCount = 7;

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

        case ErrorKind::CloseFailed:
            return "CLOSE_FAILED";

        case ErrorKind::RotationFailed:
            return "ROTATION_FAILED";

        case ErrorKind::DeleteFailed:
            return "DELETE_FAILED";

        case ErrorKind::Exception:
            return "EXCEPTION";
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
// by a multi-log to its loggers) keeps them.
//
// Message and Location refer to the caller's data, so they are only valid during the call; a logger, formatter or
// callback that keeps a record beyond it must copy them.
/////////////////////////////////////////////////////////////////////////////
struct TASWLogRecord
{
    std::chrono::system_clock::time_point Timestamp; // UTC (a system_clock time point counts from the UTC epoch)
    Level LogLevel = Level::Info;
    std::string_view Message;
    std::source_location Location; // Where the entry was logged
    std::uint32_t ProcessId = 0; // The OS process id (see GetCurrentOSProcessId())
    std::uint32_t ThreadId = 0; // The OS id of the thread that logged the entry (see GetCurrentOSThreadId())
    bool Raw = false; // Written as is, without the line layout or a line ending (LogRaw(), LogForceRaw())
    bool Forced = false; // Written whatever the minimum level (LogForce(), LogForceRaw())
};

//---------------------------------------------------------------------------

} // namespace ASWLog

#endif // #ifndef ASWLog_TypesH
