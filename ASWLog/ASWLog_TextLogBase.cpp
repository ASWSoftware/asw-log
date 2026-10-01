/* **************************************************************************
ASWLog_TextLogBase.cpp
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
#include "ASWLog_TextLogBase.h"
//---------------------------------------------------------------------------
// System includes here
#include <filesystem>
#include <format>
#include <iterator>
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWTextLogBase
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
void TASWTextLogBase::AfterEntryUnlocked()
{
}

//---------------------------------------------------------------------------
void TASWTextLogBase::AppendLineEnding(std::string& line) const
{
    if (m_Config.LogLineEnding == LineEnding::CRLF)
        line += "\r\n";
    else
        line += '\n';
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::Close()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return CloseUnlocked();
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::DispatchLogCallback

    Called after m_Mutex has been released, so a callback that logs again doesn't deadlock. Calls the config's
    OnLogEntry if it's set and 'level' meets CallbackMinimumLevel.
*/
void TASWTextLogBase::DispatchLogCallback(Level level, std::string_view formattedLine) const noexcept
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

//---------------------------------------------------------------------------
bool TASWTextLogBase::EnsureReadyUnlocked()
{
    return m_IsInitialized.load(std::memory_order_acquire) && m_IsOpen.load(std::memory_order_acquire);
}

//---------------------------------------------------------------------------
void TASWTextLogBase::Finalize() noexcept
{
    // Called from the destructors, where an exception would terminate the program
    try
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_IsInitialized.load(std::memory_order_acquire))
            return;

        if (m_Config.WriteShutdownLog)
        {
            std::string msg = "Logger shutdown: " + Time::ToISO8601String(NowUTC());

            if (!m_Config.BannerMessage_Shutdown.empty())
                msg += ", " + m_Config.BannerMessage_Shutdown;

            WriteLogEntry(Level::Info, msg, false, false, true, std::source_location::current());
        }

        CloseUnlocked();
    }
    catch (...)
    {
    }
}

//---------------------------------------------------------------------------
std::string TASWTextLogBase::FormatLine(
    Level level, std::string_view message, std::source_location loc, std::chrono::system_clock::time_point now) const
{
    std::string line;
    line.reserve(message.size() + 256);

    if (m_Config.LogUTCDateTime)
        std::format_to(std::back_inserter(line), "[{}]", Time::ToISO8601String(now));

    if (m_Config.LogLevelStr)
        std::format_to(std::back_inserter(line), "[{}]", Level_ToString(level));

    if (m_Config.LogProcessId)
        std::format_to(std::back_inserter(line), "[P:{}]", GetCurrentOSProcessId());

    if (m_Config.LogThreadId)
        std::format_to(std::back_inserter(line), "[T:{}]", GetCurrentOSThreadId());

    if (m_Config.LogAppMem_WorkingSet || m_Config.LogAppMem_PeakWorkingSet)
    {
        const auto memoryUsage = GetMemoryUsage();
        if (m_Config.LogAppMem_WorkingSet)
            std::format_to(std::back_inserter(line), "[WS:{}]", memoryUsage.WorkingSetBytes);

        if (m_Config.LogAppMem_PeakWorkingSet)
            std::format_to(std::back_inserter(line), "[PWS:{}]", memoryUsage.PeakWorkingSetBytes);
    }

    if (m_Config.LogMethodName)
        std::format_to(std::back_inserter(line), "[{}]", loc.function_name());

    if (m_Config.LogSourceLine)
    {
        std::filesystem::path fullPath(loc.file_name());
        std::format_to(std::back_inserter(line), "[{}:{}]", fullPath.filename().string(), loc.line());
    }

    line += ": ";
    line.append(message);
    return line;
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::Initialize(const TASWLogConfig& config)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_IsInitialized.load(std::memory_order_acquire))
    {
        return false;
    }

    m_Config = config;
    m_MinimumLevel.store(m_Config.InitialMinimumLevel, std::memory_order_release);

    if (!InitializeUnlocked())
    {
        return false;
    }

    if (!m_Config.BannerMessage_Init.empty())
    {
        WriteLogEntry(Level::Info, m_Config.BannerMessage_Init, false, false, true, std::source_location::current());
    }

    WriteInitializationInfo();

    AfterEntryUnlocked();

    return true;
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::IsOpen() const noexcept
{
    return m_IsOpen.load(std::memory_order_acquire);
}

