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
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>

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

// True if the environment variable 'name' is set to a non-empty value.
bool IsEnvironmentVariableSet(const char* name) noexcept
{
#if defined(_WIN32)
    // With no buffer, returns the size the value needs including its terminating null (1 for an empty value), or 0
    // if the variable isn't set. Avoids getenv(), which MSVC warns about.
    return GetEnvironmentVariableA(name, nullptr, 0) > 1;
#else
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0';
#endif
}

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
bool TASWConsoleLog::DetectStreamColorSupport(bool isStdErr)
{
#if defined(_WIN32)
    const HANDLE handle = GetStdHandle(isStdErr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
        return false;

    // Fails if the stream isn't a console, e.g. when it's redirected to a file or a pipe
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode))
        return false;

    // Consoles before Windows 10 don't accept virtual terminal processing, and would print the codes literally
    return (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0 ||
        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    // A terminal (not a file or a pipe), and not one that declares it can't show colors
    if (isatty(isStdErr ? STDERR_FILENO : STDOUT_FILENO) == 0)
        return false;

    const char* term = std::getenv("TERM");
    return term == nullptr || std::string_view(term) != "dumb";
#endif
}

//---------------------------------------------------------------------------
void TASWConsoleLog::Finalize() noexcept
{
    // Called from the destructor, where an exception would terminate the program
    try
    {
        std::lock_guard<std::mutex> lock(m_ConsoleMutex);
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
ColorMode TASWConsoleLog::GetColorMode() const noexcept
{
    return m_ColorMode.load(std::memory_order_acquire);
}

//---------------------------------------------------------------------------
TASWConsoleLog& TASWConsoleLog::GetInstance()
{
    // Never deleted, so the instance stays usable through static destruction. It is only finalized at exit, by a
    // handler registered right after it is created, which runs where a static instance's destructor would have run.
    static TASWConsoleLog* const instance = new TASWConsoleLog();
    [[maybe_unused]] static const int atExitResult = std::atexit([] {
            instance->Finalize();
        });
    return *instance;
}

//---------------------------------------------------------------------------
std::string TASWConsoleLog::GetLevelColor(Level level) const
{
    std::lock_guard<std::mutex> lock(m_ConsoleMutex);
    return std::string(LevelColorUnlocked(level));
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

    m_StdOutColorSupported.store(DetectStreamColorSupport(false), std::memory_order_release);
    m_StdErrColorSupported.store(DetectStreamColorSupport(true), std::memory_order_release);
    m_NoColorRequested.store(IsEnvironmentVariableSet("NO_COLOR"), std::memory_order_release);

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
    return m_StdOutColorSupported.load(std::memory_order_acquire) || m_StdErrColorSupported.load(std::memory_order_acquire);
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

    LogEntry(level, message, false, false, true, loc);
}

//---------------------------------------------------------------------------
/*
    TASWConsoleLog::LogEntry

    Writes one entry for the public Log* methods, then calls OnLogEntry. Never throws, so that logging can't throw
    into the application: if writing fails (e.g. out of memory), the entry is dropped.
*/
void TASWConsoleLog::LogEntry(
    Level level, std::string_view message, bool force, bool raw, bool includeNewLine, std::source_location loc) noexcept
{
    try
    {
        std::string writtenLine;
        {
            std::lock_guard<std::mutex> lock(m_ConsoleMutex);
            if (!m_IsInitialized.load(std::memory_order_acquire) || !m_IsOpen.load(std::memory_order_acquire))
                return;

            writtenLine = WriteLogEntry(level, message, force, raw, includeNewLine, loc);
        }

        if (!writtenLine.empty())
            DispatchLogCallback(level, writtenLine);
    }
    catch (...)
    {
    }
}

//---------------------------------------------------------------------------
void TASWConsoleLog::LogForce(Level level, std::string_view message, std::source_location loc)
{
    LogEntry(level, message, true, false, true, loc);
}

//---------------------------------------------------------------------------
void TASWConsoleLog::LogForceRaw(Level level, std::string_view message, std::source_location loc)
{
    LogEntry(level, message, true, true, false, loc);
}

//---------------------------------------------------------------------------
void TASWConsoleLog::LogRaw(Level level, std::string_view message, std::source_location loc)
{
    if (level < GetMinimumLevel())
        return;

    LogEntry(level, message, false, true, false, loc);
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
void TASWConsoleLog::SetColorMode(ColorMode colorMode) noexcept
{
    m_ColorMode.store(colorMode, std::memory_order_release);
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
bool TASWConsoleLog::ShouldColorStream(bool isStdErr) const noexcept
{
    switch (GetColorMode())
    {
        case ColorMode::Always:
            return true;

        case ColorMode::Never:
            return false;

        case ColorMode::Auto:
            break;
    }

    const auto& isSupported = isStdErr ? m_StdErrColorSupported : m_StdOutColorSupported;
    return isSupported.load(std::memory_order_acquire) && !m_NoColorRequested.load(std::memory_order_acquire);
}

//---------------------------------------------------------------------------
void TASWConsoleLog::WriteApplicationInfo()
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
#elif defined(__APPLE__)
    applicationInfo += "MacOSX";
#else
#error "ASWLog: Unrecognized target platform in WriteApplicationInfo()"
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
        const auto now = NowUTC();

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
        if (includeNewLine)
            AppendLineEnding(line);
    }

    const bool toStdErr = level >= Level::Warn;
    auto& stream = toStdErr ? std::cerr : std::cout;
    const auto color = ShouldColorStream(toStdErr) ? LevelColorUnlocked(level) : std::string_view{};

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
