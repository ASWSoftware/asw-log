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
#include <format>
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
    OnLogEntry if it's set and the record's level meets CallbackMinimumLevel.
*/
void TASWTextLogBase::DispatchLogCallback(const TASWLogRecord& record, std::string_view formattedLine) const noexcept
{
    const auto& callback = m_Config.OnLogEntry;
    if (callback == nullptr || record.LogLevel < m_Config.CallbackMinimumLevel)
        return;

    try
    {
        callback(record, formattedLine);
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

            WriteInfoLine(msg);
        }

        CloseUnlocked();
    }
    catch (...)
    {
    }
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::Flush() noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return FlushUnlocked();
    }
    catch (...)
    {
        return false;
    }
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
    SetMinimumLevel(m_Config.InitialMinimumLevel);

    if (!InitializeUnlocked())
    {
        return false;
    }

    if (!m_Config.BannerMessage_Init.empty())
    {
        WriteInfoLine(m_Config.BannerMessage_Init);
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

    WriteInfoLine(std::format("App: {}", applicationInfo));
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteDriveInfo()
{
    WriteInfoLine(std::format("Drive: {}", GetDriveInfoString()));
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::WriteInfoLine

    Writes one of this logger's own Info lines (startup, banner or shutdown), stamped now, with the caller's location.
*/
void TASWTextLogBase::WriteInfoLine(std::string_view message, std::source_location loc)
{
    TASWLogRecord record;
    record.LogLevel = Level::Info;
    record.Message = message;
    record.Location = loc;
    StampRecord(record);
    WriteLogEntry(record);
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

    Formats and writes one stamped entry, unless the logger is disabled, its level is filtered out (and it isn't
    forced), or PrepareWriteUnlocked() drops it. Returns the line written, or an empty string if nothing was written.
    The startup and shutdown lines come through here too, so they aren't written while the logger is disabled.
*/
std::string TASWTextLogBase::WriteLogEntry(const TASWLogRecord& record)
{
    if (!IsEnabled() || (!record.Forced && record.LogLevel < GetMinimumLevel()))
        return {};

    if (!PrepareWriteUnlocked(record.Timestamp))
        return {};

    std::string line;
    if (record.Raw)
    {
        line.reserve(record.Message.size() + 2);
        line.append(record.Message);
    }
    else
    {
        // Without a formatter, calls the built-in layout directly (no default formatter object, which could be
        // destroyed at exit before a never-destroyed singleton logger writes its shutdown line)
        const auto* formatter = m_Config.Formatter.get();
        line = formatter != nullptr ? formatter->Format(record, m_Config) : TASWTextFormatter::FormatLine(record, m_Config);
        AppendLineEnding(line);
    }

    WriteLineUnlocked(record.LogLevel, line, !record.Raw);
    return line;
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteMemoryUsageInfo()
{
    WriteInfoLine(std::format("App Memory: {}", GetMemoryUsageString()));
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteOSInfo()
{
    WriteInfoLine(std::format("OS: {}", GetOSInfoString()));
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::WriteRecord

    Writes one entry passed to Write(), then calls OnLogEntry. Never throws, so that logging can't throw into the
    application: if writing fails (e.g. out of memory), the entry is dropped.
*/
void TASWTextLogBase::WriteRecord(const TASWLogRecord& record) noexcept
{
    try
    {
        std::string writtenLine;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (!EnsureReadyUnlocked())
                return;

            writtenLine = WriteLogEntry(record);

            AfterEntryUnlocked();
        }

        if (!writtenLine.empty())
            DispatchLogCallback(record, writtenLine);
    }
    catch (...)
    {
    }
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteSystemMemoryInfo()
{
    WriteInfoLine(std::format("System memory: {}", GetSystemMemoryUsageString()));
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteTimeInfo()
{
    WriteInfoLine(std::format("Time: {}", GetTimeInfoString()));
}

//---------------------------------------------------------------------------

} // namespace ASWLog
