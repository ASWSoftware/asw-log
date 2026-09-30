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
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#include <share.h>
#include <sys/stat.h>
#include <windows.h>
#undef min
#undef max
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

// How many "_N" suffixes are tried when a backup name is taken, before rotation gives up.
constexpr int MaxBackupNameSuffix = 1000;

// Returns the first unused backup path for 'logPath': "<stem>.<reasonTag>.<timeLabel>.bak", or with "_1", "_2", ...
// appended to the time label if that is taken. Returns an empty path if every name is taken or the file system
// can't be checked, so an existing backup is never replaced.
std::filesystem::path FindFreeBackupPath(
    const std::filesystem::path& logPath, std::string_view reasonTag, std::string_view timeLabel)
{
    for (int suffix = 0; suffix <= MaxBackupNameSuffix; ++suffix)
    {
        auto candidate = logPath;
        if (suffix == 0)
            candidate.replace_extension(std::format(".{}.{}.bak", reasonTag, timeLabel));
        else
            candidate.replace_extension(std::format(".{}.{}_{}.bak", reasonTag, timeLabel, suffix));

        std::error_code errorCode;
        const bool taken = std::filesystem::exists(candidate, errorCode);
        if (errorCode)
            return {};

        if (!taken)
            return candidate;
    }

    return {};
}

// Returns the size of an open file, read through its handle, or 0 if that fails. Unlike the size read through the
// file's path, this is current on Windows while the file is open.
std::uintmax_t GetOpenFileSize(std::FILE* file)
{
#if defined(_WIN32)
    struct _stat64 fileStatus {};
    if (_fstat64(_fileno(file), &fileStatus) == 0)
        return static_cast<std::uintmax_t>(fileStatus.st_size);
#else
    struct stat fileStatus {};
    if (fstat(fileno(file), &fileStatus) == 0)
        return static_cast<std::uintmax_t>(fileStatus.st_size);
#endif

    return 0;
}

// True if an operation that last failed at 'lastFailure' may be tried again at 'now', 'retryDelay' later. Also true if
// the clock went backwards, so that a clock change can't postpone the retry.
bool IsRetryDue(std::chrono::system_clock::time_point lastFailure, std::chrono::system_clock::time_point now,
    std::chrono::milliseconds retryDelay)
{
    return now < lastFailure || now - lastFailure >= retryDelay;
}

// Formats a UTC time for a backup file name: "YYYY-MM-DD_HHMMSS_mmm".
std::string ToBackupTimeLabel(std::chrono::system_clock::time_point timePoint)
{
    // Cut from the ISO 8601 form "YYYY-MM-DDTHH:mm:ss.mmmZ", leaving out the characters not wanted in a file name
    const auto isoTime = Time::ToISO8601String(timePoint);
    const std::string_view iso(isoTime);
    return std::format("{}_{}{}{}_{}", iso.substr(0, 10), iso.substr(11, 2), iso.substr(14, 2), iso.substr(17, 2),
        iso.substr(20, 3));
}

} // namespace

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
    m_Size = 0;
    return flushResult == 0 && closeResult == 0;
}

//---------------------------------------------------------------------------
std::uintmax_t TASWFileStreamBuf::GetSize() const noexcept
{
    return m_Size;
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

    // Child processes don't get the file: 'N' makes the handle non-inheritable (on Windows an inherited handle would
    // block renaming the file during rotation), and 'e' closes it when a child starts another program (O_CLOEXEC).
    // Both are set as the file opens, so a child started by another thread meanwhile can't get it either.
#if defined(_WIN32)
    m_File = _wfsopen(path.c_str(), L"abN", _SH_DENYWR);
#else
    m_File = std::fopen(path.c_str(), "abe");
#endif
    if (m_File == nullptr)
        return false;

    m_Size = GetOpenFileSize(m_File);
    return true;
}

//---------------------------------------------------------------------------
bool TASWFileStreamBuf::Write(std::string_view data)
{
    if (m_File == nullptr)
        return false;

    const auto written = std::fwrite(data.data(), 1, data.size(), m_File);
    m_Size += written;
    return written == data.size();
}

