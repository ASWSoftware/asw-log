/* **************************************************************************
ASWLog_MultiLog.cpp
Author: Anthony S. West - ASW Software

See header for info.

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

//---------------------------------------------------------------------------
// Module header
#include "ASWLog_MultiLog.h"
//---------------------------------------------------------------------------
// System includes here
#include <algorithm>
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWMultiLog
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
bool TASWMultiLog::AddLogger(IASWLog& logger)
{
    if (&logger == this)
        return false;

    std::lock_guard<std::mutex> lock(m_ListMutex);
    if (std::find(m_Sinks.begin(), m_Sinks.end(), &logger) != m_Sinks.end())
        return false;

    m_Sinks.push_back(&logger);
    return true;
}

//---------------------------------------------------------------------------
bool TASWMultiLog::Close()
{
    bool allSucceeded = true;
    for (auto* sink : SnapshotSinks())
    {
        if (!sink->Close())
            allSucceeded = false;
    }

    // Like the other loggers, a closed composite can be initialized again
    {
        std::lock_guard<std::mutex> lock(m_StateMutex);
        m_IsInitialized.store(false, std::memory_order_release);
    }

    return allSucceeded;
}

//---------------------------------------------------------------------------
bool TASWMultiLog::Contains(const IASWLog& logger) const noexcept
{
    std::lock_guard<std::mutex> lock(m_ListMutex);
    return std::find(m_Sinks.begin(), m_Sinks.end(), &logger) != m_Sinks.end();
}

//---------------------------------------------------------------------------
/*
    TASWMultiLog::FanOut

    Calls 'logCall' with each registered sink. Never throws: a sink that throws (e.g. a custom IASWLog) doesn't stop
    the others from receiving the entry, and doesn't throw into the application.
*/
template<typename TLogCall>
void TASWMultiLog::FanOut(const TLogCall& logCall) const noexcept
{
    try
    {
        for (auto* sink : SnapshotSinks())
        {
            try
            {
                logCall(*sink);
            }
            catch (...)
            {
            }
        }
    }
    catch (...)
    {
    }
}

//---------------------------------------------------------------------------
std::size_t TASWMultiLog::GetLoggerCount() const noexcept
{
    std::lock_guard<std::mutex> lock(m_ListMutex);
    return m_Sinks.size();
}

//---------------------------------------------------------------------------
std::vector<IASWLog*> TASWMultiLog::GetLoggers() const
{
    return SnapshotSinks();
}

//---------------------------------------------------------------------------
bool TASWMultiLog::Initialize(const TASWLogConfig& config)
{
    {
        std::lock_guard<std::mutex> lock(m_StateMutex);
        if (m_IsInitialized.load(std::memory_order_acquire))
            return false;

        m_Config = config;
        m_MinimumLevel.store(m_Config.InitialMinimumLevel, std::memory_order_release);
        m_IsInitialized.store(true, std::memory_order_release);
    }

    bool allSucceeded = true;
    for (auto* sink : SnapshotSinks())
    {
        if (!sink->Initialize(config) && !sink->IsOpen())
            allSucceeded = false;
    }

    return allSucceeded;
}

//---------------------------------------------------------------------------
bool TASWMultiLog::IsOpen() const noexcept
{
    for (auto* sink : SnapshotSinks())
    {
        if (!sink->IsOpen())
            return false;
    }

    return true;
}

//---------------------------------------------------------------------------
void TASWMultiLog::Log(Level level, std::string_view message, std::source_location loc)
{
    if (level == Level::Off || level < GetMinimumLevel())
        return;

    FanOut([&](IASWLog& sink) {
            sink.Log(level, message, loc);
        });
}

//---------------------------------------------------------------------------
void TASWMultiLog::LogForce(Level level, std::string_view message, std::source_location loc)
{
    if (level == Level::Off) // Not a severity: never written, even when forced
        return;

    FanOut([&](IASWLog& sink) {
            sink.LogForce(level, message, loc);
        });
}

//---------------------------------------------------------------------------
void TASWMultiLog::LogForceRaw(Level level, std::string_view message, std::source_location loc)
{
    if (level == Level::Off) // Not a severity: never written, even when forced
        return;

    FanOut([&](IASWLog& sink) {
            sink.LogForceRaw(level, message, loc);
        });
}

//---------------------------------------------------------------------------
void TASWMultiLog::LogRaw(Level level, std::string_view message, std::source_location loc)
{
    if (level == Level::Off || level < GetMinimumLevel())
        return;

    FanOut([&](IASWLog& sink) {
            sink.LogRaw(level, message, loc);
        });
}

//---------------------------------------------------------------------------
bool TASWMultiLog::Open()
{
    bool allSucceeded = true;
    for (auto* sink : SnapshotSinks())
    {
        if (!sink->Open())
            allSucceeded = false;
    }

    return allSucceeded;
}

//---------------------------------------------------------------------------
std::size_t TASWMultiLog::RemoveAllLoggers() noexcept
{
    std::lock_guard<std::mutex> lock(m_ListMutex);
    const auto count = m_Sinks.size();
    m_Sinks.clear();
    return count;
}

//---------------------------------------------------------------------------
/*
    TASWMultiLog::RemoveLogger

    Returns true if `logger` was registered and has been removed.
*/
bool TASWMultiLog::RemoveLogger(IASWLog& logger) noexcept
{
    std::lock_guard<std::mutex> lock(m_ListMutex);
    const auto originalCount = m_Sinks.size();
    m_Sinks.erase(std::remove(m_Sinks.begin(), m_Sinks.end(), &logger), m_Sinks.end());
    return m_Sinks.size() != originalCount;
}

//---------------------------------------------------------------------------
std::vector<IASWLog*> TASWMultiLog::SnapshotSinks() const
{
    std::lock_guard<std::mutex> lock(m_ListMutex);
    return m_Sinks;
}

//---------------------------------------------------------------------------

} // namespace ASWLog
