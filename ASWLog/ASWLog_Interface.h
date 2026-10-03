/* **************************************************************************
ASWLog_Interface.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

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

#ifndef ASWLog_InterfaceH
#define ASWLog_InterfaceH
//---------------------------------------------------------------------------
#include <concepts>
#include <exception>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWLog_Config.h"
#include "ASWLog_Types.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWRuntimeFormat
//
// A format string known only at run time, made by RuntimeFormat().
/////////////////////////////////////////////////////////////////////////////
struct TASWRuntimeFormat
{
    std::string_view Format;
};

// Passes a format string that isn't a compile-time constant (e.g. one read from a file) to a *Fmt method, which checks
// any other format string against its arguments at compile time. This one is checked when the entry is formatted
// instead: a mismatch logs "[ASWLog format error: <reason>] <format string>" (see TASWFormatString::FormatMessage()).
// Like C++26's std::runtime_format, it holds a view of 'format', so the string must outlive the *Fmt call.
[[nodiscard]] constexpr TASWRuntimeFormat RuntimeFormat(std::string_view format) noexcept
{
    return TASWRuntimeFormat{ format };
}


/////////////////////////////////////////////////////////////////////////////
// TASWFormatString
//
// A format string plus the source location of the call that passed it, taken by the *Fmt methods, where Args are the
// types of the arguments it formats. A compile-time constant format string (e.g. a literal) is checked against them
// at compile time, like std::format's, so LogInfoFmt("{} {}", 1) doesn't compile. A format string known only at run
// time must be wrapped in RuntimeFormat().
//
// C++ doesn't allow a defaulted std::source_location parameter after the arguments' parameter pack, but the defaulted
// argument of these implicit constructors is evaluated at the call site too, so it captures the caller's location.
/////////////////////////////////////////////////////////////////////////////
template<typename ... Args>
struct TASWFormatString
{
    std::string_view Format;
    std::source_location Location;

    template<typename T>
    requires std::convertible_to<const T&, std::string_view>
    consteval TASWFormatString(const T& format, std::source_location location = std::source_location::current()) noexcept
        : Format(format),
          Location(location)
    {
        // std::format_string's constructor fails to compile if the format string doesn't match Args
        [[maybe_unused]] const std::format_string<Args...> checkedFormat(Format);
    }

    TASWFormatString(TASWRuntimeFormat format, std::source_location location = std::source_location::current()) noexcept
        : Format(format.Format),
          Location(location)
    {
    }

    // Formats the message from 'args'. Never throws: if formatting fails (e.g. a RuntimeFormat() string doesn't match
    // the arguments, or a formatter throws), returns "[ASWLog format error: <reason>] <format string>" instead, so the
    // entry is still logged.
    [[nodiscard]] std::string FormatMessage(Args&... args) const noexcept
    {
        try
        {
            return std::vformat(Format, std::make_format_args(args ...));
        }
        catch (const std::exception& error)
        {
            return DescribeFormatError(error.what());
        }
        catch (...)
        {
            return DescribeFormatError("unknown exception");
        }
    }

private:
    [[nodiscard]] std::string DescribeFormatError(std::string_view reason) const noexcept
    {
        try
        {
            return std::string("[ASWLog format error: ").append(reason).append("] ").append(Format);
        }
        catch (...)
        {
            return {}; // Out of memory: log an empty entry rather than throw
        }
    }
};


/////////////////////////////////////////////////////////////////////////////
// IASWLog
//
// Interface for the logger. Every logging method ends in Write(), the one
// method a logger implements to receive entries (a logger deriving from
// TASWLogBase implements its WriteRecord() hook instead).
//
// Logging never throws into the application: every method except
// GetFullVersionStr() is noexcept, and failures are reported by a false
// result or by dropping the entry.
/////////////////////////////////////////////////////////////////////////////
class IASWLog
{
private:
    [[nodiscard]] static TASWLogRecord MakeRecord(Level level, std::string_view message, std::source_location loc, bool raw,
        bool forced) noexcept
    {
        TASWLogRecord record;
        record.LogLevel = level;
        record.Message = message;
        record.Location = loc;
        record.Raw = raw;
        record.Forced = forced;
        return record;
    }

public:
    virtual ~IASWLog() = default;

    virtual std::string_view GetVersionStr() const noexcept = 0;
    virtual std::string GetFullVersionStr() const = 0;

    virtual TASWLogConfig& GetConfig() noexcept = 0;
    virtual const TASWLogConfig& GetConfig() const noexcept = 0;

    // Initialize(), Open() and Close() return false if they fail, including on an unexpected exception.
    virtual bool Initialize(const TASWLogConfig& config) noexcept = 0;

    virtual bool Open() noexcept = 0;
    // Closes the output (e.g. the log file). This doesn't always stop logging: a file logger with
    // TASWFileConfig::AutoOpenClosePerWrite reopens its file for the next entry. Use SetEnabled(false) to stop logging.
    virtual bool Close() noexcept = 0;
    // Pushes the entries written so far out of the logger's buffers (e.g. a file's buffer to the operating system), for
    // use before a risky operation or with FlushMode::Manual. Works while disabled, since it writes no new entries.
    // Returns false if the output isn't open or the flush failed (a multi-log: if any of its loggers' flushes failed).
    virtual bool Flush() noexcept = 0;
    virtual bool IsOpen() const noexcept = 0;

    // Whether the logger writes anything at all. While disabled, nothing is written, not even LogForce*() entries or
    // the startup and shutdown lines, but the output stays open and the minimum level is kept. Enabled by default.
    // Lock-free, so it can be changed from any thread at any time.
    virtual bool IsEnabled() const noexcept = 0;
    virtual void SetEnabled(bool enabled) noexcept = 0;

    // The level an entry needs to be written by Log()/LogRaw() (LogForce*() ignores it). Seeded by Initialize() from
    // TASWLogConfig::InitialMinimumLevel. Lock-free, so it can be changed from any thread at any time.
    virtual Level GetMinimumLevel() const noexcept = 0;
    virtual void SetMinimumLevel(Level level) noexcept = 0;

    // True if Log()/LogRaw() would write an entry at 'level': the logger is enabled, 'level' isn't Off, and it meets
    // the minimum level (a multi-log also needs one of its loggers to accept it). The *Fmt methods use it to skip
    // formatting an entry that wouldn't be written; use it the same way to skip building an expensive message.
    virtual bool ShouldLog(Level level) const noexcept = 0;

    // Receives every entry: from the logging methods below, or passed on by another logger (e.g. a multi-log). Writes
    // it unless the logger is disabled, its level is Off, or it is below the minimum level and not record.Forced. A
    // logger that writes it first fills in the record's Timestamp, ProcessId and ThreadId if they are still zero, on
    // the calling thread (see TASWLogRecord). An entry that can't be written (e.g. out of memory) is dropped.
    virtual void Write(const TASWLogRecord& record) noexcept = 0;

    // --- Non-virtual Inline Logging Methods ---
    // Each passes a record with the caller's source location to Write(). LogRaw() writes the message as is, without the
    // line layout or a line ending (e.g. a multi-line HTTP body); LogForce() writes it whatever the minimum level.
    inline void Log(Level level, std::string_view msg, std::source_location loc = std::source_location::current()) noexcept
    {
        Write(MakeRecord(level, msg, loc, false, false));
    }

    inline void LogRaw(Level level, std::string_view msg, std::source_location loc = std::source_location::current()) noexcept
    {
        Write(MakeRecord(level, msg, loc, true, false));
    }

    inline void LogForce(Level level, std::string_view msg, std::source_location loc = std::source_location::current()) noexcept
    {
        Write(MakeRecord(level, msg, loc, false, true));
    }

    inline void LogForceRaw(Level level, std::string_view msg, std::source_location loc = std::source_location::current()) noexcept
    {
        Write(MakeRecord(level, msg, loc, true, true));
    }

    inline void LogTrace(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept
    {
        Write(MakeRecord(Level::Trace, msg, loc, false, false));
    }

    inline void LogDebug(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept
    {
        Write(MakeRecord(Level::Debug, msg, loc, false, false));
    }

    inline void LogInfo(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept
    {
        Write(MakeRecord(Level::Info, msg, loc, false, false));
    }

    inline void LogWarn(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept
    {
        Write(MakeRecord(Level::Warn, msg, loc, false, false));
    }

    inline void LogError(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept
    {
        Write(MakeRecord(Level::Error, msg, loc, false, false));
    }

    inline void LogCritical(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept
    {
        Write(MakeRecord(Level::Critical, msg, loc, false, false));
    }

    // --- Non-virtual Inline Template Format Methods ---
    // Each passes on the caller's source location, captured by TASWFormatString. The format string is checked against
    // the arguments at compile time. Use RuntimeFormat() to wrap run time format strings. A formatting error at run
    // time is logged in place of the message instead of thrown (see TASWFormatString::FormatMessage()). An entry that
    // wouldn't be written isn't formatted: see ShouldLog(), and, for the LogForce*Fmt() methods, IsEnabled() and
    // Level::Off. The type_identity_t keeps 'fmt' from taking part in deducing Args, like std::format's parameter.
    template<typename ... Args>
    inline void LogFmt(Level level, TASWFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        if (ShouldLog(level))
            Log(level, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogRawFmt(Level level, TASWFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        if (ShouldLog(level))
            LogRaw(level, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogForceFmt(Level level, TASWFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        if (level != Level::Off && IsEnabled())
            LogForce(level, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogForceRawFmt(Level level, TASWFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        if (level != Level::Off && IsEnabled())
            LogForceRaw(level, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogTraceFmt(TASWFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        LogFmt(Level::Trace, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogDebugFmt(TASWFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        LogFmt(Level::Debug, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogInfoFmt(TASWFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        LogFmt(Level::Info, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogWarnFmt(TASWFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        LogFmt(Level::Warn, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogErrorFmt(TASWFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        LogFmt(Level::Error, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogCriticalFmt(TASWFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        LogFmt(Level::Critical, fmt, std::forward<Args>(args) ...);
    }
};

} // namespace ASWLog

#endif // ASWLog_InterfaceH
