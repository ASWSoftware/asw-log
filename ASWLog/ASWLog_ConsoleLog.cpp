/* **************************************************************************
ASWLog_ConsoleLog.cpp
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
#include "ASWLog_ConsoleLog.h"
//---------------------------------------------------------------------------
// System includes here
#include <filesystem>
#include <iostream>
#include <iterator>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

constexpr std::string_view AnsiReset = "\x1b[0m";

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWConsoleLog
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWConsoleLog::~TASWConsoleLog()
{
    Finalize();
}

//---------------------------------------------------------------------------
void TASWConsoleLog::AppendLineEnding(std::string& line)
{
    if (m_Config.LogLineEnding == LineEnding::CRLF)
        line += "\r\n";
    else
        line += '\n';
}

//---------------------------------------------------------------------------
bool TASWConsoleLog::Close()
{
    std::lock_guard<std::mutex> lock(m_ConsoleMutex);
    return CloseUnlocked();
}

//---------------------------------------------------------------------------
bool TASWConsoleLog::CloseUnlocked()
{
    m_IsOpen.store(false, std::memory_order_release);
    m_IsInitialized.store(false, std::memory_order_release);
    return true;
}

//---------------------------------------------------------------------------
std::array<std::string, LevelCount> TASWConsoleLog::DefaultLevelColors()
{
    return {
        "\x1b[90m",   // Trace - bright black / gray
        "\x1b[36m",   // Debug - cyan
        "\x1b[32m",   // Info - green
        "\x1b[33m",   // Warn - yellow
        "\x1b[31m",   // Error - red
        "\x1b[1;91m", // Critical - bold bright red
    };
}

//---------------------------------------------------------------------------
void TASWConsoleLog::EnableAnsiColorSupport()
{
#if defined(_WIN32)
    bool anySucceeded = false;
    const DWORD handleIds[] = { STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
    for (const auto handleId : handleIds)
    {
        HANDLE handle = GetStdHandle(handleId);
        if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
            continue;

        DWORD mode = 0;
        if (!GetConsoleMode(handle, &mode))
            continue;

        if (SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            anySucceeded = true;
    }

    m_ColorSupported.store(anySucceeded, std::memory_order_release);
#else
    // POSIX terminals accept ANSI escape codes natively; no explicit enabling is needed.
    m_ColorSupported.store(true, std::memory_order_release);
#endif
}

//---------------------------------------------------------------------------
void TASWConsoleLog::Finalize()
{
    std::lock_guard<std::mutex> lock(m_ConsoleMutex);
    if (!m_IsInitialized.load(std::memory_order_acquire))
        return;

    if (m_Config.WriteShutdownLog)
    {
        std::string msg = "Logger shutdown: " + Time::ToISO8601String(std::chrono::system_clock::now());

        if (!m_Config.BannerMessage_Shutdown.empty())
            msg += ", " + m_Config.BannerMessage_Shutdown;

        WriteLogEntry(Level::Info, msg, false, false, true, std::source_location::current());
    }

    CloseUnlocked();
}

//---------------------------------------------------------------------------
TASWConsoleLog& TASWConsoleLog::GetInstance()
{
    static TASWConsoleLog instance;
    return instance;
}

//---------------------------------------------------------------------------
std::string TASWConsoleLog::GetLevelColor(Level level) const
{
    std::lock_guard<std::mutex> lock(m_ConsoleMutex);
    return std::string(LevelColorUnlocked(level));
}

//---------------------------------------------------------------------------
bool TASWConsoleLog::GetUseColor() const noexcept
{
    return m_UseColor.load(std::memory_order_acquire);
}

//---------------------------------------------------------------------------
bool TASWConsoleLog::Initialize(const TASWLogConfig& config)
{
    std::lock_guard<std::mutex> lock(m_ConsoleMutex);
    if (m_IsInitialized.load(std::memory_order_acquire))
    {
        return false;
    }

    m_Config = config;
    m_MinimumLevel.store(m_Config.InitialMinimumLevel, std::memory_order_release);

    EnableAnsiColorSupport();

    m_IsOpen.store(true, std::memory_order_release);
    m_IsInitialized.store(true, std::memory_order_release);

    if (!m_Config.BannerMessage_Init.empty())
    {
        WriteLogEntry(Level::Info, m_Config.BannerMessage_Init, false, false, true, std::source_location::current());
    }

    WriteInitializationInfo();

    return true;
}

//---------------------------------------------------------------------------
bool TASWConsoleLog::IsColorSupported() const noexcept
{
    return m_ColorSupported.load(std::memory_order_acquire);
}

//---------------------------------------------------------------------------
bool TASWConsoleLog::IsOpen() const noexcept
{
    return m_IsOpen.load(std::memory_order_acquire);
}

//---------------------------------------------------------------------------
std::string_view TASWConsoleLog::LevelColorUnlocked(Level level) const noexcept
{
    const auto index = static_cast<std::size_t>(level);
    return index < m_LevelColors.size() ? std::string_view(m_LevelColors[index]) : std::string_view{};
}

//---------------------------------------------------------------------------
void TASWConsoleLog::Log(Level level, std::string_view message, std::source_location loc)
{
    if (level < GetMinimumLevel())
        return;

    std::string writtenLine;
    {
        std::lock_guard<std::mutex> lock(m_ConsoleMutex);
        if (!m_IsInitialized.load(std::memory_order_acquire) || !m_IsOpen.load(std::memory_order_acquire))
            return;

        writtenLine = WriteLogEntry(level, message, false, false, true, loc);
    }

    if (!writtenLine.empty())
        DispatchLogCallback(level, writtenLine);
}

//---------------------------------------------------------------------------
void TASWConsoleLog::LogForce(Level level, std::string_view message, std::source_location loc)
{
    std::string writtenLine;
    {
        std::lock_guard<std::mutex> lock(m_ConsoleMutex);
        if (!m_IsInitialized.load(std::memory_order_acquire) || !m_IsOpen.load(std::memory_order_acquire))
            return;

        writtenLine = WriteLogEntry(level, message, true, false, true, loc);
    }

    if (!writtenLine.empty())
        DispatchLogCallback(level, writtenLine);
}

//---------------------------------------------------------------------------
void TASWConsoleLog::LogForceRaw(Level level, std::string_view message, std::source_location loc)
{
    std::string writtenLine;
    {
        std::lock_guard<std::mutex> lock(m_ConsoleMutex);
        if (!m_IsInitialized.load(std::memory_order_acquire) || !m_IsOpen.load(std::memory_order_acquire))
            return;

        writtenLine = WriteLogEntry(level, message, true, true, false, loc);
    }

    if (!writtenLine.empty())
        DispatchLogCallback(level, writtenLine);
}

//---------------------------------------------------------------------------
void TASWConsoleLog::LogRaw(Level level, std::string_view message, std::source_location loc)
{
    if (level < GetMinimumLevel())
        return;

    std::string writtenLine;
    {
        std::lock_guard<std::mutex> lock(m_ConsoleMutex);
        if (!m_IsInitialized.load(std::memory_order_acquire) || !m_IsOpen.load(std::memory_order_acquire))
            return;

        writtenLine = WriteLogEntry(level, message, false, true, false, loc);
    }

    if (!writtenLine.empty())
        DispatchLogCallback(level, writtenLine);
}

//---------------------------------------------------------------------------
bool TASWConsoleLog::Open()
{
    std::lock_guard<std::mutex> lock(m_ConsoleMutex);
    return OpenUnlocked();
}

//---------------------------------------------------------------------------
bool TASWConsoleLog::OpenUnlocked()
{
    m_IsOpen.store(true, std::memory_order_release);
    m_IsInitialized.store(true, std::memory_order_release);
    return true;
}

//---------------------------------------------------------------------------
void TASWConsoleLog::ResetLevelColor(Level level) noexcept
{
    const auto index = static_cast<std::size_t>(level);
    if (index >= m_LevelColors.size())
        return;

    std::lock_guard<std::mutex> lock(m_ConsoleMutex);
    m_LevelColors[index] = DefaultLevelColors()[index];
}

//---------------------------------------------------------------------------
void TASWConsoleLog::ResetLevelColors() noexcept
{
    std::lock_guard<std::mutex> lock(m_ConsoleMutex);
    m_LevelColors = DefaultLevelColors();
}

//---------------------------------------------------------------------------
void TASWConsoleLog::SetLevelColor(Level level, std::string colorCode)
{
    const auto index = static_cast<std::size_t>(level);
    if (index >= m_LevelColors.size())
        return;

    std::lock_guard<std::mutex> lock(m_ConsoleMutex);
    m_LevelColors[index] = std::move(colorCode);
}

//---------------------------------------------------------------------------
void TASWConsoleLog::SetUseColor(bool useColor) noexcept
{
    m_UseColor.store(useColor, std::memory_order_release);
}

//---------------------------------------------------------------------------
void TASWConsoleLog::WriteApplicationInfo()
{
    auto applicationInfo = std::format("app_exe='{}', app_target=", GetExecutablePath().string());

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
#elif defined(__APPLE__)
    applicationInfo += "MacOSX";
#else
    applicationInfo += "Johnny5"; // Shouldn't get here
#endif

    if (m_Config.Init_LogCommandLine)
        applicationInfo += std::format(", command_line='{}'", GetCommandLineString());

    WriteLogEntry(Level::Info, std::format("App: {}", applicationInfo), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------
void TASWConsoleLog::WriteDriveInfo()
{
    WriteLogEntry(Level::Info,
        std::format("Drive: {}", GetDriveInfoString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------
void TASWConsoleLog::WriteInitializationInfo()
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
std::string TASWConsoleLog::WriteLogEntry(
    Level level, std::string_view message, bool force, bool raw, bool includeNewLine, std::source_location loc)
{
    if (!force && level < GetMinimumLevel())
        return {};

    std::string line;
    line.reserve(message.size() + 256);

    if (raw)
    {
        line.append(message);
        if (includeNewLine)
            AppendLineEnding(line);
    }
    else
    {
        const auto now = std::chrono::system_clock::now();

        if (m_Config.LogUTCDateTime)
            std::format_to(std::back_inserter(line), "[{}]", Time::ToISO8601String(now));

        if (m_Config.LogLevelStr)
            std::format_to(std::back_inserter(line), "[{}]", Level_ToString(level));

        if (m_Config.LogProcessId)
        {
#if defined(_WIN32)
            std::format_to(std::back_inserter(line), "[P:{}]", GetCurrentProcessId());
#else
            std::format_to(std::back_inserter(line), "[P:{}]", getpid());
#endif
        }

        if (m_Config.LogThreadId)
        {
            auto numericThreadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
            std::format_to(std::back_inserter(line), "[T:{}]", numericThreadId);
        }

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
        if (includeNewLine)
            AppendLineEnding(line);
    }

    auto& stream = (level >= Level::Warn) ? std::cerr : std::cout;
    const auto color = GetUseColor() ? LevelColorUnlocked(level) : std::string_view{};

    if (!color.empty())
        stream << color << line << AnsiReset;
    else
        stream << line;

    stream.flush();

    return line;
}

//---------------------------------------------------------------------------
void TASWConsoleLog::WriteMemoryUsageInfo()
{
    WriteLogEntry(Level::Info, std::format("App Memory: {}", GetMemoryUsageString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------
void TASWConsoleLog::WriteOSInfo()
{
    WriteLogEntry(Level::Info, std::format("OS: {}", GetOSInfoString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------
void TASWConsoleLog::WriteSystemMemoryInfo()
{
    WriteLogEntry(Level::Info,
        std::format("System memory: {}", GetSystemMemoryUsageString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------
void TASWConsoleLog::WriteTimeInfo()
{
    WriteLogEntry(Level::Info, std::format("Time: {}", GetTimeInfoString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------

} // namespace ASWLog
