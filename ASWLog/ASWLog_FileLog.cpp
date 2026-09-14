/* **************************************************************************
ASWLog_FileLog.cpp
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
#include "ASWLog_FileLog.h"
//---------------------------------------------------------------------------
// System includes here
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWFileLog
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWFileLog::~TASWFileLog()
{
    Finalize();
}

//---------------------------------------------------------------------------
void TASWFileLog::Finalize(std::string_view exitMessage)
{
    std::lock_guard<std::mutex> lock(m_FileMutex);
    if (!m_IsInitialized)
        return;

    if (m_FileStream.is_open())
    {
        if (m_Config.LogUTCDateTime)
            m_FileStream << "[" << Time::ToISO8601String(std::chrono::system_clock::now()) << "]";

        m_FileStream << "[LOGGER SHUTDOWN]";

        if (!exitMessage.empty())
            m_FileStream << ": " << exitMessage;

        m_FileStream << "\n";
        m_FileStream.flush();
        m_FileStream.close();
    }

    m_IsInitialized = false;
}

//---------------------------------------------------------------------------
TASWFileLog& TASWFileLog::GetInstance()
{
    static TASWFileLog instance;
    return instance;
}

//---------------------------------------------------------------------------
bool TASWFileLog::Initialize(const TASWLogConfig& config)
{
    std::unique_lock<std::mutex> lock(m_FileMutex);
    if (m_IsInitialized)
    {
        return false;
    }

    m_Config = config;
    m_MinimumLevel.store(m_Config.MinimumLevel);

    if (!m_Config.LogFilePath.parent_path().empty())
    {
        std::filesystem::create_directories(m_Config.LogFilePath.parent_path());
    }

    m_FileStream.open(m_Config.LogFilePath, std::ios::out | std::ios::app);
    if (!m_FileStream.is_open())
    {
        return false;
    }

    // Capture the initial initialization date stamp to anchor midnight evaluation
    auto now = std::chrono::system_clock::now();
    m_LastLogDateStr = Time::ToDateString(now);

    m_IsInitialized = true;

    if (!m_Config.BannerMessage.empty())
    {
        lock.unlock(); // Unlock to route safely through public Log call, which will lock
        Log(Level::Info, m_Config.BannerMessage);
    }

    return true;
}

//---------------------------------------------------------------------------
void TASWFileLog::Log(Level level, std::string_view message, std::source_location loc)
{
    if (level < m_MinimumLevel.load(std::memory_order_relaxed))
        return;

    std::lock_guard<std::mutex> lock(m_FileMutex);
    if (!m_FileStream.is_open())
        return;

    auto now = std::chrono::system_clock::now();

    // Check Midnight Rolling Rule
    if (m_Config.EnableDailyRolling)
    {
        std::string currentDateStr = Time::ToDateString(now);
        if (currentDateStr != m_LastLogDateStr)
        {
            RotateLogFiles("daily");
            m_LastLogDateStr = currentDateStr; // Transition reference state to the new day
        }
    }

    // Check Size-Based Rotation Rule
    if (m_Config.EnableRotation && std::filesystem::exists(m_Config.LogFilePath))
    {
        if (std::filesystem::file_size(m_Config.LogFilePath) >= m_Config.MaxFileSizeBytes)
        {
            RotateLogFiles("size");
        }
    }

    // --- Assemble the complete line metadata dynamically via string formatting configs ---

    if (m_Config.LogUTCDateTime)
        m_FileStream << "[" << Time::ToISO8601String(now) << "]";

    if (m_Config.LogLevelStr)
        m_FileStream << "[" << Level_ToString(level) << "]";

    if (m_Config.LogProcessId)
    {
#if defined(_WIN32)
        m_FileStream << "[" << GetCurrentProcessId() << "]";
#else
        m_FileStream << "[" << getpid() << "]";
#endif
    }

    if (m_Config.LogThreadId)
    {
        auto numericThreadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
        m_FileStream << "[" << numericThreadId << "]";
    }

//    if (m_Config.LogModuleName)
//        m_FileStream << "[" << m_Config.ModuleName << "]";

    if (m_Config.LogMethodName)
        m_FileStream << "[" << loc.function_name() << "]";

    if (m_Config.LogSourceLine)
    {
        std::filesystem::path fullPath(loc.file_name());
        m_FileStream << "[" << fullPath.filename().string() << ":" << loc.line() << "]";
    }

    // --- Done writing config metadata ---

    // Finally, write the message
    m_FileStream << ": " << message << "\n";

    // Flush out system level trace boundaries to safeguard against application crashes
    m_FileStream.flush();
}

//---------------------------------------------------------------------------
void TASWFileLog::RotateLogFiles(std::string_view reasonTag)
{
    m_FileStream.close();

    auto now = std::chrono::system_clock::now();
    std::string timeStr = Time::ToDateString(now);

    std::filesystem::path backupPath = m_Config.LogFilePath;
    backupPath.replace_extension(std::format(".{}.{}.bak", reasonTag, timeStr));

    std::filesystem::remove(backupPath);
    std::filesystem::rename(m_Config.LogFilePath, backupPath);

    m_FileStream.open(m_Config.LogFilePath, std::ios::out | std::ios::app);
}

//---------------------------------------------------------------------------

} // namespace ASWLog
