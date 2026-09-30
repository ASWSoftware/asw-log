/* **************************************************************************
ASWLog_Config.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Source for the ASWLog config options.

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

#ifndef ASWLog_ConfigH
#define ASWLog_ConfigH
//---------------------------------------------------------------------------
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_Types.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

struct TASWLogConfig
{
    // Seeds a logger's lock-free runtime level gate at Initialize() time only.
    // Use the logger's SetMinimumLevel()/GetMinimumLevel() to read or change the
    // effective level afterward; this field does not track later changes.
    Level InitialMinimumLevel = Level::Info;
    LineEnding LogLineEnding = LineEnding::LF;
    std::filesystem::path LogsFolderPath = "logs";
    std::filesystem::path LogFilePath = "aswlog.txt";
    std::string BannerMessage_Init;
    std::string BannerMessage_Shutdown;
    // Opens and closes the log file for every entry. Required when several processes write to one log file: with the
    // file kept open, other processes can't open it on Windows, and on POSIX they keep appending to the renamed backup
    // after a rotation.
    bool AutoOpenClosePerWrite = false;
    bool WriteShutdownLog = true;

    FlushMode LogFlushMode = FlushMode::EveryWrite;
    std::chrono::milliseconds FlushInterval{ 1000 };

    // Meta-data configuration for each log entry
    bool LogAppMem_WorkingSet = false;
    bool LogAppMem_PeakWorkingSet = false;
    bool LogLevelStr      = true;
    bool LogMethodName    = false;
    bool LogProcessId     = true;
    bool LogSourceLine    = false;
    bool LogThreadId      = true;
    bool LogUTCDateTime   = true;

    // Initialize output configuration
    bool Init_LogApplicationInfo = true;
    bool Init_LogCommandLine = false;
    bool Init_LogDriveInfo = true;
    bool Init_LogMemoryUsage = true;
    bool Init_LogOSInfo = true;
    bool Init_LogSysMemInfo = true;
    bool Init_LogTimeInfo = true;

    // Retry log entry options
    int OpenRetryCount = 5;
    std::chrono::milliseconds OpenRetryDelay{ 50 };

    // Circuit breaker for a log file that couldn't be reopened (e.g. after a rotation while another program held it).
    // Entries are dropped, without trying to open the file, until this long after the last failed attempt. The next
    // entry then tries again, with OpenRetryCount/OpenRetryDelay. 0 = try on every entry. Not used with
    // AutoOpenClosePerWrite, which opens the file for every entry anyway.
    std::chrono::milliseconds CircuitBreakerResetDelay{ 500 };

    // --- Log Rotation and Rolling Options ---
    bool EnableRotation       = false;
    std::uintmax_t MaxFileSizeBytes = 10 * 1024 * 1024; // Default 10MB
    // After a failed size rotation (e.g. another program holds the file), entries keep going to the current file, and
    // rotating isn't tried again until this long after the failure. 0 = try on every entry.
    std::chrono::milliseconds RotationRetryDelay{ 500 };
    // Rolls the file over at UTC midnight, and at Initialize() if the file was last written on an earlier UTC day. The
    // backup is named for the day it holds.
    bool EnableDailyRolling   = false;

    // --- Log Retention Options (applied automatically after a successful rotation) ---
    std::chrono::hours RetentionMaxAge{ 0 }; // 0 = disabled. When > 0, backups for this log older than this age are deleted after each rotation.

    // --- Log Entry Callback Options ---
    using LogCallback = std::function<void (Level level, std::string_view formattedLine)>;
    LogCallback OnLogEntry; // Optional hook invoked after a successful write (e.g. alerting/crash-reporting). Invoked outside the sink's internal lock; exceptions are swallowed.
    Level CallbackMinimumLevel = Level::Error; // Independent threshold gating OnLogEntry; unrelated to InitialMinimumLevel or the Force* APIs.

    [[nodiscard]] std::filesystem::path ResolveLogFileDir() const
    {
        return ResolveLogFilePath().parent_path();
    }

    [[nodiscard]] std::filesystem::path ResolveLogFilePath() const
    {
        auto candidate = LogFilePath.empty() ? std::filesystem::path("aswlog.txt") : LogFilePath;
        auto baseFolder = LogsFolderPath.empty() ? std::filesystem::path("logs") : LogsFolderPath;

        if (candidate.is_absolute())
        {
            return candidate.lexically_normal();
        }

        auto resolvedBase = baseFolder.lexically_normal();
        return (resolvedBase / candidate).lexically_normal();
    }
};

} // namespace ASWLog

#endif // #ifndef ASWLog_ConfigH
