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
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <share.h>
#include <windows.h>
#undef min
#undef max
#else
#include <unistd.h>
#endif
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWFileStreamBuf
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
bool TASWFileStreamBuf::Close()
{
    if (m_File == nullptr)
        return true;

    const auto flushResult = std::fflush(m_File);
    const auto closeResult = std::fclose(m_File);
    m_File = nullptr;
    return flushResult == 0 && closeResult == 0;
}

//---------------------------------------------------------------------------
bool TASWFileStreamBuf::IsOpen() const noexcept
{
    return m_File != nullptr;
}

//---------------------------------------------------------------------------
bool TASWFileStreamBuf::Open(const std::filesystem::path& path)
{
    Close();
#if defined(_WIN32)
    m_File = _wfsopen(path.c_str(), L"ab", _SH_DENYWR);
#else
    m_File = std::fopen(path.c_str(), "ab");
#endif
    return m_File != nullptr;
}

//---------------------------------------------------------------------------
bool TASWFileStreamBuf::Write(std::string_view data)
{
    if (m_File == nullptr)
        return false;

    return std::fwrite(data.data(), 1, data.size(), m_File) == data.size();
}

//---------------------------------------------------------------------------
TASWFileStreamBuf::int_type TASWFileStreamBuf::overflow(int_type character)
{
    if (m_File == nullptr)
        return traits_type::eof();

    if (character != traits_type::eof() && std::fputc(character, m_File) == EOF)
        return traits_type::eof();

    return character;
}

//---------------------------------------------------------------------------
int TASWFileStreamBuf::sync()
{
    return m_File != nullptr && std::fflush(m_File) == 0 ? 0 : -1;
}

//---------------------------------------------------------------------------
std::streamsize TASWFileStreamBuf::xsputn(const char* data, std::streamsize size)
{
    if (m_File == nullptr || size <= 0)
        return 0;

    return static_cast<std::streamsize>(std::fwrite(data, 1, static_cast<std::size_t>(size), m_File));
}

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWFileStream
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWFileStream::TASWFileStream()
    : inherited(&m_Buffer)
{
}

//---------------------------------------------------------------------------
TASWFileStream::~TASWFileStream()
{
    Close();
}

//---------------------------------------------------------------------------
bool TASWFileStream::Close()
{
    const auto result = m_Buffer.Close();
    if (!result)
        setstate(std::ios::failbit);
    return result;
}

//---------------------------------------------------------------------------
void TASWFileStream::Flush()
{
    flush();
}

//---------------------------------------------------------------------------
bool TASWFileStream::IsOpen() const noexcept
{
    return m_Buffer.IsOpen();
}

//---------------------------------------------------------------------------
bool TASWFileStream::Open(const std::filesystem::path& path)
{
    clear();
    const auto result = m_Buffer.Open(path);
    if (!result)
        setstate(std::ios::failbit);
    return result;
}

//---------------------------------------------------------------------------
bool TASWFileStream::Write(std::string_view data)
{
    const auto result = m_Buffer.Write(data);
    if (!result)
        setstate(std::ios::failbit);
    return result;
}

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
void TASWFileLog::AppendLineEnding(std::string& line)
{
    if (m_Config.LogLineEnding == LineEnding::CRLF)
        line += "\r\n";
    else
        line += '\n';
}

//---------------------------------------------------------------------------
bool TASWFileLog::Close()
{
    std::lock_guard<std::mutex> lock(m_FileMutex);
    return CloseUnlocked();
}

//---------------------------------------------------------------------------
bool TASWFileLog::CloseUnlocked()
{
    if (m_FileStream.IsOpen())
    {
        m_FileStream.Flush();
        m_FileStream.Close();
    }

    m_IsOpen.store(false, std::memory_order_release);
    m_IsInitialized.store(false, std::memory_order_release);
    return true;
}

//---------------------------------------------------------------------------
std::size_t TASWFileLog::DeleteOldLogs(
    const std::filesystem::path& logDir, std::string_view pattern, std::chrono::hours maxAge)
{
    if (!std::filesystem::exists(logDir) || !std::filesystem::is_directory(logDir))
        return 0;

    // Don't allow root directory, such as "C:\"
    if (logDir.native().length() <= 3)
        return 0;

    const auto cutoff = std::filesystem::file_time_type::clock::now() - maxAge;
    std::size_t deletedCount = 0;

    for (const auto& entry : std::filesystem::directory_iterator(logDir))
    {
        if (!entry.is_regular_file())
            continue;

        const auto filename = entry.path().filename().string();
        const auto match = pattern.empty() || MatchesWildcard(filename, pattern);
        if (!match)
            continue;

        const auto lastWrite = std::filesystem::last_write_time(entry.path());
        if (lastWrite < cutoff)
        {
            std::error_code errorCode;
            if (std::filesystem::remove(entry.path(), errorCode) && !errorCode)
                ++deletedCount;
        }
    }

    return deletedCount;
}

//---------------------------------------------------------------------------
bool TASWFileLog::EnsureOpen()
{
    if (m_FileStream.IsOpen())
        return true;

    return OpenUnlocked();
}

