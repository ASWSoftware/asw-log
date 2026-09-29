/* **************************************************************************
Test_ASWLog_FileLog.cpp
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
#include "Test_ASWLog_FileLog.h"
//---------------------------------------------------------------------------
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <share.h>
#else
#include <unistd.h>
#endif
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_FileLog.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

const auto GroupBaseTempDir = std::filesystem::temp_directory_path() / "aswlog_tests";
const auto TestTempDir = GroupBaseTempDir / "test";

// A TASWFileLog whose clock the test sets, through TASWLogBase's NowUTC() hook
class TFixedClockFileLog : public ASWLog::TASWFileLog
{
public:
    std::chrono::system_clock::time_point CurrentTime{};

protected:
    std::chrono::system_clock::time_point NowUTC() const noexcept override
    {
        return CurrentTime;
    }
};

// While alive, stops the log file from being renamed (so a rotation fails), and optionally from being opened for
// writing, like a virus scanner or another program holding the file. Windows: a second handle without delete sharing
// blocks the rename, and the read-only attribute blocks the open. POSIX: a read-only folder blocks the rename, and a
// read-only file blocks the open (neither stops the root user).
class TFileBlocker
{
private:
    std::filesystem::path m_File;
    bool m_BlockOpen;
#if defined(_WIN32)
    std::FILE* m_OtherHandle = nullptr;
#endif

    static constexpr auto WritePerms = std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
        std::filesystem::perms::others_write;

public:
    TFileBlocker(const std::filesystem::path& file, bool blockOpen)
        : m_File(file),
          m_BlockOpen(blockOpen)
    {
#if defined(_WIN32)
        m_OtherHandle = _wfsopen(file.c_str(), L"rb", _SH_DENYNO);
#else
        std::filesystem::permissions(file.parent_path(), WritePerms, std::filesystem::perm_options::remove);
#endif
        if (m_BlockOpen)
            std::filesystem::permissions(file, WritePerms, std::filesystem::perm_options::remove);
    }

    ~TFileBlocker()
    {
        std::error_code errorCode;
        if (m_BlockOpen)
            std::filesystem::permissions(m_File, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, errorCode);
#if defined(_WIN32)
        if (m_OtherHandle != nullptr)
            std::fclose(m_OtherHandle);
#else
        std::filesystem::permissions(m_File.parent_path(), std::filesystem::perms::owner_write, std::filesystem::perm_options::add, errorCode);
#endif
    }

    TFileBlocker(const TFileBlocker&) = delete;
    TFileBlocker& operator=(const TFileBlocker&) = delete;
};

// True if TFileBlocker can work: file permissions don't stop the root user on POSIX
bool CanBlockFiles()
{
#if defined(_WIN32)
    return true;
#else
    return geteuid() != 0;
#endif
}

// A config for the rotation tests, with no line metadata or startup info, so each line is just its message
ASWLog::TASWLogConfig MakeRotationTestConfig(const std::filesystem::path& logFile)
{
    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.LogUTCDateTime = false;
    config.LogLevelStr = false;
    config.LogProcessId = false;
    config.LogThreadId = false;
    config.LogMethodName = false;
    config.LogSourceLine = false;
    config.Init_LogTimeInfo = false;
    config.Init_LogOSInfo = false;
    config.Init_LogDriveInfo = false;
    config.Init_LogSysMemInfo = false;
    config.Init_LogApplicationInfo = false;
    config.Init_LogMemoryUsage = false;
    config.OpenRetryCount = 1;
    config.WriteShutdownLog = false;
    return config;
}

std::string ReadFileText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};

    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
TTest_ASWLog_FileLog::TTest_ASWLog_FileLog()
    : inherited("ASWLog_FileLog_Tests")
{
    RegisterTest(&TTest_ASWLog_FileLog::Test_DailyRolling_KeepsExistingBackupForSameDate, "DailyRolling_KeepsExistingBackupForSameDate");
    RegisterTest(&TTest_ASWLog_FileLog::Test_DailyRolling_NamesBackupForContentDate, "DailyRolling_NamesBackupForContentDate");
    RegisterTest(&TTest_ASWLog_FileLog::Test_DeleteOldLogs_MatchesNonASCIIFileNames, "DeleteOldLogs_MatchesNonASCIIFileNames");
    RegisterTest(&TTest_ASWLog_FileLog::Test_DeleteOldLogs_RemovesOldFiles, "DeleteOldLogs_RemovesOldFiles");
    RegisterTest(&TTest_ASWLog_FileLog::Test_FailedReopen_RetriesAndResumesLogging, "FailedReopen_RetriesAndResumesLogging");
    RegisterTest(&TTest_ASWLog_FileLog::Test_FailedReopen_ZeroResetDelayRetriesOnNextWrite, "FailedReopen_ZeroResetDelayRetriesOnNextWrite");
    RegisterTest(&TTest_ASWLog_FileLog::Test_FailedSizeRotation_WaitsBeforeRetrying, "FailedSizeRotation_WaitsBeforeRetrying");
    RegisterTest(&TTest_ASWLog_FileLog::Test_GetInstance_ReturnsSameInstance, "GetInstance_ReturnsSameInstance");
    RegisterTest(&TTest_ASWLog_FileLog::Test_InitializeAndLogInfo_WritesText, "InitializeAndLogInfo_WritesText");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Initialize_SuppressesInfoBannersBelowMinimumLevel, "Initialize_SuppressesInfoBannersBelowMinimumLevel");
    RegisterTest(&TTest_ASWLog_FileLog::Test_LogFormatMethods_FormatsMessage, "LogFormatMethods_FormatsMessage");
    RegisterTest(&TTest_ASWLog_FileLog::Test_LogFormatMethods_WriteCallerSourceLine, "LogFormatMethods_WriteCallerSourceLine");
    RegisterTest(&TTest_ASWLog_FileLog::Test_LogLineMetadata_Options, "LogLineMetadata_Options");
    RegisterTest(&TTest_ASWLog_FileLog::Test_LogNewLineAndForceOptions, "LogNewLineAndForceOptions");
    RegisterTest(&TTest_ASWLog_FileLog::Test_LogRawOptions, "LogRawOptions");
    RegisterTest(&TTest_ASWLog_FileLog::Test_MultiThreadedStress_WritesAllMessagesToDisk, "MultiThreadedStress_WritesAllMessagesToDisk");
    RegisterTest(&TTest_ASWLog_FileLog::Test_MultiThreadedStress_WritesAllMessagesToDisk_OpenClose, "MultiThreadedStress_WritesAllMessagesToDisk_OpenClose");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnLogEntry_FiresForQualifyingLevelsOnly, "OnLogEntry_FiresForQualifyingLevelsOnly");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnLogEntry_ReentrantCallbackDoesNotDeadlock, "OnLogEntry_ReentrantCallbackDoesNotDeadlock");
    RegisterTest(&TTest_ASWLog_FileLog::Test_RetentionMaxAge_DefaultDisabledPreservesOldBackups, "RetentionMaxAge_DefaultDisabledPreservesOldBackups");
    RegisterTest(&TTest_ASWLog_FileLog::Test_RetentionMaxAge_DeletesExpiredBackupsAfterRotation, "RetentionMaxAge_DeletesExpiredBackupsAfterRotation");
    RegisterTest(&TTest_ASWLog_FileLog::Test_RotateLogFiles_KeepsEveryBackup, "RotateLogFiles_KeepsEveryBackup");
    RegisterTest(&TTest_ASWLog_FileLog::Test_SizeRotation_AutoOpenCloseCountsOtherWriters, "SizeRotation_AutoOpenCloseCountsOtherWriters");
    RegisterTest(&TTest_ASWLog_FileLog::Test_SizeRotation_CountsExistingFileSize, "SizeRotation_CountsExistingFileSize");
    RegisterTest(&TTest_ASWLog_FileLog::Test_SizeRotation_RotatesWhenLimitReached, "SizeRotation_RotatesWhenLimitReached");
}
//---------------------------------------------------------------------------
TTest_ASWLog_FileLog::~TTest_ASWLog_FileLog()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::SetUp_Group()
{
    Log("Setting up temp group folder: " + GroupBaseTempDir.string());
    std::filesystem::create_directories(GroupBaseTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::SetUp_Test(ITestCase& testCase)
{
    Log("  Setting up temp folder for " + testCase.GetName() + ": " + TestTempDir.string());
    std::filesystem::create_directories(TestTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::TearDown_Group()
{
    Log("Cleaning up temp group folder:" + GroupBaseTempDir.string());
    std::filesystem::remove_all(GroupBaseTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::TearDown_Test(ITestCase& testCase)
{
    Log("  Cleaning up temp folder for " + testCase.GetName() + ": " + TestTempDir.string());
    std::filesystem::remove_all(TestTempDir);
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_DailyRolling_KeepsExistingBackupForSameDate()
{
    // Arrange
    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "rolling_existing.log";
    auto config = MakeRotationTestConfig(logFile);
    config.EnableDailyRolling = true;

    // A backup for the same day already exists, e.g. left by an earlier run
    const auto existingBackup = TestTempDir / "rolling_existing.daily.2026-01-15.bak";
    {
        std::ofstream existingStream(existingBackup);
        existingStream << "earlier_backup";
    }

    const auto dayOne = std::chrono::sys_days{ 2026y / 1 / 15 };
    TFixedClockFileLog logger;
    logger.CurrentTime = dayOne + 23h + 59min;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("day_one_entry");
    logger.CurrentTime = dayOne + 24h + 30s;
    logger.LogInfo("day_two_entry");
    logger.Close();

    // Assert
    const auto newBackupContents = ReadFileText(TestTempDir / "rolling_existing.daily.2026-01-15_1.bak");
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckEquals(std::string("earlier_backup"), ReadFileText(existingBackup), __func__, __LINE__, "Daily rolling should not replace an existing backup for the same day");
    CheckTrue(newBackupContents.find("day_one_entry") != std::string::npos, __func__, __LINE__, "Daily rolling should add _1 to the name when the day's backup already exists");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_DailyRolling_NamesBackupForContentDate()
{
    // Arrange
    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "rolling.log";
    auto config = MakeRotationTestConfig(logFile);
    config.EnableDailyRolling = true;

    const auto dayOne = std::chrono::sys_days{ 2026y / 1 / 15 };
    TFixedClockFileLog logger;
    logger.CurrentTime = dayOne + 23h + 59min;

    // Act: log just before and just after UTC midnight
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("day_one_entry");
    logger.CurrentTime = dayOne + 24h + 30s;
    logger.LogInfo("day_two_entry");
    logger.Close();

    // Assert
    const auto backupContents = ReadFileText(TestTempDir / "rolling.daily.2026-01-15.bak");
    const auto currentContents = ReadFileText(logFile);
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(backupContents.find("day_one_entry") != std::string::npos, __func__, __LINE__, "The daily backup should be named for the day its entries are from");
    CheckTrue(backupContents.find("day_two_entry") == std::string::npos, __func__, __LINE__, "The daily backup should not contain entries from the new day");
    CheckFalse(std::filesystem::exists(TestTempDir / "rolling.daily.2026-01-16.bak"), __func__, __LINE__, "The daily backup should not be named for the day that just started");
    CheckTrue(currentContents.find("day_two_entry") != std::string::npos, __func__, __LINE__, "The new day's entries should go to the reopened log file");
    CheckTrue(currentContents.find("day_one_entry") == std::string::npos, __func__, __LINE__, "The reopened log file should not contain the previous day's entries");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_DeleteOldLogs_MatchesNonASCIIFileNames()
{
    // Arrange: a file name with a character outside the Windows ANSI code page (Greek small lambda, U+03BB, which is
    // CE BB in UTF-8). Written with escapes, and without a u8"" literal: MSVC builds turned both the raw character and
    // u8"\xCE\xBB" into a different name. A wide hex escape is a UTF-16 code unit on every Windows compiler, and a
    // POSIX file name is just bytes.
#if defined(_WIN32)
    const std::filesystem::path oldFileName = L"\x03BB_old_example.log";
#else
    const std::filesystem::path oldFileName = "\xCE\xBB_old_example.log";
#endif
    const auto oldFile = TestTempDir / oldFileName;
    {
        std::ofstream oldStream(oldFile);
        oldStream << "old";
    }
    std::filesystem::last_write_time(oldFile, std::chrono::file_clock::now() - std::chrono::hours(2));

    const std::string utf8Pattern = "\xCE\xBB_*example.log"; // U+03BB in UTF-8, then "_*example.log"
    std::size_t deletedCount = 0;
    bool threw = false;

    // Act
    try
    {
        deletedCount = ASWLog::TASWFileLog::DeleteOldLogs(TestTempDir, utf8Pattern, std::chrono::hours(1));
    }
    catch (...)
    {
        threw = true;
    }

    // Assert
    CheckFalse(threw, __func__, __LINE__, "DeleteOldLogs should not throw for a file name the ANSI code page can't represent");
    CheckEquals(static_cast<size_t>(1), deletedCount, __func__, __LINE__, "DeleteOldLogs should match UTF-8 patterns against UTF-8 file names");
    CheckFalse(std::filesystem::exists(oldFile), __func__, __LINE__, "The matching old file should be removed");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_DeleteOldLogs_RemovesOldFiles()
{
    // Arrange
    const auto oldFile = TestTempDir / "old_example.log";
    {
        std::ofstream oldStream(oldFile);
        oldStream << "old";
    }

    const auto newFile = TestTempDir / "new_example.log";
    {
        std::ofstream newStream(newFile);
        newStream << "new";
    }

    const auto oldWriteTime = std::chrono::file_clock::now() - std::chrono::hours(2);
    std::filesystem::last_write_time(oldFile, oldWriteTime);
    std::filesystem::last_write_time(newFile, std::chrono::file_clock::now());

    // Act
    const auto deletedCount = ASWLog::TASWFileLog::DeleteOldLogs(TestTempDir, "*example.log", std::chrono::hours(1));

    // Assert
    CheckEquals(static_cast<size_t>(1), deletedCount, __func__, __LINE__, "DeleteOldLogs should remove the stale matching file");
    CheckFalse(std::filesystem::exists(oldFile), __func__, __LINE__, "Old log file should be removed");
    CheckTrue(std::filesystem::exists(newFile), __func__, __LINE__, "Recent log file should remain");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_FailedReopen_RetriesAndResumesLogging()
{
    // Arrange
    if (!CanBlockFiles())
        Skip(__func__, __LINE__, "File permissions don't stop the root user, so the failure can't be simulated");

    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "reopen.log";
    auto config = MakeRotationTestConfig(logFile);
    config.CircuitBreakerResetDelay = 200ms;

    const auto startTime = std::chrono::sys_days{ 2026y / 1 / 15 } + 10h;
    TFixedClockFileLog logger;
    logger.CurrentTime = startTime;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("before_outage");

    bool rotated = true;
    bool openDuringOutage = true;
    {
        // The rotation's rename fails, and so does reopening the log afterward
        TFileBlocker blocker(logFile, true);
        rotated = logger.RotateLogFiles("manual");
        openDuringOutage = logger.IsOpen();
        logger.LogInfo("during_outage");
    }

    // The file is available again, but the circuit breaker waits CircuitBreakerResetDelay before trying it
    logger.CurrentTime = startTime + 100ms;
    logger.LogInfo("too_soon");
    logger.CurrentTime = startTime + 250ms;
    logger.LogInfo("after_recovery");
    const bool openAfterRecovery = logger.IsOpen();
    logger.Close();

    // Assert
    const auto contents = ReadFileText(logFile);
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckFalse(rotated, __func__, __LINE__, "RotateLogFiles should fail while the file is held");
    CheckFalse(openDuringOutage, __func__, __LINE__, "The log should report closed after it couldn't be reopened");
    CheckTrue(contents.find("before_outage") != std::string::npos, __func__, __LINE__, "Entries before the outage should be kept");
    CheckTrue(contents.find("during_outage") == std::string::npos, __func__, __LINE__, "Entries while the file can't be opened are dropped");
    CheckTrue(contents.find("too_soon") == std::string::npos, __func__, __LINE__, "The reopen should not be retried before CircuitBreakerResetDelay has passed");
    CheckTrue(contents.find("after_recovery") != std::string::npos, __func__, __LINE__, "Logging should resume once CircuitBreakerResetDelay has passed and the file can be opened");
    CheckTrue(openAfterRecovery, __func__, __LINE__, "The log should report open again after recovering");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_FailedReopen_ZeroResetDelayRetriesOnNextWrite()
{
    // Arrange
    if (!CanBlockFiles())
        Skip(__func__, __LINE__, "File permissions don't stop the root user, so the failure can't be simulated");

    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "reopen_zero.log";
    auto config = MakeRotationTestConfig(logFile);
    config.CircuitBreakerResetDelay = 0ms;

    TFixedClockFileLog logger;
    logger.CurrentTime = std::chrono::sys_days{ 2026y / 1 / 15 } + 10h;

    // Act: the clock doesn't move, so only a zero delay lets the next write retry
    const bool initialized = logger.Initialize(config);
    bool rotated = true;
    {
        TFileBlocker blocker(logFile, true);
        rotated = logger.RotateLogFiles("manual");
    }
    logger.LogInfo("next_write");
    logger.Close();

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckFalse(rotated, __func__, __LINE__, "RotateLogFiles should fail while the file is held");
    CheckTrue(ReadFileText(logFile).find("next_write") != std::string::npos, __func__, __LINE__, "With CircuitBreakerResetDelay 0, the next write should reopen the file");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_FailedSizeRotation_WaitsBeforeRetrying()
{
    // Arrange
    if (!CanBlockFiles())
        Skip(__func__, __LINE__, "File permissions don't stop the root user, so the failure can't be simulated");

    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "rotate_retry.log";
    auto config = MakeRotationTestConfig(logFile);
    config.EnableRotation = true;
    config.MaxFileSizeBytes = 50;
    config.RotationRetryDelay = 200ms;

    const auto startTime = std::chrono::sys_days{ 2026y / 1 / 15 } + 10h;
    TFixedClockFileLog logger;
    logger.CurrentTime = startTime;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("line_one_long_enough_to_reach_the_size_limit_alone");
    {
        // The size rotation's rename fails, but the log reopens, so the entry is still written
        TFileBlocker blocker(logFile, false);
        logger.LogInfo("line_two");
    }

    // Still within RotationRetryDelay: no new rotation attempt yet
    logger.CurrentTime = startTime + 100ms;
    logger.LogInfo("line_three");
    logger.CurrentTime = startTime + 250ms;
    logger.LogInfo("line_four");
    logger.Close();

    // Assert
    std::size_t backupCount = 0;
    std::string backupContents;
    for (const auto& entry : std::filesystem::directory_iterator(TestTempDir))
    {
        const auto name = entry.path().filename().string();
        if (name.starts_with("rotate_retry.size.") && name.ends_with(".bak"))
        {
            ++backupCount;
            backupContents = ReadFileText(entry.path());
        }
    }

    const auto currentContents = ReadFileText(logFile);
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckEquals(static_cast<size_t>(1), backupCount, __func__, __LINE__, "The rotation should succeed once RotationRetryDelay has passed");
    CheckTrue(backupContents.find("line_two") != std::string::npos, __func__, __LINE__, "The entry written when the rotation failed should still reach the file");
    CheckTrue(backupContents.find("line_three") != std::string::npos, __func__, __LINE__, "A failed rotation should not be retried before RotationRetryDelay has passed");
    CheckTrue(currentContents.find("line_four") != std::string::npos, __func__, __LINE__, "The entry after the retry interval should go to the new log file");
    CheckTrue(currentContents.find("line_three") == std::string::npos, __func__, __LINE__, "The new log file should only hold entries after the successful rotation");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_GetInstance_ReturnsSameInstance()
{
    // Act
    auto& first = ASWLog::TASWFileLog::GetInstance();
    auto& second = ASWLog::TASWFileLog::GetInstance();

    // Assert
    CheckTrue(&first == &second, __func__, __LINE__, "GetInstance should return the same logger on every call");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_InitializeAndLogInfo_WritesText()
{
    // Arrange
    const auto logFile = TestTempDir / "aswlog_runtime.log";

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = logFile;
    config.LogUTCDateTime = false;
    config.LogLevelStr = false;
    config.LogProcessId = false;
    config.LogThreadId = false;
    config.LogAppMem_WorkingSet = false;
    config.LogAppMem_PeakWorkingSet = false;
    config.LogMethodName = false;
    config.LogSourceLine = false;
    config.Init_LogTimeInfo = false;
    config.Init_LogOSInfo = false;
    config.Init_LogDriveInfo = false;
    config.Init_LogSysMemInfo = false;
    config.Init_LogApplicationInfo = false;
    config.Init_LogMemoryUsage = false;
    config.OpenRetryCount = 1;

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("unit_test_message");
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(logger.IsOpen() == false, __func__, __LINE__, "Logger should be closed after explicit close");
    CheckTrue(contents.find("unit_test_message") != std::string::npos, __func__, __LINE__, "Logged file should contain the test message");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Initialize_SuppressesInfoBannersBelowMinimumLevel()
{
    // Arrange
    const auto logFile = TestTempDir / "suppressed_banners.log";

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Warn;
    config.BannerMessage_Init = "should_not_appear_banner";
    config.OpenRetryCount = 1;
    // Init_Log* toggles are left at their defaults (all true) so this test exercises
    // every internal Info-level banner writer, not just a subset.

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogError("this_error_should_appear");
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(contents.find("should_not_appear_banner") == std::string::npos, __func__, __LINE__, "BannerMessage_Init (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckTrue(contents.find("Time:") == std::string::npos, __func__, __LINE__, "Init time info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckTrue(contents.find("OS:") == std::string::npos, __func__, __LINE__, "Init OS info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckTrue(contents.find("Drive:") == std::string::npos, __func__, __LINE__, "Init drive info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckTrue(contents.find("System memory:") == std::string::npos, __func__, __LINE__, "Init system memory info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckTrue(contents.find("App:") == std::string::npos, __func__, __LINE__, "Init application info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckTrue(contents.find("App Memory:") == std::string::npos, __func__, __LINE__, "Init app memory info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckTrue(contents.find("this_error_should_appear") != std::string::npos, __func__, __LINE__, "Messages at or above InitialMinimumLevel should still be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_LogFormatMethods_FormatsMessage()
{
    // Arrange
    const auto logFile = TestTempDir / "format_message.log";

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.LogUTCDateTime = false;
    config.LogLevelStr = false;
    config.LogProcessId = false;
    config.LogThreadId = false;
    config.LogMethodName = false;
    config.LogSourceLine = false;
    config.OpenRetryCount = 1;

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfoFmt("value={}, suffix={}", 42, "done");
    logger.LogTraceFmt("trace {}", "ok");
    logger.LogForceFmt(ASWLog::Level::Warn, "forced {}", "value");
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(contents.find("value=42, suffix=done") != std::string::npos, __func__, __LINE__, "LogInfoFmt should format the message");
    CheckTrue(contents.find("trace ok") != std::string::npos, __func__, __LINE__, "LogTraceFmt should format the message");
    CheckTrue(contents.find("forced value") != std::string::npos, __func__, __LINE__, "LogForceFmt should bypass filter and format the message");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_LogFormatMethods_WriteCallerSourceLine()
{
    // Arrange
    const auto logFile = TestTempDir / "fmt_location.log";

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = logFile;
    config.LogSourceLine = true;
    config.OpenRetryCount = 1;

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    const int callLine = __LINE__ + 1;
    logger.LogInfoFmt("located {}", 1);
    logger.Close();

    // Assert
    const auto contents = ReadFileText(logFile);
    const auto expected = "[Test_ASWLog_FileLog.cpp:" + std::to_string(callLine) + "]: located 1";
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(contents.find(expected) != std::string::npos, __func__, __LINE__, "LogSourceLine should show the file and line of the LogInfoFmt call: " + expected);
    CheckTrue(contents.find("ASWLog_Interface.h") == std::string::npos, __func__, __LINE__, "LogSourceLine should not show the header that implements the *Fmt methods");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_LogLineMetadata_Options()
{
    // Arrange
    const auto logFile = TestTempDir / "metadata_line.log";

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.LogUTCDateTime = true;
    config.LogLevelStr = true;
    config.LogProcessId = true;
    config.LogThreadId = true;
    config.LogAppMem_WorkingSet = true;
    config.LogAppMem_PeakWorkingSet = true;
    config.LogMethodName = true;
    config.LogSourceLine = true;
    config.OpenRetryCount = 1;

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("metadata_message");
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(contents.find("Z") != std::string::npos, __func__, __LINE__, "LogUTCDateTime should add a UTC timestamp");
    CheckTrue(contents.find("INFO") != std::string::npos, __func__, __LINE__, "LogLevelStr should include the log level");
    CheckTrue(contents.find("[P:") != std::string::npos, __func__, __LINE__, "LogProcessId should include the process id");
    CheckTrue(contents.find("[T:") != std::string::npos, __func__, __LINE__, "LogThreadId should include the thread id");
    CheckTrue(contents.find("[WS:") != std::string::npos, __func__, __LINE__, "LogAppMem_WorkingSet should include working set memory");
    CheckTrue(contents.find("[PWS:") != std::string::npos, __func__, __LINE__, "LogAppMem_PeakWorkingSet should include peak working set memory");
    CheckTrue(contents.find("Test_LogLineMetadata_Options") != std::string::npos, __func__, __LINE__, "LogMethodName should include the calling method name");
    CheckTrue(contents.find("metadata_message") != std::string::npos, __func__, __LINE__, "Metadata log line should still contain the message");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_LogNewLineAndForceOptions()
{
    // Arrange
    const auto logFile = TestTempDir / "newline_force.log";

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Error;
    config.LogUTCDateTime = false;
    config.LogLevelStr = false;
    config.LogProcessId = false;
    config.LogThreadId = false;
    config.LogMethodName = false;
    config.LogSourceLine = false;
    config.OpenRetryCount = 1;

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogWarn("filtered_message");
    logger.LogForce(ASWLog::Level::Warn, "forced_message");
    logger.LogInfo("ignored_message");
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(contents.find("filtered_message") == std::string::npos, __func__, __LINE__, "Log should respect the minimum level unless forced");
    CheckTrue(contents.find("forced_message") != std::string::npos, __func__, __LINE__, "LogForce should bypass the minimum level");
    CheckTrue(contents.find("forced_message\n") != std::string::npos, __func__, __LINE__, "LogForce should append a newline by default");
    CheckTrue(contents.find("ignored_message") == std::string::npos, __func__, __LINE__, "Log should not write a message below the configured minimum level");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_LogRawOptions()
{
    // Arrange
    const auto logFile = TestTempDir / "raw.log";

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.LogUTCDateTime = false;
    config.LogLevelStr = false;
    config.LogProcessId = false;
    config.LogThreadId = false;
    config.LogMethodName = false;
    config.LogSourceLine = false;
    config.OpenRetryCount = 1;

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogRaw(ASWLog::Level::Info, "raw_message");
    logger.LogForceRaw(ASWLog::Level::Warn, "raw_force_message");
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(contents.find("raw_message") != std::string::npos, __func__, __LINE__, "LogRaw should write the raw message");
    CheckTrue(contents.find("raw_force_message") != std::string::npos, __func__, __LINE__, "LogForceRaw should write the raw message even when filtered");
    CheckTrue(contents.find("raw_message\n") == std::string::npos, __func__, __LINE__, "LogRaw should not append a newline by default");
    CheckTrue(contents.find("raw_force_message\n") == std::string::npos, __func__, __LINE__, "LogForceRaw should not append a trailing newline");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_MultiThreadedStress_WritesAllMessagesToDisk()
{
    // Arrange
    const auto logFile = TestTempDir / "stress.log";
    constexpr int threadCount = 4;
    constexpr int messagesPerThread = 100;

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.LogUTCDateTime = false;
    config.LogLevelStr = false;
    config.LogProcessId = false;
    config.LogThreadId = false;
    config.LogMethodName = false;
    config.LogSourceLine = false;
    config.OpenRetryCount = 1;

    std::vector<std::string> expectedMessages;
    expectedMessages.reserve(threadCount * messagesPerThread);
    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        for (int messageIndex = 0; messageIndex < messagesPerThread; ++messageIndex)
        {
            expectedMessages.push_back("stress_t" + std::to_string(threadIndex) + "_m" + std::to_string(messageIndex));
        }
    }

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        threads.emplace_back([&logger, threadIndex]()
            {
                for (int messageIndex = 0; messageIndex < messagesPerThread; ++messageIndex)
                {
                    logger.LogInfo("stress_t" + std::to_string(threadIndex) + "_m" + std::to_string(messageIndex));
                }
            });
    }

    for (auto& thread : threads)
        thread.join();

    logger.Flush();
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(!contents.empty(), __func__, __LINE__, "Stress log file should contain at least one entry");
    for (const auto& message : expectedMessages)
    {
        CheckTrue(contents.find(message) != std::string::npos,
            __func__, __LINE__,
            "Multi-threaded stress log should persist every expected message to disk: " + message);
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_MultiThreadedStress_WritesAllMessagesToDisk_OpenClose()
{
    // Arrange
    const auto logFile = TestTempDir / "stress.log";
    constexpr int threadCount = 4;
    constexpr int messagesPerThread = 200;

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.LogUTCDateTime = false;
    config.LogLevelStr = false;
    config.LogProcessId = false;
    config.LogThreadId = false;
    config.LogMethodName = false;
    config.LogSourceLine = false;
    config.OpenRetryCount = 1;
    config.AutoOpenClosePerWrite = true;

    std::vector<std::string> expectedMessages;
    expectedMessages.reserve(threadCount * messagesPerThread);
    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        for (int messageIndex = 0; messageIndex < messagesPerThread; ++messageIndex)
        {
            expectedMessages.push_back("stress_t" + std::to_string(threadIndex) + "_m" + std::to_string(messageIndex));
        }
    }

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        Log("    starting stress test thread: " + std::to_string(threadIndex));
        threads.emplace_back([&logger, threadIndex]()
            {
                for (int messageIndex = 0; messageIndex < messagesPerThread; ++messageIndex)
                {
                    logger.LogInfo("stress_t" + std::to_string(threadIndex) + "_m" + std::to_string(messageIndex));
                }
            });
    }

    for (auto& thread : threads)
        thread.join();

    logger.Flush();
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(!contents.empty(), __func__, __LINE__, "Stress log file should contain at least one entry");
    for (const auto& message : expectedMessages)
    {
        CheckTrue(contents.find(message) != std::string::npos,
            __func__, __LINE__,
            "Multi-threaded stress log should persist every expected message to disk: " + message);
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnLogEntry_FiresForQualifyingLevelsOnly()
{
    // Arrange
    const auto logFile = TestTempDir / "callback.log";

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.LogUTCDateTime = false;
    config.LogLevelStr = false;
    config.LogProcessId = false;
    config.LogThreadId = false;
    config.LogMethodName = false;
    config.LogSourceLine = false;
    config.OpenRetryCount = 1;
    config.CallbackMinimumLevel = ASWLog::Level::Error;

    std::vector<ASWLog::Level> callbackLevels;
    std::vector<std::string> callbackMessages;
    config.OnLogEntry = [&callbackLevels, &callbackMessages](ASWLog::Level level, std::string_view line)
        {
            callbackLevels.push_back(level);
            callbackMessages.emplace_back(line);
        };

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("below_threshold");
    logger.LogError("at_threshold");
    logger.LogCritical("above_threshold");
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(contents.find("below_threshold") != std::string::npos, __func__, __LINE__, "Entries below CallbackMinimumLevel should still be written to the file");
    CheckEquals(static_cast<size_t>(2), callbackMessages.size(), __func__, __LINE__, "OnLogEntry should only fire for entries at or above CallbackMinimumLevel");
    if (callbackMessages.size() == 2)
    {
        CheckEquals(static_cast<int32_t>(ASWLog::Level::Error), static_cast<int32_t>(callbackLevels[0]), __func__, __LINE__, "First callback should report the Error entry's level");
        CheckTrue(callbackMessages[0].find("at_threshold") != std::string::npos, __func__, __LINE__, "Callback should receive the same formatted line written to disk");
        CheckEquals(static_cast<int32_t>(ASWLog::Level::Critical), static_cast<int32_t>(callbackLevels[1]), __func__, __LINE__, "Second callback should report the Critical entry's level");
        CheckTrue(callbackMessages[1].find("above_threshold") != std::string::npos, __func__, __LINE__, "Callback should receive the same formatted line written to disk");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnLogEntry_ReentrantCallbackDoesNotDeadlock()
{
    // Arrange
    const auto logFile = TestTempDir / "reentrant.log";

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.LogUTCDateTime = false;
    config.LogLevelStr = false;
    config.LogProcessId = false;
    config.LogThreadId = false;
    config.LogMethodName = false;
    config.LogSourceLine = false;
    config.OpenRetryCount = 1;
    config.CallbackMinimumLevel = ASWLog::Level::Error;

    ASWLog::TASWFileLog logger;
    bool reentered = false;
    config.OnLogEntry = [&logger, &reentered](ASWLog::Level, std::string_view)
        {
            // A callback that logs again must not deadlock: DispatchLogCallback is
            // invoked only after the sink's internal mutex has been released.
            if (!reentered)
            {
                reentered = true;
                logger.LogInfo("reentrant_message");
            }
        };

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogError("trigger_message");
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(reentered, __func__, __LINE__, "Callback should have fired and re-entered the logger");
    CheckTrue(contents.find("reentrant_message") != std::string::npos, __func__, __LINE__, "Re-entrant Log call from the callback should complete and be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_RetentionMaxAge_DefaultDisabledPreservesOldBackups()
{
    // Arrange
    const auto logFile = TestTempDir / "retention_disabled.log";

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.LogUTCDateTime = false;
    config.LogLevelStr = false;
    config.LogProcessId = false;
    config.LogThreadId = false;
    config.LogMethodName = false;
    config.LogSourceLine = false;
    config.OpenRetryCount = 1;
    // config.RetentionMaxAge left at its default (0 = disabled)

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("seed_message");

    // Simulate a stale backup left over from an earlier rotation
    const auto staleBackup = TestTempDir / "retention_disabled.stale.2020-01-01.bak";
    {
        std::ofstream staleStream(staleBackup);
        staleStream << "stale";
    }
    std::filesystem::last_write_time(staleBackup, std::chrono::file_clock::now() - std::chrono::hours(2));

    const bool rotated = logger.RotateLogFiles("manual");
    logger.Close();

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(rotated, __func__, __LINE__, "RotateLogFiles should succeed");
    CheckTrue(std::filesystem::exists(staleBackup), __func__, __LINE__, "RetentionMaxAge left at its default (disabled) should not delete old backups after rotation");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_RetentionMaxAge_DeletesExpiredBackupsAfterRotation()
{
    // Arrange
    const auto logFile = TestTempDir / "retention.log";

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.LogUTCDateTime = false;
    config.LogLevelStr = false;
    config.LogProcessId = false;
    config.LogThreadId = false;
    config.LogMethodName = false;
    config.LogSourceLine = false;
    config.OpenRetryCount = 1;
    config.RetentionMaxAge = std::chrono::hours(1);

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("seed_message");

    // Simulate a stale backup left over from an earlier rotation
    const auto staleBackup = TestTempDir / "retention.stale.2020-01-01.bak";
    {
        std::ofstream staleStream(staleBackup);
        staleStream << "stale";
    }
    std::filesystem::last_write_time(staleBackup, std::chrono::file_clock::now() - std::chrono::hours(2));

    const bool rotated = logger.RotateLogFiles("manual");
    logger.Close();

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(rotated, __func__, __LINE__, "RotateLogFiles should succeed");
    CheckFalse(std::filesystem::exists(staleBackup), __func__, __LINE__, "Stale backup older than RetentionMaxAge should be deleted automatically after rotation");
    CheckTrue(std::filesystem::exists(logFile), __func__, __LINE__, "Log file should be recreated after rotation");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_RotateLogFiles_KeepsEveryBackup()
{
    // Arrange: a fixed clock makes both rotations want the same backup name
    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "rotate.log";
    const auto config = MakeRotationTestConfig(logFile);

    TFixedClockFileLog logger;
    logger.CurrentTime = std::chrono::sys_days{ 2026y / 1 / 15 } + 10h + 30min + 5s + 250ms;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("first_segment");
    const bool rotatedFirst = logger.RotateLogFiles("manual");
    logger.LogInfo("second_segment");
    const bool rotatedSecond = logger.RotateLogFiles("manual");
    logger.LogInfo("third_segment");
    logger.Close();

    // Assert
    const auto firstContents = ReadFileText(TestTempDir / "rotate.manual.2026-01-15_103005_250.bak");
    const auto secondContents = ReadFileText(TestTempDir / "rotate.manual.2026-01-15_103005_250_1.bak");
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(rotatedFirst, __func__, __LINE__, "The first RotateLogFiles should succeed");
    CheckTrue(rotatedSecond, __func__, __LINE__, "The second RotateLogFiles should succeed");
    CheckTrue(firstContents.find("first_segment") != std::string::npos, __func__, __LINE__, "The backup should be named <stem>.<reason>.<YYYY-MM-DD_HHMMSS_mmm>.bak and keep the first segment");
    CheckTrue(firstContents.find("second_segment") == std::string::npos, __func__, __LINE__, "A second rotation should not replace the first backup");
    CheckTrue(secondContents.find("second_segment") != std::string::npos, __func__, __LINE__, "A second rotation with a taken name should add _1 to the name");
    CheckTrue(ReadFileText(logFile).find("third_segment") != std::string::npos, __func__, __LINE__, "Entries after the last rotation should go to the reopened log file");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_SizeRotation_AutoOpenCloseCountsOtherWriters()
{
    // Arrange: in AutoOpenClosePerWrite mode the log is closed between writes, so other processes can append to it
    const auto logFile = TestTempDir / "sized_shared.log";
    auto config = MakeRotationTestConfig(logFile);
    config.AutoOpenClosePerWrite = true;
    config.EnableRotation = true;
    config.MaxFileSizeBytes = 100;

    const std::string otherWriterContents(150, 'x');
    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("own_first");
    {
        // Stands in for another process writing to the shared log between this logger's writes
        std::ofstream otherWriter(logFile, std::ios::binary | std::ios::app);
        otherWriter << otherWriterContents;
    }
    logger.LogInfo("own_second");
    logger.Close();

    // Assert
    std::size_t backupCount = 0;
    std::string backupContents;
    for (const auto& entry : std::filesystem::directory_iterator(TestTempDir))
    {
        const auto name = entry.path().filename().string();
        if (name.starts_with("sized_shared.size.") && name.ends_with(".bak"))
        {
            ++backupCount;
            backupContents = ReadFileText(entry.path());
        }
    }

    const auto currentContents = ReadFileText(logFile);
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckEquals(static_cast<size_t>(1), backupCount, __func__, __LINE__, "Another writer's bytes should count toward MaxFileSizeBytes, since the size is read again at each open");
    CheckTrue(backupContents.find("own_first") != std::string::npos, __func__, __LINE__, "The backup should hold this logger's earlier entry");
    CheckTrue(backupContents.find(otherWriterContents) != std::string::npos, __func__, __LINE__, "The backup should hold the other writer's entry");
    CheckTrue(currentContents.find("own_second") != std::string::npos, __func__, __LINE__, "The entry that triggered the rotation should go to the new log file");
    CheckTrue(currentContents.find('x') == std::string::npos, __func__, __LINE__, "The new log file should not contain the other writer's earlier entry");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_SizeRotation_CountsExistingFileSize()
{
    // Arrange: a log left over from an earlier run is already over the limit
    const auto logFile = TestTempDir / "sized_existing.log";
    const std::string earlierContents(150, 'x');
    {
        std::ofstream earlierStream(logFile, std::ios::binary);
        earlierStream << earlierContents;
    }

    auto config = MakeRotationTestConfig(logFile);
    config.EnableRotation = true;
    config.MaxFileSizeBytes = 100;

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("after_restart");
    logger.Close();

    // Assert
    std::size_t backupCount = 0;
    std::string backupContents;
    for (const auto& entry : std::filesystem::directory_iterator(TestTempDir))
    {
        const auto name = entry.path().filename().string();
        if (name.starts_with("sized_existing.size.") && name.ends_with(".bak"))
        {
            ++backupCount;
            backupContents = ReadFileText(entry.path());
        }
    }

    const auto currentContents = ReadFileText(logFile);
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckEquals(static_cast<size_t>(1), backupCount, __func__, __LINE__, "The first write should rotate a log that was already over MaxFileSizeBytes when opened");
    CheckEquals(earlierContents, backupContents, __func__, __LINE__, "The backup should hold the earlier run's contents");
    CheckTrue(currentContents.find("after_restart") != std::string::npos, __func__, __LINE__, "The new entry should go to the reopened log file");
    CheckTrue(currentContents.find('x') == std::string::npos, __func__, __LINE__, "The reopened log file should not contain the earlier run's contents");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_SizeRotation_RotatesWhenLimitReached()
{
    // Arrange: ten lines of about 18 bytes each against a 50-byte limit need several rotations
    const auto logFile = TestTempDir / "sized.log";
    auto config = MakeRotationTestConfig(logFile);
    config.EnableRotation = true;
    config.MaxFileSizeBytes = 50;

    constexpr int LineCount = 10;
    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    for (int index = 0; index < LineCount; ++index)
        logger.LogInfo("size_line_" + std::to_string(index) + "_end");
    logger.Close();

    // Assert
    std::size_t backupCount = 0;
    std::string allContents;
    for (const auto& entry : std::filesystem::directory_iterator(TestTempDir))
    {
        const auto name = entry.path().filename().string();
        if (name.starts_with("sized.size.") && name.ends_with(".bak"))
        {
            ++backupCount;
            allContents += ReadFileText(entry.path());
        }
    }

    const auto currentContents = ReadFileText(logFile);
    allContents += currentContents;

    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(backupCount >= 2, __func__, __LINE__, "Reaching MaxFileSizeBytes should rotate the open log to a .size. backup each time");
    CheckTrue(currentContents.find("size_line_0_end") == std::string::npos, __func__, __LINE__, "The first lines should have been rotated out of the current log");

    for (int index = 0; index < LineCount; ++index)
    {
        const auto marker = "size_line_" + std::to_string(index) + "_end";
        const auto first = allContents.find(marker);
        const bool foundOnce = first != std::string::npos && allContents.find(marker, first + 1) == std::string::npos;
        CheckTrue(foundOnce, __func__, __LINE__, "Each line should be in exactly one of the log and its backups: " + marker);
    }
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_FileLog)
