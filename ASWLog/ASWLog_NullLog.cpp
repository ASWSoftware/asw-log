/* **************************************************************************
ASWLog_NullLog.cpp
Author: Anthony S. West - ASW Software

See header for info.

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

//---------------------------------------------------------------------------
// Module header
#include "ASWLog_NullLog.h"
//---------------------------------------------------------------------------
// System includes here
#include <format>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWLog_Version.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWNullLog
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWNullLog::TASWNullLog()
    : m_Config(std::make_shared<const TASWLogConfig>())
{
}

//---------------------------------------------------------------------------
bool TASWNullLog::Close() noexcept
{
    return true;
}

//---------------------------------------------------------------------------
void TASWNullLog::DumpBacktrace() noexcept
{
}

//---------------------------------------------------------------------------
bool TASWNullLog::Flush() noexcept
{
    return true;
}

//---------------------------------------------------------------------------
std::shared_ptr<const TASWLogConfig> TASWNullLog::GetConfig() const noexcept
{
    std::lock_guard<std::mutex> lock(m_ConfigMutex);
    return m_Config;
}

//---------------------------------------------------------------------------
std::string TASWNullLog::GetFullVersionStr() const
{
    return std::format("TASWNullLog - Base version {}", GetVersionStr());
}

//---------------------------------------------------------------------------
TASWNullLog& TASWNullLog::GetInstance()
{
    // Never deleted, so the instance stays usable through static destruction (it has nothing to finalize)
    static TASWNullLog* const instance = new TASWNullLog();
    return *instance;
}

//---------------------------------------------------------------------------
Level TASWNullLog::GetMinimumLevel() const noexcept
{
    return m_MinimumLevel.load(std::memory_order_relaxed);
}

//---------------------------------------------------------------------------
std::string_view TASWNullLog::GetVersionStr() const noexcept
{
    return Version; // See ASWLog_Version.h
}

//---------------------------------------------------------------------------
bool TASWNullLog::Initialize(const TASWLogConfig& config) noexcept
{
    if (!StoreConfig(config))
        return false;

    SetMinimumLevel(config.InitialMinimumLevel);

    return true;
}

//---------------------------------------------------------------------------
bool TASWNullLog::IsEnabled() const noexcept
{
    return false;
}

//---------------------------------------------------------------------------
bool TASWNullLog::IsOpen() const noexcept
{
    return true;
}

//---------------------------------------------------------------------------
bool TASWNullLog::Open() noexcept
{
    return true;
}

//---------------------------------------------------------------------------
bool TASWNullLog::Reconfigure(const TASWLogConfig& config) noexcept
{
    return StoreConfig(config);
}

//---------------------------------------------------------------------------
void TASWNullLog::SetEnabled(bool /*enabled*/) noexcept
{
}

//---------------------------------------------------------------------------
void TASWNullLog::SetMinimumLevel(Level level) noexcept
{
    m_MinimumLevel.store(level, std::memory_order_relaxed);
}

//---------------------------------------------------------------------------
bool TASWNullLog::ShouldLog(Level /*level*/) const noexcept
{
    return false;
}

//---------------------------------------------------------------------------
bool TASWNullLog::ShouldLog(const TASWLogRecord& /*record*/) const noexcept
{
    return false;
}

//---------------------------------------------------------------------------
/*
    TASWNullLog::StoreConfig

    Replaces the kept config with a copy of 'config'. The previous one is released after the lock, since destroying it
    can run other code (e.g. the destructors of its callbacks' captures). False if the copy can't be made.
*/
bool TASWNullLog::StoreConfig(const TASWLogConfig& config) noexcept
{
    try
    {
        auto snapshot = std::make_shared<const TASWLogConfig>(config);

        {
            std::lock_guard<std::mutex> lock(m_ConfigMutex);
            m_Config.swap(snapshot);
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}

//---------------------------------------------------------------------------
void TASWNullLog::Write(const TASWLogRecord& /*record*/) noexcept
{
}

//---------------------------------------------------------------------------

} // namespace ASWLog
