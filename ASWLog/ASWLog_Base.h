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

public:
    std::string_view GetVersionStr() const noexcept final
    {
        return "0.26.2"; // Semantic Versioning
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

protected:
    // Invoked by sinks after their internal mutex has been released, so a callback that logs again does not
    // deadlock. The config's OnLogEntry is called if set and 'level' meets 'CallbackMinimumLevel'.
    void DispatchLogCallback(Level level, std::string_view formattedLine) const noexcept
    {
        const auto& callback = m_Config.OnLogEntry;
        if (callback == nullptr || level < m_Config.CallbackMinimumLevel)
            return;

        try
        {
            callback(level, formattedLine);
        }
        catch (...)
        {
        }
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
