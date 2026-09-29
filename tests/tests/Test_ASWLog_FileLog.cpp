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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
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
    RegisterTest(&TTest_ASWLog_FileLog::Test_DeleteOldLogs_RemovesOldFiles, "DeleteOldLogs_RemovesOldFiles");
    RegisterTest(&TTest_ASWLog_FileLog::Test_InitializeAndLogInfo_WritesText, "InitializeAndLogInfo_WritesText");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Initialize_SuppressesInfoBannersBelowMinimumLevel, "Initialize_SuppressesInfoBannersBelowMinimumLevel");
    RegisterTest(&TTest_ASWLog_FileLog::Test_LogFormatMethods_FormatsMessage, "LogFormatMethods_FormatsMessage");
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

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_FileLog)
