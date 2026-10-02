/* **************************************************************************
ASWLog_Base.h
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

#ifndef ASWLog_BaseH
#define ASWLog_BaseH
//---------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_Interface.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWLogBase
//
// Holds common base methods for log descendants.
/////////////////////////////////////////////////////////////////////////////
class TASWLogBase : public IASWLog
{
private:
    typedef IASWLog inherited;

protected:
    TASWLogConfig m_Config;
    std::atomic<Level> m_MinimumLevel{ Level::Info };
    std::atomic<bool> m_IsInitialized{ false };

protected:
    // Pure virtual helper so the base class knows what implementation name to print
    virtual std::string_view GetLoggerClassName() const noexcept = 0;

    // The current time, used for log line timestamps, daily rolling, and backup file names. Override it to control
    // the logger's clock, e.g. in tests. A system_clock time point has no time zone (it counts from the UTC epoch).
    virtual std::chrono::system_clock::time_point NowUTC() const noexcept
    {
        return std::chrono::system_clock::now();
    }

public:
    std::string_view GetVersionStr() const noexcept final
    {
        return "0.45.0-dev.1"; // Semantic Versioning
    }

    std::string GetFullVersionStr() const final
    {
        return std::format("{} - Base version {}", GetLoggerClassName(), GetVersionStr());
    }

    TASWLogConfig& GetConfig() noexcept final
    {
        return m_Config;
    }

    const TASWLogConfig& GetConfig() const noexcept final
    {
        return m_Config;
    }

    // Lock-free runtime level gate. Initialize() seeds this from m_Config.InitialMinimumLevel;
    // afterward this atomic (not m_Config.InitialMinimumLevel) is the authoritative value
    // used by Log()/LogRaw() to skip locking entirely for filtered entries.
    Level GetMinimumLevel() const noexcept
    {
        return m_MinimumLevel.load(std::memory_order_relaxed);
    }

    void SetMinimumLevel(Level level) noexcept
    {
        m_MinimumLevel.store(level, std::memory_order_relaxed);
    }

public:
    void LogTrace(std::string_view msg, std::source_location loc = std::source_location::current()) override;
    void LogDebug(std::string_view msg, std::source_location loc = std::source_location::current()) override;
    void LogInfo(std::string_view msg, std::source_location loc = std::source_location::current()) override;
    void LogWarn(std::string_view msg, std::source_location loc = std::source_location::current()) override;
    void LogError(std::string_view msg, std::source_location loc = std::source_location::current()) override;
    void LogCritical(std::string_view msg, std::source_location loc = std::source_location::current()) override;
};

} // namespace ASWLog

#endif // ASWLog_BaseH
