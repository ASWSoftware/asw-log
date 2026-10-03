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
#include <mutex>
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

// How many times rotation picks a free backup name and tries to rename the log to it, if another process takes that
// name first each time.
constexpr int MaxBackupRenameAttempts = 10;

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
void TASWFileLog::AfterEntryUnlocked()
{
    // In this mode the file is only open while an entry (or Initialize()'s startup lines) is written
    if (m_Config.AutoOpenClosePerWrite)
        CloseUnlocked();
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
    TASWFileLog::EnsureReadyUnlocked

    Makes sure the log file is open for the next write. Returns false if the entry can't be written.
*/
bool TASWFileLog::EnsureReadyUnlocked()
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
bool TASWFileLog::InitializeUnlocked()
{
    if (m_Config.EnableDailyRolling)
        RotateDailyLogFromEarlierDayUnlocked();

    if (!OpenUnlocked())
    {
        return false;
    }

    m_LastLogDateStr = Time::ToDateString(NowUTC());
    return true;
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
/*
    TASWFileLog::PrepareWriteUnlocked

    Rotates the log file if the day has changed or the file has reached its maximum size, as configured. Returns false
    if the file isn't open (e.g. it couldn't be reopened after a rotation), which drops the line.
*/
bool TASWFileLog::PrepareWriteUnlocked(std::chrono::system_clock::time_point now)
{
    if (m_Config.EnableDailyRolling)
    {
        const auto currentDateStr = Time::ToDateString(now);
        if (currentDateStr != m_LastLogDateStr)
        {
            // Name the backup for the day its content is from, not the day that just started. A log shared with other
            // processes may already have been rolled over by one of them, so there the file's last write decides.
            if (m_Config.AutoOpenClosePerWrite)
                RotateDailyLogFromEarlierDayUnlocked();
            else
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

    return m_FileStream.IsOpen();
}

//---------------------------------------------------------------------------
bool TASWFileLog::RotateLogFiles(std::string_view reasonTag)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return RotateLogFilesUnlocked(reasonTag, ToBackupTimeLabel(NowUTC()));
}

//---------------------------------------------------------------------------
/*
    TASWFileLog::RotateDailyLogFromEarlierDayUnlocked

    For daily rolling: if the log file has entries from an earlier UTC day than today, going by its last write time,
    rotates it to a daily backup named for that day, so they aren't mixed into today's file. If the rotation fails,
    logging still appends to the file.

    Called by Initialize() before the file is opened (e.g. the app was restarted the next morning), and at the midnight
    rollover when the log may be shared with other processes (AutoOpenClosePerWrite): if one of them has already rolled
    it over and written today, the file is from today, and it is left alone. The file is closed between writes in that
    mode, so its last write time read through the path is current, also on Windows.
*/
void TASWFileLog::RotateDailyLogFromEarlierDayUnlocked()
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
        // Another process sharing the log may take the free name first, so on "already exists" the next one is tried
        for (int attempt = 0; attempt < MaxBackupRenameAttempts; ++attempt)
        {
            const auto backupPath = FindFreeBackupPath(logPath, reasonTag, timeLabel);
            if (backupPath.empty())
            {
                errorCode = std::make_error_code(std::errc::file_exists);
                break;
            }

            errorCode = RenameWithoutReplacing(logPath, backupPath);
            if (errorCode != std::errc::file_exists)
                break;
        }
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
void TASWFileLog::WriteLineUnlocked(Level /*level*/, std::string_view line, bool endsLine)
{
    m_FileStream.Write(line);
    MaybeFlush(endsLine);
}

//---------------------------------------------------------------------------

} // namespace ASWLog
