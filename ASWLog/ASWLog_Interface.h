/* **************************************************************************
ASWLog_Interface.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

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

#ifndef ASWLog_InterfaceH
#define ASWLog_InterfaceH
//---------------------------------------------------------------------------
#include <concepts>
#include <exception>
#include <format>
#include <initializer_list>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWLog_Config.h"
#include "ASWLog_Types.h"
#include "ASWLog_Unicode.h"
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

// A wide format string known only at run time, made by RuntimeFormat()
struct TASWWideRuntimeFormat
{
    std::wstring_view Format;
};

// Passes a format string that isn't a compile-time constant (e.g. one read from a file) to a *Fmt method, which checks
// any other format string against its arguments at compile time. This one is checked when the entry is formatted
// instead: a mismatch logs "[ASWLog format error: <reason>] <format string>" (see TASWFormatString::FormatMessage()),
// which isn't reported to TASWLogConfig::OnError, since the entry is still written. Like C++26's std::runtime_format,
// it holds a view of 'format', so the string must outlive the *Fmt call. Also for a wide format string.
[[nodiscard]] constexpr TASWRuntimeFormat RuntimeFormat(std::string_view format) noexcept
{
    return TASWRuntimeFormat{ format };
}

[[nodiscard]] constexpr TASWWideRuntimeFormat RuntimeFormat(std::wstring_view format) noexcept
{
    return TASWWideRuntimeFormat{ format };
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
// TASWWideFormatString
//
// A wide (wchar_t) format string plus the source location of the call that passed it, taken by the *Fmt methods like
// TASWFormatString: checked against the arguments at compile time, like std::wformat_string's, and formatted with
// std::format's wide functions, then converted to UTF-8. Its arguments must be formattable as wide text: numbers, wide
// strings, ASWLog::Wide() for char16_t text, and ASWLog::UTF8() for UTF-8 text (narrow strings aren't accepted). A
// format string known only at run time must be wrapped in RuntimeFormat().
/////////////////////////////////////////////////////////////////////////////
template<typename ... Args>
struct TASWWideFormatString
{
    std::wstring_view Format;
    std::source_location Location;

    template<typename T>
    requires std::convertible_to<const T&, std::wstring_view>
    consteval TASWWideFormatString(const T& format, std::source_location location = std::source_location::current()) noexcept
        : Format(format),
          Location(location)
    {
        // std::wformat_string's constructor fails to compile if the format string doesn't match Args
        [[maybe_unused]] const std::wformat_string<Args...> checkedFormat(Format);
    }

    TASWWideFormatString(TASWWideRuntimeFormat format, std::source_location location = std::source_location::current()) noexcept
        : Format(format.Format),
          Location(location)
    {
    }

    // Formats the message from 'args', as UTF-8. Never throws: if formatting fails, returns "[ASWLog format error:
    // <reason>] <format string>" instead (see TASWFormatString::FormatMessage()).
    [[nodiscard]] std::string FormatMessage(Args&... args) const noexcept
    {
        try
        {
            return WideToUTF8(std::vformat(Format, std::make_wformat_args(args ...)));
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
            return std::string("[ASWLog format error: ").append(reason).append("] ").append(WideToUTF8(Format));
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
// result or by dropping the entry. A logger deriving from TASWLogBase also
// reports its internal failures to TASWLogConfig::OnError.
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

    // Writes an entry whose message is wide text, converted to UTF-8 only if the logger would use the entry. 'fields'
    // may be null.
    void WriteWide(Level level, TASWWideText message, const std::initializer_list<TASWLogField>* fields,
        std::source_location loc, bool raw, bool forced) noexcept;

    // Writes an entry with fields: the record points to the list, which lives during the call
    void WriteWithFields(Level level, std::string_view message, std::initializer_list<TASWLogField> fields,
        std::source_location loc, bool raw, bool forced) noexcept
    {
        const std::span<const TASWLogField> ownFields(fields.begin(), fields.size());
        TASWLogRecord record = MakeRecord(level, message, loc, raw, forced);
        record.Fields = &ownFields;
        Write(record);
    }

public:
    virtual ~IASWLog() = default;

    virtual std::string_view GetVersionStr() const noexcept = 0;
    virtual std::string GetFullVersionStr() const = 0;

    // The logger's current settings, as an immutable snapshot that stays valid and unchanged after Reconfigure()
    // replaces it. To change a setting, copy it, change the copy and pass it to Reconfigure():
    //     auto config = *logger.GetConfig();
    //     config.Line.ShowThreadId = false;
    //     logger.Reconfigure(config);
    virtual std::shared_ptr<const TASWLogConfig> GetConfig() const noexcept = 0;

    // Initialize(), Reconfigure(), Open() and Close() return false if they fail, including on an unexpected exception.
    virtual bool Initialize(const TASWLogConfig& config) noexcept = 0;
    // Replaces the settings of an initialized logger, safely while other threads log: the entries written after it
    // returns use the new settings. A file logger whose file path or AutoOpenClosePerWrite changed closes the old file
    // and opens the new one. Doesn't change the minimum level (InitialMinimumLevel only seeds it at Initialize(); use
    // SetMinimumLevel()) and doesn't write the startup lines. A multi-log passes the settings on to every logger it
    // holds, like Initialize(). Returns false if the logger isn't initialized (use Initialize()), or if the new
    // settings couldn't be applied, e.g. the new file couldn't be opened: they are kept, and the file logger tries
    // again on later entries (see TASWFileConfig::CircuitBreakerResetDelay).
    virtual bool Reconfigure(const TASWLogConfig& config) noexcept = 0;

    virtual bool Open() noexcept = 0;
    // Closes the output (e.g. the log file). This doesn't always stop logging: a file logger with
    // TASWFileConfig::AutoOpenClosePerWrite reopens its file for the next entry. Use SetEnabled(false) to stop logging.
    virtual bool Close() noexcept = 0;
    // Pushes the entries written so far out of the logger's buffers (e.g. a file's buffer to the operating system), for
    // use before a risky operation or with FlushMode::Manual. Works while disabled, since it writes no new entries.
    // Returns false if the output isn't open or the flush failed (a multi-log: if any of its loggers' flushes failed).
    virtual bool Flush() noexcept = 0;
    virtual bool IsOpen() const noexcept = 0;

    // Writes the backtrace now (the entries kept below the minimum level, see TASWBacktraceConfig) and forgets it, e.g.
    // when the application detects a problem that isn't logged as an error. Does nothing if the backtrace is empty or
    // the logger is disabled. A multi-log passes it on to every logger it holds.
    virtual void DumpBacktrace() noexcept = 0;

    // Whether the logger writes anything at all. While disabled, nothing is written, not even LogForce*() entries or
    // the startup and shutdown lines, but the output stays open and the minimum level is kept. Enabled by default.
    // Lock-free, so it can be changed from any thread at any time.
    virtual bool IsEnabled() const noexcept = 0;
    virtual void SetEnabled(bool enabled) noexcept = 0;

    // The level an entry needs to be written by Log()/LogRaw() (LogForce*() ignores it; a category's own level replaces
    // it, see TASWCategoryLog). Seeded by Initialize() from TASWLogConfig::InitialMinimumLevel. Lock-free, so it can be
    // changed from any thread at any time.
    virtual Level GetMinimumLevel() const noexcept = 0;
    virtual void SetMinimumLevel(Level level) noexcept = 0;

    // True if Log()/LogRaw() would use an entry at 'level': the logger is enabled, 'level' isn't Off, and it meets the
    // minimum level, or the backtrace keeps it (see TASWBacktraceConfig; a multi-log also needs one of its loggers to
    // accept it). The *Fmt methods use it to skip formatting an entry that wouldn't be used; use it the same way to
    // skip building an expensive message.
    virtual bool ShouldLog(Level level) const noexcept = 0;
    // True if Write() would use 'record': like ShouldLog(Level) for its level, with record.CategoryLevel in place of
    // the minimum level if set, and only the enabled and Level::Off checks if record.Forced. A category logger (see
    // TASWCategoryLog) asks the logger it wraps this way.
    virtual bool ShouldLog(const TASWLogRecord& record) const noexcept = 0;

    // Receives every entry: from the logging methods below, or passed on by another logger (e.g. a multi-log). Writes
    // it unless the logger is disabled, its level is Off, or it is below the minimum level (record.CategoryLevel if
    // set) and not record.Forced (the backtrace may keep such an entry, see TASWBacktraceConfig). A logger that writes
    // or keeps it first fills in the record's Timestamp, ProcessId and ThreadId if they are still zero, on the calling
    // thread (see TASWLogRecord). An entry that can't be written (e.g. out of memory) is dropped.
    virtual void Write(const TASWLogRecord& record) noexcept = 0;

    // --- Non-virtual Inline Logging Methods ---
    // Each passes a record with the caller's source location to Write(). LogRaw() writes the message as is, without the
    // line layout or a line ending (e.g. a multi-line HTTP body; a TASWJSONFormatter logger writes it as a JSON object
    // with "raw":true instead); LogForce() writes it whatever the minimum level.
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

    // --- Non-virtual Inline Logging Methods With Fields ---
    // As above, with fields for this entry, e.g. LogInfo("Order placed", {{"orderId", 17}, {"total", 9.99}}). They
    // come after the fields of the thread's scopes (see TASWLogScope) and replace those with the same key. The keys and
    // texts are only read during the call. A text logger writes them before the message (see
    // TASWLineConfig::ShowFields), but not for a LogRaw() entry, which it writes as is.
    inline void Log(Level level, std::string_view msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWithFields(level, msg, fields, loc, false, false);
    }

    inline void LogRaw(Level level, std::string_view msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWithFields(level, msg, fields, loc, true, false);
    }

    inline void LogForce(Level level, std::string_view msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWithFields(level, msg, fields, loc, false, true);
    }

    inline void LogForceRaw(Level level, std::string_view msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWithFields(level, msg, fields, loc, true, true);
    }

    inline void LogTrace(std::string_view msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWithFields(Level::Trace, msg, fields, loc, false, false);
    }

    inline void LogDebug(std::string_view msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWithFields(Level::Debug, msg, fields, loc, false, false);
    }

    inline void LogInfo(std::string_view msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWithFields(Level::Info, msg, fields, loc, false, false);
    }

    inline void LogWarn(std::string_view msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWithFields(Level::Warn, msg, fields, loc, false, false);
    }

    inline void LogError(std::string_view msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWithFields(Level::Error, msg, fields, loc, false, false);
    }

    inline void LogCritical(std::string_view msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWithFields(Level::Critical, msg, fields, loc, false, false);
    }

    // --- Non-virtual Inline Logging Methods With Wide Text ---
    // As above, for a message of wide text: wchar_t (e.g. L"Opened", a std::wstring, or C++Builder's
    // UnicodeString::c_str()) or char16_t (e.g. u"Opened"; see TASWWideText). The message is converted to UTF-8 only if
    // the entry would be used (see ShouldLog()), so a filtered call converts nothing. Field keys and texts stay UTF-8.
    inline void Log(Level level, TASWWideText msg, std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(level, msg, nullptr, loc, false, false);
    }

    inline void LogRaw(Level level, TASWWideText msg, std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(level, msg, nullptr, loc, true, false);
    }

    inline void LogForce(Level level, TASWWideText msg, std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(level, msg, nullptr, loc, false, true);
    }

    inline void LogForceRaw(Level level, TASWWideText msg, std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(level, msg, nullptr, loc, true, true);
    }

    inline void LogTrace(TASWWideText msg, std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(Level::Trace, msg, nullptr, loc, false, false);
    }

    inline void LogDebug(TASWWideText msg, std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(Level::Debug, msg, nullptr, loc, false, false);
    }

    inline void LogInfo(TASWWideText msg, std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(Level::Info, msg, nullptr, loc, false, false);
    }

    inline void LogWarn(TASWWideText msg, std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(Level::Warn, msg, nullptr, loc, false, false);
    }

    inline void LogError(TASWWideText msg, std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(Level::Error, msg, nullptr, loc, false, false);
    }

    inline void LogCritical(TASWWideText msg, std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(Level::Critical, msg, nullptr, loc, false, false);
    }

    inline void Log(Level level, TASWWideText msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(level, msg, &fields, loc, false, false);
    }

    inline void LogRaw(Level level, TASWWideText msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(level, msg, &fields, loc, true, false);
    }

    inline void LogForce(Level level, TASWWideText msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(level, msg, &fields, loc, false, true);
    }

    inline void LogForceRaw(Level level, TASWWideText msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(level, msg, &fields, loc, true, true);
    }

    inline void LogTrace(TASWWideText msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(Level::Trace, msg, &fields, loc, false, false);
    }

    inline void LogDebug(TASWWideText msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(Level::Debug, msg, &fields, loc, false, false);
    }

    inline void LogInfo(TASWWideText msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(Level::Info, msg, &fields, loc, false, false);
    }

    inline void LogWarn(TASWWideText msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(Level::Warn, msg, &fields, loc, false, false);
    }

    inline void LogError(TASWWideText msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(Level::Error, msg, &fields, loc, false, false);
    }

    inline void LogCritical(TASWWideText msg, std::initializer_list<TASWLogField> fields,
        std::source_location loc = std::source_location::current()) noexcept
    {
        WriteWide(Level::Critical, msg, &fields, loc, false, false);
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

    // --- Non-virtual Inline Template Format Methods With Fields ---
    // As above, with fields for this entry (see the logging methods with fields), first since the format arguments
    // come last, e.g. LogInfoFmt({{"orderId", id}}, "Order {} placed", id).
    template<typename ... Args>
    inline void LogFmt(Level level, std::initializer_list<TASWLogField> fields, TASWFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        if (ShouldLog(level))
            Log(level, fmt.FormatMessage(args ...), fields, fmt.Location);
    }

    template<typename ... Args>
    inline void LogRawFmt(Level level, std::initializer_list<TASWLogField> fields, TASWFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        if (ShouldLog(level))
            LogRaw(level, fmt.FormatMessage(args ...), fields, fmt.Location);
    }

    template<typename ... Args>
    inline void LogForceFmt(Level level, std::initializer_list<TASWLogField> fields,
        TASWFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        if (level != Level::Off && IsEnabled())
            LogForce(level, fmt.FormatMessage(args ...), fields, fmt.Location);
    }

    template<typename ... Args>
    inline void LogForceRawFmt(Level level, std::initializer_list<TASWLogField> fields,
        TASWFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        if (level != Level::Off && IsEnabled())
            LogForceRaw(level, fmt.FormatMessage(args ...), fields, fmt.Location);
    }

    template<typename ... Args>
    inline void LogTraceFmt(std::initializer_list<TASWLogField> fields, TASWFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        LogFmt(Level::Trace, fields, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogDebugFmt(std::initializer_list<TASWLogField> fields, TASWFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        LogFmt(Level::Debug, fields, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogInfoFmt(std::initializer_list<TASWLogField> fields, TASWFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        LogFmt(Level::Info, fields, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogWarnFmt(std::initializer_list<TASWLogField> fields, TASWFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        LogFmt(Level::Warn, fields, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogErrorFmt(std::initializer_list<TASWLogField> fields, TASWFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        LogFmt(Level::Error, fields, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogCriticalFmt(std::initializer_list<TASWLogField> fields, TASWFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        LogFmt(Level::Critical, fields, fmt, std::forward<Args>(args) ...);
    }

    // --- Non-virtual Inline Template Format Methods With a Wide Format String ---
    // As above, with a wchar_t format string, e.g. LogInfoFmt(L"Opened {} ({} bytes)", fileName, size), whose
    // arguments are formatted as wide text (see TASWWideFormatString) and the message converted to UTF-8. Only when the
    // entry would be written, as above.
    template<typename ... Args>
    inline void LogFmt(Level level, TASWWideFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        if (ShouldLog(level))
            Log(level, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogRawFmt(Level level, TASWWideFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        if (ShouldLog(level))
            LogRaw(level, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogForceFmt(Level level, TASWWideFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        if (level != Level::Off && IsEnabled())
            LogForce(level, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogForceRawFmt(Level level, TASWWideFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        if (level != Level::Off && IsEnabled())
            LogForceRaw(level, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogTraceFmt(TASWWideFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        LogFmt(Level::Trace, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogDebugFmt(TASWWideFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        LogFmt(Level::Debug, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogInfoFmt(TASWWideFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        LogFmt(Level::Info, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogWarnFmt(TASWWideFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        LogFmt(Level::Warn, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogErrorFmt(TASWWideFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        LogFmt(Level::Error, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogCriticalFmt(TASWWideFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        LogFmt(Level::Critical, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogFmt(Level level, std::initializer_list<TASWLogField> fields, TASWWideFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        if (ShouldLog(level))
            Log(level, fmt.FormatMessage(args ...), fields, fmt.Location);
    }

    template<typename ... Args>
    inline void LogRawFmt(Level level, std::initializer_list<TASWLogField> fields,
        TASWWideFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        if (ShouldLog(level))
            LogRaw(level, fmt.FormatMessage(args ...), fields, fmt.Location);
    }

    template<typename ... Args>
    inline void LogForceFmt(Level level, std::initializer_list<TASWLogField> fields,
        TASWWideFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        if (level != Level::Off && IsEnabled())
            LogForce(level, fmt.FormatMessage(args ...), fields, fmt.Location);
    }

    template<typename ... Args>
    inline void LogForceRawFmt(Level level, std::initializer_list<TASWLogField> fields,
        TASWWideFormatString<std::type_identity_t<Args>...> fmt, Args&&... args) noexcept
    {
        if (level != Level::Off && IsEnabled())
            LogForceRaw(level, fmt.FormatMessage(args ...), fields, fmt.Location);
    }

    template<typename ... Args>
    inline void LogTraceFmt(std::initializer_list<TASWLogField> fields, TASWWideFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        LogFmt(Level::Trace, fields, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogDebugFmt(std::initializer_list<TASWLogField> fields, TASWWideFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        LogFmt(Level::Debug, fields, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogInfoFmt(std::initializer_list<TASWLogField> fields, TASWWideFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        LogFmt(Level::Info, fields, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogWarnFmt(std::initializer_list<TASWLogField> fields, TASWWideFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        LogFmt(Level::Warn, fields, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogErrorFmt(std::initializer_list<TASWLogField> fields, TASWWideFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        LogFmt(Level::Error, fields, fmt, std::forward<Args>(args) ...);
    }

    template<typename ... Args>
    inline void LogCriticalFmt(std::initializer_list<TASWLogField> fields, TASWWideFormatString<std::type_identity_t<Args>...> fmt,
        Args&&... args) noexcept
    {
        LogFmt(Level::Critical, fields, fmt, std::forward<Args>(args) ...);
    }
};

} // namespace ASWLog

#endif // ASWLog_InterfaceH
