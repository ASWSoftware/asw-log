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
#include <format>
#include <source_location>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_Config.h"
#include "ASWLog_Types.h"
//---------------------------------------------------------------------------


namespace ASWLog
{

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

    virtual bool Initialize(const TASWLogConfig& config) = 0;
    virtual void Finalize(std::string_view exitMessage = "") = 0;

    // Core raw string logging method
    virtual void Log(
        Level level, std::string_view message, std::source_location loc = std::source_location::current()) = 0;

    // Helper functions for easy Level calling shortcuts
    virtual void LogTrace(std::string_view msg, std::source_location loc = std::source_location::current()) = 0;
    virtual void LogDebug(std::string_view msg, std::source_location loc = std::source_location::current()) = 0;
    virtual void LogInfo(std::string_view msg, std::source_location loc = std::source_location::current()) = 0;
    virtual void LogWarn(std::string_view msg, std::source_location loc = std::source_location::current()) = 0;
    virtual void LogError(std::string_view msg, std::source_location loc = std::source_location::current()) = 0;
    virtual void LogCritical(std::string_view msg, std::source_location loc = std::source_location::current()) = 0;

    // --- Non-virtual Inline Template Format Methods ---
    template<typename ... Args>
    inline void LogFmt(Level level, std::string_view fmt, Args&&... args)
    {
        Log(level, std::vformat(fmt, std::make_format_args(args ...)));
    }

    template<typename ... Args>
    inline void LogTraceFmt(std::string_view fmt, Args&&... args)
    {
        Log(Level::Trace, std::vformat(fmt, std::make_format_args(args ...)));
    }

    template<typename ... Args>
    inline void LogDebugFmt(std::string_view fmt, Args&&... args)
    {
        Log(Level::Debug, std::vformat(fmt, std::make_format_args(args ...)));
    }

    template<typename ... Args>
    inline void LogInfoFmt(std::string_view fmt, Args&&... args)
    {
        Log(Level::Info, std::vformat(fmt, std::make_format_args(args ...)));
    }

    template<typename ... Args>
    inline void LogWarnFmt(std::string_view fmt, Args&&... args)
    {
        Log(Level::Warn, std::vformat(fmt, std::make_format_args(args ...)));
    }

    template<typename ... Args>
    inline void LogErrorFmt(std::string_view fmt, Args&&... args)
    {
        Log(Level::Error, std::vformat(fmt, std::make_format_args(args ...)));
    }

    template<typename ... Args>
    inline void LogCriticalFmt(std::string_view fmt, Args&&... args)
    {
        Log(Level::Critical, std::vformat(fmt, std::make_format_args(args ...)));
    }
};

} // namespace ASWLog

#endif // ASWLog_InterfaceH