//---------------------------------------------------------------------------
void TASWTextLogBase::Log(Level level, std::string_view message, std::source_location loc)
{
    if (level < GetMinimumLevel())
        return;

    LogEntry(level, message, false, false, true, loc);
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::LogEntry

    Writes one entry for the public Log* methods, then calls OnLogEntry. Never throws, so that logging can't throw
    into the application: if writing fails (e.g. out of memory), the entry is dropped.
*/
void TASWTextLogBase::LogEntry(
    Level level, std::string_view message, bool force, bool raw, bool includeNewLine, std::source_location loc) noexcept
{
    try
    {
        std::string writtenLine;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (!EnsureReadyUnlocked())
                return;

            writtenLine = WriteLogEntry(level, message, force, raw, includeNewLine, loc);

            AfterEntryUnlocked();
        }

        if (!writtenLine.empty())
            DispatchLogCallback(level, writtenLine);
    }
    catch (...)
    {
    }
}

//---------------------------------------------------------------------------
void TASWTextLogBase::LogForce(Level level, std::string_view message, std::source_location loc)
{
    LogEntry(level, message, true, false, true, loc);
}

//---------------------------------------------------------------------------
void TASWTextLogBase::LogForceRaw(Level level, std::string_view message, std::source_location loc)
{
    LogEntry(level, message, true, true, false, loc);
}

//---------------------------------------------------------------------------
void TASWTextLogBase::LogRaw(Level level, std::string_view message, std::source_location loc)
{
    if (level < GetMinimumLevel())
        return;

    LogEntry(level, message, false, true, false, loc);
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::Open()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return OpenUnlocked();
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::PrepareWriteUnlocked(std::chrono::system_clock::time_point /*now*/)
{
    return true;
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteApplicationInfo()
{
    auto applicationInfo = std::format("app_exe='{}', app_target=", PathToUTF8String(GetExecutablePath()));

#if defined(_WIN64)
    applicationInfo += "Win64";
#elif defined(_WIN32)
    applicationInfo += "Win32";
#elif defined(__linux__) && defined(__x86_64__)
    applicationInfo += "Linux64";
#elif defined(__linux__) && defined(__aarch64__)
    applicationInfo += "LinuxARM64";
#elif defined(__linux__)
    applicationInfo += "Linux32";
#else
#error "ASWLog: Unrecognized target platform in WriteApplicationInfo()"
#endif

    if (m_Config.Init_LogCommandLine)
        applicationInfo += std::format(", command_line='{}'", GetCommandLineString());

    WriteLogEntry(Level::Info, std::format("App: {}", applicationInfo), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteDriveInfo()
{
    WriteLogEntry(Level::Info,
        std::format("Drive: {}", GetDriveInfoString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteInitializationInfo()
{
    if (m_Config.Init_LogTimeInfo)
        WriteTimeInfo();

    if (m_Config.Init_LogOSInfo)
        WriteOSInfo();

    if (m_Config.Init_LogDriveInfo)
        WriteDriveInfo();

    if (m_Config.Init_LogSysMemInfo)
        WriteSystemMemoryInfo();

    if (m_Config.Init_LogApplicationInfo)
        WriteApplicationInfo();

    if (m_Config.Init_LogMemoryUsage)
        WriteMemoryUsageInfo();
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::WriteLogEntry

    Formats and writes one entry, unless its level is filtered out (and it isn't forced) or PrepareWriteUnlocked()
    drops it. Returns the line written, or an empty string if nothing was written.
*/
std::string TASWTextLogBase::WriteLogEntry(
    Level level, std::string_view message, bool force, bool raw, bool includeNewLine, std::source_location loc)
{
    if (!force && level < GetMinimumLevel())
        return {};

    const auto now = NowUTC();
    if (!PrepareWriteUnlocked(now))
        return {};

    std::string line;
    if (raw)
    {
        line.reserve(message.size() + 2);
        line.append(message);
    }
    else
    {
        line = FormatLine(level, message, loc, now);
    }

    if (includeNewLine)
        AppendLineEnding(line);

    WriteLineUnlocked(level, line, includeNewLine);
    return line;
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteMemoryUsageInfo()
{
    WriteLogEntry(Level::Info, std::format("App Memory: {}", GetMemoryUsageString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteOSInfo()
{
    WriteLogEntry(Level::Info, std::format("OS: {}", GetOSInfoString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteSystemMemoryInfo()
{
    WriteLogEntry(Level::Info,
        std::format("System memory: {}", GetSystemMemoryUsageString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteTimeInfo()
{
    WriteLogEntry(Level::Info, std::format("Time: {}", GetTimeInfoString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------

} // namespace ASWLog
