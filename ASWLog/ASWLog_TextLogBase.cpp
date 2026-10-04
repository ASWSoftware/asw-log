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
    if (GetConfigUnlocked().Line.Ending == LineEnding::CRLF)
        line += "\r\n";
    else
        line += '\n';
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::Close() noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return CloseUnlocked();
    }
    catch (...)
    {
        return false;
    }
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::DispatchLogCallback

    Calls the OnLogEntry of 'config', the snapshot WriteRecord() took when it wrote the entry. Called after m_Mutex
    has been released, so a callback that logs again (or reconfigures the logger) doesn't deadlock.
*/
void TASWTextLogBase::DispatchLogCallback(const TASWLogConfig& config, const TASWLogRecord& record,
    std::string_view formattedLine) const noexcept
{
    try
    {
        config.OnLogEntry(record, formattedLine);
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

        // EnsureReadyUnlocked() reopens an output that is closed between entries (e.g. a file with
        // AutoOpenClosePerWrite)
        if (GetConfigUnlocked().Shutdown.WriteLine && EnsureReadyUnlocked())
        {
            std::string msg = "Logger shutdown: " + Time::ToISO8601String(NowUTC());

            if (!GetConfigUnlocked().Shutdown.Banner.empty())
                msg += ", " + GetConfigUnlocked().Shutdown.Banner;

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
bool TASWTextLogBase::Initialize(const TASWLogConfig& config) noexcept
{
    try
    {
        std::shared_ptr<const TASWLogConfig> previousConfig; // Released after the lock (see SetConfig())
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_IsInitialized.load(std::memory_order_acquire))
        {
            return false;
        }

        previousConfig = SetConfig(config);
        SetMinimumLevel(config.InitialMinimumLevel);

        if (!InitializeUnlocked())
        {
            return false;
        }

        // The output is ready, so the startup lines are best effort: if one throws (e.g. a formatter that throws, or
        // out of memory while gathering the system info), the rest are skipped but the logger is still initialized
        try
        {
            if (!GetConfigUnlocked().Startup.Banner.empty())
            {
                WriteInfoLine(GetConfigUnlocked().Startup.Banner);
            }

            WriteInitializationInfo();
        }
        catch (...)
        {
        }

        AfterEntryUnlocked();

        return true;
    }
    catch (...)
    {
        return false;
    }
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::IsOpen() const noexcept
{
    return m_IsOpen.load(std::memory_order_acquire);
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::Open() noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return OpenUnlocked();
    }
    catch (...)
    {
        return false;
    }
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::PrepareWriteUnlocked(std::chrono::system_clock::time_point /*now*/)
{
    return true;
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::Reconfigure(const TASWLogConfig& config) noexcept
{
    try
    {
        std::shared_ptr<const TASWLogConfig> previousConfig; // Released after the lock (see SetConfig())
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_IsInitialized.load(std::memory_order_acquire))
            return false;

        previousConfig = SetConfig(config);
        return ReconfigureUnlocked(*previousConfig);
    }
    catch (...)
    {
        return false;
    }
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::ReconfigureUnlocked(const TASWLogConfig& /*previous*/)
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

    if (GetConfigUnlocked().Startup.WriteCommandLine)
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
    if (GetConfigUnlocked().Startup.WriteTimeInfo)
        WriteTimeInfo();

    if (GetConfigUnlocked().Startup.WriteOSInfo)
        WriteOSInfo();

    if (GetConfigUnlocked().Startup.WriteDriveInfo)
        WriteDriveInfo();

    if (GetConfigUnlocked().Startup.WriteSystemMemoryInfo)
        WriteSystemMemoryInfo();

    if (GetConfigUnlocked().Startup.WriteApplicationInfo)
        WriteApplicationInfo();

    if (GetConfigUnlocked().Startup.WriteMemoryUsage)
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
        const auto& config = GetConfigUnlocked();
        const auto* formatter = config.Line.Formatter.get();
        line = formatter != nullptr ? formatter->Format(record, config) : TASWTextFormatter::FormatLine(record, config);
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

    Writes one entry passed to Write(), then calls OnLogEntry if it's set and the record's level meets
    OnLogEntryMinimumLevel. If writing throws (e.g. out of memory, or a formatter that throws), TASWLogBase::Write()
    drops the entry.
*/
void TASWTextLogBase::WriteRecord(const TASWLogRecord& record)
{
    std::string writtenLine;
    // Set only if OnLogEntry is called: the callback runs outside the lock, from the config the entry was written with
    std::shared_ptr<const TASWLogConfig> callbackConfig;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!EnsureReadyUnlocked())
            return;

        writtenLine = WriteLogEntry(record);

        AfterEntryUnlocked();

        const auto& config = GetConfigUnlocked();
        if (!writtenLine.empty() && config.OnLogEntry != nullptr && record.LogLevel >= config.OnLogEntryMinimumLevel)
            callbackConfig = GetConfigSnapshotUnlocked();
    }

    if (callbackConfig != nullptr)
        DispatchLogCallback(*callbackConfig, record, writtenLine);
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