//---------------------------------------------------------------------------
void TASWFileLog::Finalize()
{
    std::lock_guard<std::mutex> lock(m_FileMutex);
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
bool TASWFileLog::Flush()
{
    std::lock_guard<std::mutex> lock(m_FileMutex);
    return FlushUnlocked();
}

//---------------------------------------------------------------------------
bool TASWFileLog::FlushUnlocked()
{
    if (!m_FileStream.IsOpen())
        return false;

    m_FileStream.Flush();
    m_LastFlushTime = std::chrono::steady_clock::now();
    return m_FileStream.good();
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
    std::lock_guard<std::mutex> lock(m_FileMutex);
    if (m_IsInitialized.load(std::memory_order_acquire))
    {
        return false;
    }

    m_Config = config;
    m_MinimumLevel.store(m_Config.InitialMinimumLevel, std::memory_order_release);

    if (!OpenUnlocked())
    {
        return false;
    }

    m_LastLogDateStr = Time::ToDateString(std::chrono::system_clock::now());

    if (!m_Config.BannerMessage_Init.empty())
    {
        WriteLogEntry(Level::Info, m_Config.BannerMessage_Init, false, false, true, std::source_location::current());
    }

    WriteInitializationInfo();

    if (m_Config.AutoOpenClosePerWrite)
        CloseUnlocked();

    return true;
}

//---------------------------------------------------------------------------
bool TASWFileLog::IsOpen() const noexcept
{
    return m_IsOpen.load(std::memory_order_acquire);
}

//---------------------------------------------------------------------------
void TASWFileLog::Log(Level level, std::string_view message, std::source_location loc)
{
    if (level < GetMinimumLevel())
        return;

    std::lock_guard<std::mutex> lock(m_FileMutex);
    if (!m_IsInitialized.load(std::memory_order_acquire) || !m_FileStream.IsOpen())
    {
        if (m_Config.AutoOpenClosePerWrite)
        {
            if (!OpenUnlocked())
                return;
        }
        else
        {
            return;
        }
    }

    WriteLogEntry(level, message, false, false, true, loc);

    if (m_Config.AutoOpenClosePerWrite)
        CloseUnlocked();
}

//---------------------------------------------------------------------------
void TASWFileLog::LogForce(Level level, std::string_view message, std::source_location loc)
{
    std::lock_guard<std::mutex> lock(m_FileMutex);
    if (!m_IsInitialized.load(std::memory_order_acquire) || !m_FileStream.IsOpen())
    {
        if (m_Config.AutoOpenClosePerWrite)
        {
            if (!OpenUnlocked())
                return;
        }
        else
        {
            return;
        }
    }

    WriteLogEntry(level, message, true, false, true, loc);

    if (m_Config.AutoOpenClosePerWrite)
        CloseUnlocked();
}

//---------------------------------------------------------------------------
void TASWFileLog::LogForceRaw(Level level, std::string_view message, std::source_location loc)
{
    std::lock_guard<std::mutex> lock(m_FileMutex);
    if (!m_IsInitialized.load(std::memory_order_acquire) || !m_FileStream.IsOpen())
    {
        if (m_Config.AutoOpenClosePerWrite)
        {
            if (!OpenUnlocked())
                return;
        }
        else
        {
            return;
        }
    }

    WriteLogEntry(level, message, true, true, false, loc);

    if (m_Config.AutoOpenClosePerWrite)
        CloseUnlocked();
}

//---------------------------------------------------------------------------
void TASWFileLog::LogRaw(Level level, std::string_view message, std::source_location loc)
{
    if (level < GetMinimumLevel())
        return;

    std::lock_guard<std::mutex> lock(m_FileMutex);

    if (!m_IsInitialized.load(std::memory_order_acquire) || !m_FileStream.IsOpen())
    {
        if (m_Config.AutoOpenClosePerWrite)
        {
            if (!OpenUnlocked())
                return;
        }
        else
        {
            return;
        }
    }

    WriteLogEntry(level, message, false, true, false, loc);

    if (m_Config.AutoOpenClosePerWrite)
        CloseUnlocked();
}

//---------------------------------------------------------------------------
void TASWFileLog::MaybeFlush(bool isNewLine)
{
    const auto flushMode = m_Config.LogFlushMode;
    if (flushMode == FlushMode::EveryWrite || (flushMode == FlushMode::OnNewLine && isNewLine))
    {
        FlushUnlocked();
        return;
    }

    if (flushMode == FlushMode::Periodic)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - m_LastFlushTime >= m_Config.FlushInterval)
            FlushUnlocked();
    }
}

//---------------------------------------------------------------------------
bool TASWFileLog::Open()
{
    std::lock_guard<std::mutex> lock(m_FileMutex);
    return OpenUnlocked();
}