//---------------------------------------------------------------------------
TASWFileStreamBuf::int_type TASWFileStreamBuf::overflow(int_type character)
{
    if (m_File == nullptr)
        return traits_type::eof();

    if (character != traits_type::eof())
    {
        if (std::fputc(character, m_File) == EOF)
            return traits_type::eof();

        ++m_Size;
    }

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

    const auto written = std::fwrite(data, 1, static_cast<std::size_t>(size), m_File);
    m_Size += written;
    return static_cast<std::streamsize>(written);
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
std::uintmax_t TASWFileStream::GetSize() const noexcept
{
    return m_Buffer.GetSize();
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
    // Never throws: uses the std::error_code overloads, skipping an entry it can't read and stopping if the folder can't
    // be listed. File names are matched as UTF-8, since the ANSI code page conversion of path::string() can throw.

    // An empty pattern would match every file; a caller who means that passes "*"
    if (pattern.empty())
        return 0;

    std::error_code errorCode;
    if (!std::filesystem::is_directory(logDir, errorCode))
        return 0;

    // Never clean up a root folder, such as "C:\", "\\server\share\" or "/"
    if (IsRootFolder(logDir))
        return 0;

    const auto cutoff = std::filesystem::file_time_type::clock::now() - maxAge;
    std::size_t deletedCount = 0;

    std::filesystem::directory_iterator entries(logDir, errorCode);
    for (const std::filesystem::directory_iterator end; !errorCode && entries != end; entries.increment(errorCode))
    {
        const auto& entry = *entries;

        std::error_code entryError;
        if (!entry.is_regular_file(entryError))
            continue;

        if (!MatchesWildcard(PathToUTF8String(entry.path().filename()), pattern))
            continue;

        const auto lastWrite = entry.last_write_time(entryError);
        if (!entryError && lastWrite < cutoff)
        {
            if (std::filesystem::remove(entry.path(), entryError) && !entryError)
                ++deletedCount;
        }
    }

    return deletedCount;
}

//---------------------------------------------------------------------------
/*
    TASWFileLog::EnsureOpenForWriteUnlocked

    Makes sure the log file is open for the next write. Returns false if the entry can't be written.
*/
bool TASWFileLog::EnsureOpenForWriteUnlocked()
{
    const bool isInitialized = m_IsInitialized.load(std::memory_order_acquire);
    if (isInitialized && m_FileStream.IsOpen())
        return true;

    // In this mode the file is opened for every write
    if (m_Config.AutoOpenClosePerWrite)
        return OpenUnlocked();

    // Initialized but closed only happens when the file couldn't be reopened (e.g. after a rotation), since Close()
    // also clears the initialized state. Circuit breaker: try again once CircuitBreakerResetDelay has passed since the
    // last failed attempt, instead of paying for a failed open on every write while the file stays unavailable.
    if (isInitialized && IsRetryDue(m_LastOpenFailure, NowUTC(), m_Config.CircuitBreakerResetDelay))
        return OpenUnlocked();

    return false;
}

//---------------------------------------------------------------------------
void TASWFileLog::Finalize() noexcept
{
    // Called from the destructor, where an exception would terminate the program
    try
    {
        std::lock_guard<std::mutex> lock(m_FileMutex);
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
    // Never deleted, so the instance stays usable through static destruction. It is only finalized at exit, by a
    // handler registered right after it is created, which runs where a static instance's destructor would have run.
    static TASWFileLog* const instance = new TASWFileLog();
    [[maybe_unused]] static const int atExitResult = std::atexit([] {
            instance->Finalize();
        });
    return *instance;
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

    if (m_Config.EnableDailyRolling)
        RotateLeftoverDailyLogUnlocked();

    if (!OpenUnlocked())
    {
        return false;
    }

    m_LastLogDateStr = Time::ToDateString(NowUTC());

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

    LogEntry(level, message, false, false, true, loc);
}

//---------------------------------------------------------------------------
/*
    TASWFileLog::LogEntry

    Writes one entry for the public Log* methods, then calls OnLogEntry. Never throws, so that logging can't throw
    into the application: if writing fails (e.g. out of memory), the entry is dropped.
*/
void TASWFileLog::LogEntry(
    Level level, std::string_view message, bool force, bool raw, bool includeNewLine, std::source_location loc) noexcept
{
    try
    {
        std::string writtenLine;
        {
            std::lock_guard<std::mutex> lock(m_FileMutex);
            if (!EnsureOpenForWriteUnlocked())
                return;

            writtenLine = WriteLogEntry(level, message, force, raw, includeNewLine, loc);

            if (m_Config.AutoOpenClosePerWrite)
                CloseUnlocked();
        }

        if (!writtenLine.empty())
            DispatchLogCallback(level, writtenLine);
    }
    catch (...)
    {
    }
}

//---------------------------------------------------------------------------
void TASWFileLog::LogForce(Level level, std::string_view message, std::source_location loc)
{
    LogEntry(level, message, true, false, true, loc);
}

//---------------------------------------------------------------------------
void TASWFileLog::LogForceRaw(Level level, std::string_view message, std::source_location loc)
{
    LogEntry(level, message, true, true, false, loc);
}

//---------------------------------------------------------------------------
void TASWFileLog::LogRaw(Level level, std::string_view message, std::source_location loc)
{
    if (level < GetMinimumLevel())
        return;

    LogEntry(level, message, false, true, false, loc);
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
        m_LastOpenFailure = NowUTC();
        return false;
    }

    if (m_LastLogDateStr.empty())
    {
        m_LastLogDateStr = Time::ToDateString(NowUTC());
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
    return RotateLogFilesUnlocked(reasonTag, ToBackupTimeLabel(NowUTC()));
}

//---------------------------------------------------------------------------
/*
    TASWFileLog::RotateLeftoverDailyLogUnlocked

    Called by Initialize() with daily rolling on, before the log file is opened. If the file already has entries from
    an earlier UTC day (e.g. the app was restarted the next morning), rotates it to a daily backup named for that day,
    as the midnight rollover would have, so they aren't mixed into today's file. If the rotation fails, logging still
    appends to the file.
*/
void TASWFileLog::RotateLeftoverDailyLogUnlocked()
{
    const auto logPath = m_Config.ResolveLogFilePath();

    std::error_code errorCode;
    const auto fileSize = std::filesystem::file_size(logPath, errorCode);
    if (errorCode || fileSize == 0)
        return;

    const auto lastWriteTime = std::filesystem::last_write_time(logPath, errorCode);
    if (errorCode)
        return;

    // Converts the file time to the logger's UTC clock through the file's age, since the standard libraries don't share
    // a file_clock conversion (clock_cast is missing from libc++, to_sys from MSVC's library). This also follows the
    // NowUTC() override.
    const auto now = NowUTC();
    const auto fileAge = std::filesystem::file_time_type::clock::now() - lastWriteTime;
    const auto lastWriteUTC = now - std::chrono::duration_cast<std::chrono::system_clock::duration>(fileAge);

    // YYYY-MM-DD strings compare in date order
    const auto lastWriteDateStr = Time::ToDateString(lastWriteUTC);
    if (lastWriteDateStr < Time::ToDateString(now))
        RotateLogFilesUnlocked("daily", lastWriteDateStr);
}

//---------------------------------------------------------------------------
bool TASWFileLog::RotateLogFilesUnlocked(std::string_view reasonTag, std::string_view timeLabel)
{
    const bool wasOpen = m_FileStream.IsOpen();
    if (wasOpen)
    {
        m_FileStream.Close();
        m_IsOpen.store(false, std::memory_order_release);
    }

    const auto logPath = m_Config.ResolveLogFilePath();

    std::error_code errorCode;
    if (std::filesystem::exists(logPath, errorCode))
    {
        const auto backupPath = FindFreeBackupPath(logPath, reasonTag, timeLabel);
        if (backupPath.empty())
            errorCode = std::make_error_code(std::errc::file_exists);
        else
            std::filesystem::rename(logPath, backupPath, errorCode);
    }

    if (errorCode)
    {
        m_LastRotationFailure = NowUTC();
        if (wasOpen)
            OpenUnlocked();
        return false;
    }

    if (m_Config.RetentionMaxAge.count() > 0)
    {
        DeleteOldLogs(m_Config.ResolveLogFileDir(),
            std::format("{}.*.bak", PathToUTF8String(m_Config.ResolveLogFilePath().stem())), m_Config.RetentionMaxAge);
    }

    if (wasOpen)
        return OpenUnlocked();

    return true;
}

//---------------------------------------------------------------------------
void TASWFileLog::WriteApplicationInfo()
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
std::string TASWFileLog::WriteLogEntry(
    Level level, std::string_view message, bool force, bool raw, bool includeNewLine, std::source_location loc)
{
    if (!force && level < GetMinimumLevel())
        return {};

    const auto now = NowUTC();

    if (m_Config.EnableDailyRolling)
    {
        const auto currentDateStr = Time::ToDateString(now);
        if (currentDateStr != m_LastLogDateStr)
        {
            // Name the backup for the day its content is from, not the day that just started
            RotateLogFilesUnlocked("daily", m_LastLogDateStr);
            m_LastLogDateStr = currentDateStr;
        }
    }

    // Uses the size tracked by the stream, since on Windows the size read through the path isn't current while open.
    // After a failed rotation (e.g. another program holds the file), waits RotationRetryDelay before trying again.
    if (m_Config.EnableRotation && m_FileStream.IsOpen() && m_FileStream.GetSize() >= m_Config.MaxFileSizeBytes &&
        IsRetryDue(m_LastRotationFailure, now, m_Config.RotationRetryDelay))
    {
        RotateLogFilesUnlocked("size", ToBackupTimeLabel(now));
    }

    if (!m_FileStream.IsOpen())
        return {};

    std::string line;
    line.reserve(message.size() + 256);

    if (raw)
    {
        line.append(message);
        if (includeNewLine)
            AppendLineEnding(line);
        m_FileStream.Write(line);
        MaybeFlush(includeNewLine);
        return line;
    }

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

    m_FileStream.Write(line);

    MaybeFlush(includeNewLine);
    return line;
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
