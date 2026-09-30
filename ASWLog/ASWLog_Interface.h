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
//---------------------------------------------------------------------------
#include "ASWLog_Config.h"
#include "ASWLog_Types.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWFormatString
//
// A format string plus the source location of the call that passed it, taken by the *Fmt methods. C++ doesn't allow
// a defaulted std::source_location parameter after the arguments' parameter pack, but the defaulted argument of this
// implicit constructor is evaluated at the call site too, so it captures the caller's location.
/////////////////////////////////////////////////////////////////////////////
struct TASWFormatString
{
    std::string_view Format;
    std::source_location Location;

    template<typename T>
    requires std::convertible_to<const T&, std::string_view>
    TASWFormatString(const T& format, std::source_location location = std::source_location::current())
        : Format(format),
          Location(location)
    {
    }

    // Formats the message from 'args'. Never throws: if formatting fails (e.g. the format string doesn't match the
    // arguments), returns "[ASWLog format error: <reason>] <format string>" instead, so the entry is still logged.
    template<typename ... Args>
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
// Interface for the logger.
/////////////////////////////////////////////////////////////////////////////
class IASWLog
{
public:
    virtual ~IASWLog() = default;

    virtual std::string_view GetVersionStr() const noexcept = 0;
    virtual std::string GetFullVersionStr() const = 0;

    virtual TASWLogConfig& GetConfig() noexcept = 0;
    virtual const TASWLogConfig& GetConfig() const noexcept = 0;

    virtual bool Initialize(const TASWLogConfig& config) = 0;

    virtual bool Open() = 0;
    virtual bool Close() = 0;
    virtual bool IsOpen() const noexcept = 0;

    virtual void Log(Level level, std::string_view message, std::source_location loc = std::source_location::current()) = 0;
    virtual void LogRaw(Level level, std::string_view message, std::source_location loc = std::source_location::current()) = 0;

    virtual void LogForce(Level level, std::string_view message, std::source_location loc = std::source_location::current()) = 0;
    virtual void LogForceRaw(Level level, std::string_view message, std::source_location loc = std::source_location::current()) = 0;

    virtual void LogTrace(std::string_view msg, std::source_location loc = std::source_location::current()) = 0;
    virtual void LogDebug(std::string_view msg, std::source_location loc = std::source_location::current()) = 0;
    virtual void LogInfo(std::string_view msg, std::source_location loc = std::source_location::current()) = 0;
    virtual void LogWarn(std::string_view msg, std::source_location loc = std::source_location::current()) = 0;
    virtual void LogError(std::string_view msg, std::source_location loc = std::source_location::current()) = 0;
    virtual void LogCritical(std::string_view msg, std::source_location loc = std::source_location::current()) = 0;

    // --- Non-virtual Inline Template Format Methods ---
    // Each passes on the caller's source location, captured by TASWFormatString. A formatting error is logged in place
    // of the message instead of thrown (see TASWFormatString::FormatMessage()).
    template<typename ... Args>
    inline void LogFmt(Level level, TASWFormatString fmt, Args&&... args)
    {
        Log(level, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogRawFmt(Level level, TASWFormatString fmt, Args&&... args)
    {
        LogRaw(level, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogForceFmt(Level level, TASWFormatString fmt, Args&&... args)
    {
        LogForce(level, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogForceRawFmt(Level level, TASWFormatString fmt, Args&&... args)
    {
        LogForceRaw(level, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogTraceFmt(TASWFormatString fmt, Args&&... args)
    {
        Log(Level::Trace, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogDebugFmt(TASWFormatString fmt, Args&&... args)
    {
        Log(Level::Debug, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogInfoFmt(TASWFormatString fmt, Args&&... args)
    {
        Log(Level::Info, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogWarnFmt(TASWFormatString fmt, Args&&... args)
    {
        Log(Level::Warn, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogErrorFmt(TASWFormatString fmt, Args&&... args)
    {
        Log(Level::Error, fmt.FormatMessage(args ...), fmt.Location);
    }

    template<typename ... Args>
    inline void LogCriticalFmt(TASWFormatString fmt, Args&&... args)
    {
        Log(Level::Critical, fmt.FormatMessage(args ...), fmt.Location);
    }
};

} // namespace ASWLog

#endif // ASWLog_InterfaceH
