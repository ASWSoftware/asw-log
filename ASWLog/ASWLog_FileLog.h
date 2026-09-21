/* **************************************************************************
ASWLog_FileLog.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Requires C++ 20 or higher.

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

#pragma once

#ifndef ASWLog_FileLogH
#define ASWLog_FileLogH
//---------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <ostream>
#include <string_view>
#include <streambuf>
#include <string>
//---------------------------------------------------------------------------
#include "ASWLog_Base.h"
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

protected:
    int_type overflow(int_type character = traits_type::eof()) override;
    std::streamsize xsputn(const char* data, std::streamsize size) override;
    int sync() override;

public:
    bool Open(const std::filesystem::path& path);
    bool Close();
    bool IsOpen() const noexcept;
    bool Write(std::string_view data);
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
    void Flush();
    bool Write(std::string_view data);
};


/////////////////////////////////////////////////////////////////////////////
// TASWFileLog
//
// Used for logging to a file.
/////////////////////////////////////////////////////////////////////////////
class TASWFileLog : public TASWLogBase
{
private:
    typedef TASWLogBase inherited;

private:
    TASWFileStream m_FileStream;
    std::mutex m_FileMutex; // Protects file write bounds across multiple threads
    std::string m_LastLogDateStr; // Stores YYYY-MM-DD state to detect structural calendar shifts
    std::atomic<bool> m_IsOpen{ false };
    std::chrono::steady_clock::time_point m_LastFlushTime{};

private:
    void AppendLineEnding(std::string& line);
    bool CloseUnlocked();
    bool EnsureOpen();
    void Finalize();
    bool FlushUnlocked();
    void MaybeFlush(bool isNewLine);
    bool OpenUnlocked();
    bool RotateLogFilesUnlocked(std::string_view reasonTag);
    void WriteApplicationInfo();
    void WriteDriveInfo();
    void WriteInitializationInfo();
    std::string WriteLogEntry(Level level, std::string_view message, bool force, bool raw, bool includeNewLine, std::source_location loc);
    void WriteMemoryUsageInfo();
    void WriteOSInfo();
    void WriteSystemMemoryInfo();
    void WriteTimeInfo();

protected:
    std::string_view GetLoggerClassName() const noexcept final
    {
        return "TASWFileLog";
    }

public: // Static methods
    static std::size_t DeleteOldLogs(const std::filesystem::path& logDir, std::string_view pattern, std::chrono::hours maxAge);
    static TASWFileLog& GetInstance(); // Singleton support for the common static instance

public:
    TASWFileLog() = default;
    ~TASWFileLog();

    bool Initialize(const TASWLogConfig& config) override;

    bool Open() override;
    bool Close() override;
    bool IsOpen() const noexcept override;
    bool Flush();

    bool RotateLogFiles(std::string_view reasonTag = "manual");

    void Log(Level level, std::string_view message, std::source_location loc = std::source_location::current()) override;
    void LogRaw(Level level, std::string_view message, std::source_location loc = std::source_location::current()) override;

    void LogForce(Level level, std::string_view message, std::source_location loc = std::source_location::current()) override;
    void LogForceRaw(Level level, std::string_view message, std::source_location loc = std::source_location::current()) override;
};

} // namespace ASWLog

#endif // ASWLog_FileLogH
