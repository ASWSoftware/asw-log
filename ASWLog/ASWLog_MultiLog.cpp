/* **************************************************************************
ASWLog_MultiLog.cpp
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
bool TASWMultiLog::Close() noexcept
{
    try
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
    catch (...)
    {
        return false; // Couldn't copy the sink list (out of memory)
    }
}

//---------------------------------------------------------------------------
bool TASWMultiLog::Contains(const IASWLog& logger) const noexcept
{
    std::lock_guard<std::mutex> lock(m_ListMutex);
    return std::find(m_Sinks.begin(), m_Sinks.end(), &logger) != m_Sinks.end();
}

//---------------------------------------------------------------------------
bool TASWMultiLog::Flush() noexcept
{
    try
    {
        bool allSucceeded = true;
        for (auto* sink : SnapshotSinks())
        {
            if (!sink->Flush())
                allSucceeded = false;
        }

        return allSucceeded;
    }
    catch (...)
    {
        return false;
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
bool TASWMultiLog::Initialize(const TASWLogConfig& config) noexcept
{
    try
    {
        {
            std::shared_ptr<const TASWLogConfig> previousConfig; // Released after the lock (see SetConfig())
            std::lock_guard<std::mutex> lock(m_StateMutex);
            if (m_IsInitialized.load(std::memory_order_acquire))
                return false;

            previousConfig = SetConfig(config);
            SetMinimumLevel(config.InitialMinimumLevel);
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
    catch (...)
    {
        return false; // Out of memory copying the config or the sink list
    }
}

//---------------------------------------------------------------------------
bool TASWMultiLog::IsOpen() const noexcept
{
    // Iterates under the lock instead of copying the list, which could throw. A sink's IsOpen() must not call back
    // into this composite.
    std::lock_guard<std::mutex> lock(m_ListMutex);
    for (const auto* sink : m_Sinks)
    {
        if (!sink->IsOpen())
            return false;
    }

    return true;
}

//---------------------------------------------------------------------------
bool TASWMultiLog::Open() noexcept
{
    try
    {
        bool allSucceeded = true;
        for (auto* sink : SnapshotSinks())
        {
            if (!sink->Open())
                allSucceeded = false;
        }

        return allSucceeded;
    }
    catch (...)
    {
        return false; // Couldn't copy the sink list (out of memory)
    }
}

//---------------------------------------------------------------------------
bool TASWMultiLog::Reconfigure(const TASWLogConfig& config) noexcept
{
    try
    {
        {
            std::shared_ptr<const TASWLogConfig> previousConfig; // Released after the lock (see SetConfig())
            std::lock_guard<std::mutex> lock(m_StateMutex);
            if (!m_IsInitialized.load(std::memory_order_acquire))
                return false;

            previousConfig = SetConfig(config);
        }

        bool allSucceeded = true;
        for (auto* sink : SnapshotSinks())
        {
            if (!sink->Reconfigure(config))
                allSucceeded = false;
        }

        return allSucceeded;
    }
    catch (...)
    {
        return false;
    }
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
bool TASWMultiLog::ShouldLog(Level level) const noexcept
{
    if (!PassesLevelGate(level))
        return false;

    // Iterates under the lock instead of copying the list, which could throw. A sink's ShouldLog() must not call back
    // into this composite (the built-in loggers' ShouldLog() only reads their own level and enabled flag).
    std::lock_guard<std::mutex> lock(m_ListMutex);
    for (const auto* sink : m_Sinks)
    {
        if (sink->ShouldLog(level))
            return true;
    }

    return false;
}

//---------------------------------------------------------------------------
std::vector<IASWLog*> TASWMultiLog::SnapshotSinks() const
{
    std::lock_guard<std::mutex> lock(m_ListMutex);
    return m_Sinks;
}

//---------------------------------------------------------------------------
/*
    TASWMultiLog::WriteRecord

    Each sink's Write() is noexcept, so a failing sink can't stop the others from getting the entry. If copying the
    sink list throws (out of memory), TASWLogBase::Write() drops the entry.
*/
void TASWMultiLog::WriteRecord(const TASWLogRecord& record)
{
    for (auto* sink : SnapshotSinks())
        sink->Write(record);
}

//---------------------------------------------------------------------------

} // namespace ASWLog
