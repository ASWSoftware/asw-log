/* **************************************************************************
ASWLog_FileLog.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Requires C++ 20 or higher.

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

#pragma once

#ifndef ASWLog_FileLogH
#define ASWLog_FileLogH
//---------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <ostream>
#include <string_view>
#include <streambuf>
#include <string>
#include <system_error>
//---------------------------------------------------------------------------
#include "ASWLog_TextLogBase.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWFileStreamBuf
//
// A file stream buffer.
/////////////////////////////////////////////////////////////////////////////
class TASWFileStreamBuf final : public std::streambuf
{
private:
    typedef std::streambuf inherited;

private:
    std::FILE* m_File = nullptr;
    std::uintmax_t m_Size = 0; // See GetSize()

protected:
    int_type overflow(int_type character = traits_type::eof()) override;
    std::streamsize xsputn(const char* data, std::streamsize size) override;
    int sync() override;

public:
    bool Open(const std::filesystem::path& path);
    bool Close();
    // The file's size: its size when opened plus the bytes written since. Tracked here because on Windows the size
    // read through the file's path (e.g. std::filesystem::file_size) isn't updated while the file is open.
    std::uintmax_t GetSize() const noexcept;
    bool IsOpen() const noexcept;
    // Asks the system to write the file's data out to the disk (FlushFileBuffers on Windows, fsync on POSIX). Only
    // what has been flushed out of this buffer is synced, so call pubsync() first. Sets 'errorCode' if it fails.
    bool SyncToDisk(std::error_code& errorCode);
    bool Write(std::string_view data);
    // Writes 'data' straight to the end of the file (write() on POSIX, WriteFile() on Windows), bypassing the stdio
    // buffer, so it is async-signal-safe: for a crash handler (see TASWTextLogBase::WriteCrashLineDirect()). Doesn't
    // count the bytes in GetSize().
    bool WriteDirect(std::string_view data) noexcept;
};


/////////////////////////////////////////////////////////////////////////////
// TASWFileStream
//
// A file stream wrapper.
/////////////////////////////////////////////////////////////////////////////
class TASWFileStream final : public std::ostream
{
private:
    typedef std::ostream inherited;

private:
    TASWFileStreamBuf m_Buffer;

public:
    TASWFileStream();
    ~TASWFileStream();
    bool Open(const std::filesystem::path& path);
    bool Close();
    bool IsOpen() const noexcept;
    bool Flush();
    std::uintmax_t GetSize() const noexcept; // See TASWFileStreamBuf::GetSize()
    bool SyncToDisk(std::error_code& errorCode); // See TASWFileStreamBuf::SyncToDisk()
    bool Write(std::string_view data);
    bool WriteDirect(std::string_view data) noexcept; // See TASWFileStreamBuf::WriteDirect()
};


/////////////////////////////////////////////////////////////////////////////
// TASWFileLog
//
// Used for logging to a file.
/////////////////////////////////////////////////////////////////////////////
class TASWFileLog : public TASWTextLogBase
{
private:
    typedef TASWTextLogBase inherited;

private:
    // m_Mutex (see TASWTextLogBase) protects all of these
    TASWFileStream m_FileStream;
    std::string m_LastLogDateStr; // Stores YYYY-MM-DD state to detect structural calendar shifts
    std::chrono::system_clock::time_point m_LastOpenFailure{}; // NowUTC() when opening the file last failed
    std::chrono::system_clock::time_point m_LastRotationFailure{}; // NowUTC() when a rotation last failed

    // File.SyncToDiskAtLevel is Critical or lower, for WriteCrashLineDirect(), which may run without m_Mutex
    std::atomic<bool> m_SyncsCrashLine{ false };

private:
    void CloseFileUnlocked(); // Closes the file but, unlike CloseUnlocked(), leaves the logger initialized
    void DeleteOldBackups(const TASWLogConfig& config); // Runs without m_Mutex
    void MaybeFlush(Level level, bool isNewLine);
    // Report a file failure: without m_Mutex (see ReportError()), or holding it (see ReportErrorUnlocked()). Never throw.
    void ReportFileError(const TASWLogConfig& config, ErrorKind kind, std::string_view message, const std::filesystem::path& path, std::error_code errorCode) noexcept;
    void ReportFileErrorUnlocked(ErrorKind kind, std::string_view message, const std::filesystem::path& path, std::error_code errorCode) noexcept;
    void RotateDailyLogFromEarlierDayUnlocked();
    bool RotateLogFilesUnlocked(std::string_view reasonTag, std::string_view timeLabel);
    void SyncToDiskUnlocked();

protected: // TASWTextLogBase hooks
    void AfterEntryUnlocked() override;
    void AfterQueuedEntriesUnlocked(bool mustFlush) override;
    bool CloseUnlocked() override;
    bool EnsureReadyUnlocked() override;
    void FlushForCrashUnlocked() noexcept override;
    bool FlushUnlocked() override;
    std::chrono::milliseconds GetWorkerIntervalUnlocked() const override; // FlushMode::Periodic
    bool InitializeUnlocked() override;
    void OnWorkerWakeUnlocked() override;
    bool OpenUnlocked() override;
    bool PrepareWriteUnlocked(std::chrono::system_clock::time_point now) override;
    bool ReconfigureUnlocked(const TASWLogConfig& previous) override;
    void WriteCrashLineDirect(std::string_view line) noexcept override;
    void WriteLineUnlocked(Level level, std::string_view line, bool endsLine) override;

protected:
    std::string_view GetLoggerClassName() const noexcept final
    {
        return "TASWFileLog";
    }

public: // Static methods
    // Deletes the files in 'logDir' (not its subfolders) whose names match the wildcard 'pattern' ('*' and '?', matched
    // as UTF-8) and that were last written more than 'maxAge' ago. Returns how many it deleted. Deletes nothing if
    // 'pattern' is empty (pass "*" for every file) or if 'logDir' is a root folder (see IsRootFolder()). Never throws.
    static std::size_t DeleteOldLogs(const std::filesystem::path& logDir, std::string_view pattern, std::chrono::hours maxAge);
    // Singleton support for the common static instance. The instance is never destroyed, so it is safe to use until
    // the process ends, e.g. from another static object's destructor or a thread still running at exit. At exit it is
    // finalized (shutdown entry, flush, close, and its FlushMode::Periodic thread stopped) in static destruction order:
    // a static object constructed after the first GetInstance() call can still log from its destructor, while one
    // constructed before it is destroyed after the finalize, so what it logs is dropped. Leak checkers that list memory
    // still allocated at exit (e.g. the MSVC debug heap's report) include the instance. In a Windows DLL (or package)
    // that is unloaded before the process ends, Close() it before unloading when using FlushMode::Periodic: stopping a
    // thread while the DLL is being unloaded can deadlock.
    static TASWFileLog& GetInstance();

public:
    TASWFileLog() = default;
    ~TASWFileLog();

    // Renames the log file to "<stem>.<reasonTag>.<YYYY-MM-DD_HHMMSS_mmm>.bak" (the time in Line.TimestampZone, UTC by
    // default), adding "_1", "_2", ... to the time if that name is taken, so an existing backup is never replaced. Then
    // reopens the log if it was open. A 'reasonTag' that contains '.' makes a backup that TASWFileConfig's backup
    // cleanup doesn't recognize, so it is kept.
    bool RotateLogFiles(std::string_view reasonTag = "manual");
};

} // namespace ASWLog

#endif // ASWLog_FileLogH