//---------------------------------------------------------------------------
bool TASWFileLog::OpenUnlocked()
{
    if (m_FileStream.IsOpen())
    {
        m_IsOpen.store(true, std::memory_order_release);
        m_IsInitialized.store(true, std::memory_order_release);
        return true;
    }

    const auto effectivePath = m_Config.ResolveLogFilePath();
    if (!effectivePath.parent_path().empty())
    {
        std::error_code errorCode;
        std::filesystem::create_directories(effectivePath.parent_path(), errorCode);
    }

    const auto retryCount = std::max<int>(1, m_Config.OpenRetryCount);
    for (int attempt = 0; attempt < retryCount; ++attempt)
    {
        m_FileStream.clear();
        m_FileStream.Open(effectivePath);
        if (m_FileStream.IsOpen())
            break;

        if (attempt + 1 < retryCount && m_Config.OpenRetryDelay.count() > 0)
            std::this_thread::sleep_for(m_Config.OpenRetryDelay);
    }

    if (!m_FileStream.IsOpen())
    {
        m_IsOpen.store(false, std::memory_order_release);
        return false;
    }

    if (m_LastLogDateStr.empty())
    {
        m_LastLogDateStr = Time::ToDateString(std::chrono::system_clock::now());
    }

    m_IsOpen.store(true, std::memory_order_release);
    m_IsInitialized.store(true, std::memory_order_release);
    m_LastFlushTime = std::chrono::steady_clock::now();
    return true;
}

//---------------------------------------------------------------------------
bool TASWFileLog::RotateLogFiles(std::string_view reasonTag)
{
    std::lock_guard<std::mutex> lock(m_FileMutex);
    return RotateLogFilesUnlocked(reasonTag);
}

//---------------------------------------------------------------------------
bool TASWFileLog::RotateLogFilesUnlocked(std::string_view reasonTag)
{
    const bool wasOpen = m_FileStream.IsOpen();
    if (wasOpen)
    {
        m_FileStream.Close();
        m_IsOpen.store(false, std::memory_order_release);
    }

    const auto now = std::chrono::system_clock::now();
    const auto timeStr = Time::ToDateString(now);
    auto backupPath = m_Config.ResolveLogFilePath();
    backupPath.replace_extension(std::format(".{}.{}.bak", reasonTag, timeStr));

    std::error_code errorCode;
    std::filesystem::remove(backupPath, errorCode);
    errorCode.clear();
    if (std::filesystem::exists(m_Config.ResolveLogFilePath(), errorCode))
    {
        std::filesystem::rename(m_Config.ResolveLogFilePath(), backupPath, errorCode);
    }

    if (errorCode)
    {
        if (wasOpen)
            OpenUnlocked();
        return false;
    }

    if (m_Config.RetentionMaxAge.count() > 0)
    {
        DeleteOldLogs(m_Config.ResolveLogFileDir(),
            std::format("{}.*.bak", m_Config.ResolveLogFilePath().stem().string()), m_Config.RetentionMaxAge);
    }

    if (wasOpen)
        return OpenUnlocked();

    return true;
}

//---------------------------------------------------------------------------
void TASWFileLog::WriteApplicationInfo()
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
void TASWFileLog::WriteDriveInfo()
{
    WriteLogEntry(Level::Info,
        std::format("Drive: {}", GetDriveInfoString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------
void TASWFileLog::WriteInitializationInfo()
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
void TASWFileLog::WriteLogEntry(
    Level level, std::string_view message, bool force, bool raw, bool includeNewLine, std::source_location loc)
{
    if (!force && level < GetMinimumLevel())
        return;

    auto now = std::chrono::system_clock::now();

    if (m_Config.EnableDailyRolling)
    {
        const auto currentDateStr = Time::ToDateString(now);
        if (currentDateStr != m_LastLogDateStr)
        {
            RotateLogFilesUnlocked("daily");
            m_LastLogDateStr = currentDateStr;
        }
    }

    const auto effectivePath = m_Config.ResolveLogFilePath();

    if (m_Config.EnableRotation && std::filesystem::exists(effectivePath))
    {
        if (std::filesystem::file_size(effectivePath) >= m_Config.MaxFileSizeBytes)
        {
            RotateLogFilesUnlocked("size");
        }
    }

    if (!m_FileStream.IsOpen())
        return;

    std::string line;
    line.reserve(message.size() + 256);

    if (raw)
    {
        line.append(message);
        if (includeNewLine)
            AppendLineEnding(line);
        m_FileStream.Write(line);
        MaybeFlush(includeNewLine);
        return;
    }

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

    m_FileStream.Write(line);

    MaybeFlush(includeNewLine);
}

//---------------------------------------------------------------------------
void TASWFileLog::WriteMemoryUsageInfo()
{
    WriteLogEntry(Level::Info, std::format("App Memory: {}", GetMemoryUsageString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------
void TASWFileLog::WriteOSInfo()
{
    WriteLogEntry(Level::Info, std::format("OS: {}", GetOSInfoString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------
void TASWFileLog::WriteSystemMemoryInfo()
{
    WriteLogEntry(Level::Info,
        std::format("System memory: {}", GetSystemMemoryUsageString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------
void TASWFileLog::WriteTimeInfo()
{
    WriteLogEntry(Level::Info, std::format("Time: {}", GetTimeInfoString()), false, false, true, std::source_location::current());
}

//---------------------------------------------------------------------------

} // namespace ASWLog
