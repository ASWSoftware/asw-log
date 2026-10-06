/* **************************************************************************
Test_ASWLog_FileLog.cpp
Author: Anthony S. West - ASW Software

See header for info.

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

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWLog_FileLog.h"
//---------------------------------------------------------------------------
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <share.h>
#include <windows.h>
#undef min
#undef max
#else
#include <fcntl.h>
#include <unistd.h>
#endif
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_FileLog.h"
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

const auto GroupBaseTempDir = std::filesystem::temp_directory_path() / "aswlog_tests";
const auto TestTempDir = GroupBaseTempDir / "test";

// How long a test waits for something another thread does (e.g. the FlushMode::Periodic thread), before it fails
constexpr std::chrono::milliseconds WaitTimeout = std::chrono::seconds(5);

// A TASWFileLog whose next FailedFlushCount flushes fail, reporting ErrorKind::FlushFailed as a real failed flush does
class TFailingFlushFileLog : public ASWLog::TASWFileLog
{
public:
    int FailedFlushCount = 0; // Set before Initialize(); then guarded by m_Mutex

protected:
    bool FlushUnlocked() override
    {
        if (FailedFlushCount <= 0)
            return TASWFileLog::FlushUnlocked();

        --FailedFlushCount;
        ASWLog::TASWLogError error;
        error.Kind = ASWLog::ErrorKind::FlushFailed;
        error.Message = "Test flush failure";
        ReportErrorUnlocked(std::move(error));
        return false;
    }

public:
    // Stops the worker thread before this class is destroyed, since the thread calls FlushUnlocked() (see
    // TASWTextLogBase::Finalize())
    ~TFailingFlushFileLog() override
    {
        Finalize();
    }
};

// A TASWFileLog that counts how often its worker thread (FlushMode::Periodic) wakes
class TWakeCountingFileLog : public ASWLog::TASWFileLog
{
public:
    std::atomic<int> WakeCount{ 0 };

protected:
    void OnWorkerWakeUnlocked() override
    {
        ++WakeCount;
        TASWFileLog::OnWorkerWakeUnlocked();
    }

public:
    // Stops the worker thread before this class is destroyed, since the thread calls OnWorkerWakeUnlocked() (see
    // TASWTextLogBase::Finalize())
    ~TWakeCountingFileLog() override
    {
        Finalize();
    }
};

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

// A TASWFileLog that can tell whether its lock is held, e.g. inside a callback
class TLockCheckFileLog : public ASWLog::TASWFileLog
{
public:
    // True if m_Mutex isn't held, checked from another thread (try_lock on a mutex the calling thread holds is
    // undefined)
    bool IsMutexFree()
    {
        bool isFree = false;
        std::thread checker([this, &isFree] {
            if (m_Mutex.try_lock())
            {
                isFree = true;
                m_Mutex.unlock();
            }
                    });
        checker.join();
        return isFree;
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

// While alive, makes 'folder' the current folder, so a test can use short relative paths
class TScopedCurrentPath
{
private:
    std::filesystem::path m_PreviousPath;

public:
    explicit TScopedCurrentPath(const std::filesystem::path& folder)
        : m_PreviousPath(std::filesystem::current_path())
    {
        std::filesystem::current_path(folder);
    }

    ~TScopedCurrentPath()
    {
        std::error_code errorCode;
        std::filesystem::current_path(m_PreviousPath, errorCode);
    }

    TScopedCurrentPath(const TScopedCurrentPath&) = delete;
    TScopedCurrentPath& operator=(const TScopedCurrentPath&) = delete;
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

// How many files in TestTempDir have names starting with 'prefix'
std::size_t CountFilesStartingWith(std::string_view prefix)
{
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(TestTempDir))
    {
        if (entry.path().filename().string().starts_with(prefix))
            ++count;
    }

    return count;
}

// Creates 'file' with 'size' bytes, last written 'age' ago, e.g. a backup left by an earlier rotation
void CreateAgedFile(const std::filesystem::path& file, std::size_t size, std::chrono::hours age)
{
    {
        std::ofstream stream(file, std::ios::binary);
        stream << std::string(size, 'x');
    }
    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() - age);
}

// Gives a logger's newly started worker thread time to start waiting for its interval, for a test that checks how a
// waiting worker reacts (e.g. to a stop or a new interval). Nothing outside the worker shows that it is waiting, so
// this is a fixed sleep: the test passes either way, but only checks the waiting case if the worker got there.
void GiveWorkerTimeToStartWaiting()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

// An OnError handler for the tests that cause failures on purpose, so their reports don't go to stderr (the OnError
// tests check the reports)
void IgnoreError(const ASWLog::TASWLogError& /*error*/)
{
}

// A config for the rotation tests, with no line metadata or startup info, so each line is just its message
ASWLog::TASWLogConfig MakeRotationTestConfig(const std::filesystem::path& logFile)
{
    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.Startup.WriteTimeInfo = false;
    config.Startup.WriteOSInfo = false;
    config.Startup.WriteDriveInfo = false;
    config.Startup.WriteSystemMemoryInfo = false;
    config.Startup.WriteApplicationInfo = false;
    config.Startup.WriteMemoryUsage = false;
    config.File.OpenRetryCount = 1;
    config.Shutdown.WriteLine = false;
    return config;
}

std::string ReadFileText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};

    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

bool WaitUntil(const std::function<bool()>& condition); // See below

// Reads 'path' until it contains 'text', for at most WaitTimeout. Returns what it read last.
std::string WaitForFileText(const std::filesystem::path& path, std::string_view text)
{
    std::string contents;
    WaitUntil([&] {
                contents = ReadFileText(path);
                return contents.find(text) != std::string::npos;
            });
    return contents;
}

// Checks 'condition' until it is true, for at most WaitTimeout. Returns false if it never was.
bool WaitUntil(const std::function<bool()>& condition)
{
    const auto deadline = std::chrono::steady_clock::now() + WaitTimeout;
    while (!condition())
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    return true;
}

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
TTest_ASWLog_FileLog::TTest_ASWLog_FileLog()
    : inherited("ASWLog_FileLog_Tests")
{
    RegisterTest(&TTest_ASWLog_FileLog::Test_Async_EntryAtWaitAtLevelIsInTheFileWhenTheCallReturns, "Async_EntryAtWaitAtLevelIsInTheFileWhenTheCallReturns");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Async_EveryWriteFlushesEachBatch, "Async_EveryWriteFlushesEachBatch");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Async_PeriodicFlushesOnTheSameThread, "Async_PeriodicFlushesOnTheSameThread");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Async_ReconfigureWritesQueuedEntriesToTheOldFile, "Async_ReconfigureWritesQueuedEntriesToTheOldFile");
    RegisterTest(&TTest_ASWLog_FileLog::Test_AutoOpenClose_StaysInitializedBetweenWrites, "AutoOpenClose_StaysInitializedBetweenWrites");
    RegisterTest(&TTest_ASWLog_FileLog::Test_BackupLimits_ApplyOnlyToThisLogsBackups, "BackupLimits_ApplyOnlyToThisLogsBackups");
    RegisterTest(&TTest_ASWLog_FileLog::Test_BackupLimits_MaxBackupFilesKeepsNewestByLastWrite, "BackupLimits_MaxBackupFilesKeepsNewestByLastWrite");
    RegisterTest(&TTest_ASWLog_FileLog::Test_BackupLimits_MaxBackupTotalBytesDeletesOldestFirst, "BackupLimits_MaxBackupTotalBytesDeletesOldestFirst");
    RegisterTest(&TTest_ASWLog_FileLog::Test_BackupLimits_MaxBackupTotalBytesIsStrict, "BackupLimits_MaxBackupTotalBytesIsStrict");
    RegisterTest(&TTest_ASWLog_FileLog::Test_BackupLimits_ZeroKeepsEveryBackup, "BackupLimits_ZeroKeepsEveryBackup");
    RegisterTest(&TTest_ASWLog_FileLog::Test_ChildProcess_DoesNotInheritLogFile, "ChildProcess_DoesNotInheritLogFile");
    RegisterTest(&TTest_ASWLog_FileLog::Test_DailyRolling_KeepsExistingBackupForSameDate, "DailyRolling_KeepsExistingBackupForSameDate");
    RegisterTest(&TTest_ASWLog_FileLog::Test_DailyRolling_KeepsLeftoverLogFromSameDay, "DailyRolling_KeepsLeftoverLogFromSameDay");
    RegisterTest(&TTest_ASWLog_FileLog::Test_DailyRolling_NamesBackupForContentDate, "DailyRolling_NamesBackupForContentDate");
    RegisterTest(&TTest_ASWLog_FileLog::Test_DailyRolling_RotatesLeftoverLogFromEarlierDay, "DailyRolling_RotatesLeftoverLogFromEarlierDay");
    RegisterTest(&TTest_ASWLog_FileLog::Test_DailyRolling_SharedLogRollsOverOnce, "DailyRolling_SharedLogRollsOverOnce");
    RegisterTest(&TTest_ASWLog_FileLog::Test_DeleteOldLogs_AcceptsShortRelativeFolder, "DeleteOldLogs_AcceptsShortRelativeFolder");
    RegisterTest(&TTest_ASWLog_FileLog::Test_DeleteOldLogs_EmptyPatternDeletesNothing, "DeleteOldLogs_EmptyPatternDeletesNothing");
    RegisterTest(&TTest_ASWLog_FileLog::Test_DeleteOldLogs_MatchesNonASCIIFileNames, "DeleteOldLogs_MatchesNonASCIIFileNames");
    RegisterTest(&TTest_ASWLog_FileLog::Test_DeleteOldLogs_RemovesOldFiles, "DeleteOldLogs_RemovesOldFiles");
    RegisterTest(&TTest_ASWLog_FileLog::Test_FailedReopen_RetriesAndResumesLogging, "FailedReopen_RetriesAndResumesLogging");
    RegisterTest(&TTest_ASWLog_FileLog::Test_FailedReopen_ZeroResetDelayRetriesOnNextWrite, "FailedReopen_ZeroResetDelayRetriesOnNextWrite");
    RegisterTest(&TTest_ASWLog_FileLog::Test_FailedSizeRotation_WaitsBeforeRetrying, "FailedSizeRotation_WaitsBeforeRetrying");
    RegisterTest(&TTest_ASWLog_FileLog::Test_FileStream_FlushWorksAfterFailedWrite, "FileStream_FlushWorksAfterFailedWrite");
    RegisterTest(&TTest_ASWLog_FileLog::Test_FlushImmediatelyAtLevel_FlushesEntriesAtOrAboveTheLevel, "FlushImmediatelyAtLevel_FlushesEntriesAtOrAboveTheLevel");
    RegisterTest(&TTest_ASWLog_FileLog::Test_FlushImmediatelyAtLevel_OffLeavesFlushingToFlushMode, "FlushImmediatelyAtLevel_OffLeavesFlushingToFlushMode");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Flush_WritesBufferedManualModeEntries, "Flush_WritesBufferedManualModeEntries");
    RegisterTest(&TTest_ASWLog_FileLog::Test_GetInstance_ReturnsSameInstance, "GetInstance_ReturnsSameInstance");
    RegisterTest(&TTest_ASWLog_FileLog::Test_InitializeAndLogInfo_WritesText, "InitializeAndLogInfo_WritesText");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Initialize_SuppressesInfoBannersBelowMinimumLevel, "Initialize_SuppressesInfoBannersBelowMinimumLevel");
    RegisterTest(&TTest_ASWLog_FileLog::Test_LogFormatMethods_FormatsMessage, "LogFormatMethods_FormatsMessage");
    RegisterTest(&TTest_ASWLog_FileLog::Test_LogFormatMethods_WriteCallerSourceLine, "LogFormatMethods_WriteCallerSourceLine");
    RegisterTest(&TTest_ASWLog_FileLog::Test_LogLineMetadata_Options, "LogLineMetadata_Options");
    RegisterTest(&TTest_ASWLog_FileLog::Test_LogNewLineAndForceOptions, "LogNewLineAndForceOptions");
    RegisterTest(&TTest_ASWLog_FileLog::Test_LogProcessAndThreadIds_AreOSIds, "LogProcessAndThreadIds_AreOSIds");
    RegisterTest(&TTest_ASWLog_FileLog::Test_LogRawOptions, "LogRawOptions");
    RegisterTest(&TTest_ASWLog_FileLog::Test_MultiThreadedStress_WritesAllMessagesToDisk, "MultiThreadedStress_WritesAllMessagesToDisk");
    RegisterTest(&TTest_ASWLog_FileLog::Test_MultiThreadedStress_WritesAllMessagesToDisk_OpenClose, "MultiThreadedStress_WritesAllMessagesToDisk_OpenClose");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnBackupCreated_IsNotCalledWhenRotationFails, "OnBackupCreated_IsNotCalledWhenRotationFails");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnBackupCreated_LeavesARenamedBackupToTheApp, "OnBackupCreated_LeavesARenamedBackupToTheApp");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnBackupCreated_ReportsDailyBackupAtInitialize, "OnBackupCreated_ReportsDailyBackupAtInitialize");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnBackupCreated_ReportsEachBackup, "OnBackupCreated_ReportsEachBackup");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnBackupCreated_RunsOutsideTheLockBeforeCleanup, "OnBackupCreated_RunsOutsideTheLockBeforeCleanup");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnBackupCreated_ThrowingCallbackStillCleansUp, "OnBackupCreated_ThrowingCallbackStillCleansUp");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnError_ReportsFailedDelete, "OnError_ReportsFailedDelete");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnError_ReportsFailedOpen, "OnError_ReportsFailedOpen");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnError_ReportsFailedRotationAndReopen, "OnError_ReportsFailedRotationAndReopen");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnError_ReportsFailedSync, "OnError_ReportsFailedSync");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnError_ReportsFullDisk, "OnError_ReportsFullDisk");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnLogEntry_FiresForQualifyingLevelsOnly, "OnLogEntry_FiresForQualifyingLevelsOnly");
    RegisterTest(&TTest_ASWLog_FileLog::Test_OnLogEntry_ReentrantCallbackDoesNotDeadlock, "OnLogEntry_ReentrantCallbackDoesNotDeadlock");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Periodic_CloseStopsAndOpenRestartsTheThread, "Periodic_CloseStopsAndOpenRestartsTheThread");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Periodic_CloseStopsTheThreadWithoutWaitingForTheInterval, "Periodic_CloseStopsTheThreadWithoutWaitingForTheInterval");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Periodic_ConcurrentLoggingAndReconfigure, "Periodic_ConcurrentLoggingAndReconfigure");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Periodic_DestructorStopsTheThreadWithoutWaitingForTheInterval, "Periodic_DestructorStopsTheThreadWithoutWaitingForTheInterval");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Periodic_ErrorHandlerCanCloseAndReopenOnTheThread, "Periodic_ErrorHandlerCanCloseAndReopenOnTheThread");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Periodic_FlushesAfterTheLastEntry, "Periodic_FlushesAfterTheLastEntry");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Periodic_ReconfigureAppliesANewInterval, "Periodic_ReconfigureAppliesANewInterval");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Periodic_ReconfigureStartsAndStopsTheThread, "Periodic_ReconfigureStartsAndStopsTheThread");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Periodic_ZeroIntervalFlushesEveryEntry, "Periodic_ZeroIntervalFlushesEveryEntry");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Reconfigure_FlushesEntriesBufferedByPreviousMode, "Reconfigure_FlushesEntriesBufferedByPreviousMode");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Reconfigure_MovesOutputToNewFile, "Reconfigure_MovesOutputToNewFile");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Reconfigure_UnopenableFileFailsButLoggerStaysInitialized, "Reconfigure_UnopenableFileFailsButLoggerStaysInitialized");
    RegisterTest(&TTest_ASWLog_FileLog::Test_RetentionMaxAge_DefaultDisabledPreservesOldBackups, "RetentionMaxAge_DefaultDisabledPreservesOldBackups");
    RegisterTest(&TTest_ASWLog_FileLog::Test_RetentionMaxAge_DeletesExpiredBackupsAfterRotation, "RetentionMaxAge_DeletesExpiredBackupsAfterRotation");
    RegisterTest(&TTest_ASWLog_FileLog::Test_RotateLogFiles_KeepsEveryBackup, "RotateLogFiles_KeepsEveryBackup");
    RegisterTest(&TTest_ASWLog_FileLog::Test_SetEnabled_FalseStopsAutoOpenCloseLogging, "SetEnabled_FalseStopsAutoOpenCloseLogging");
    RegisterTest(&TTest_ASWLog_FileLog::Test_SizeRotation_AutoOpenCloseCountsOtherWriters, "SizeRotation_AutoOpenCloseCountsOtherWriters");
    RegisterTest(&TTest_ASWLog_FileLog::Test_SizeRotation_CountsExistingFileSize, "SizeRotation_CountsExistingFileSize");
    RegisterTest(&TTest_ASWLog_FileLog::Test_SizeRotation_RotatesWhenLimitReached, "SizeRotation_RotatesWhenLimitReached");
    RegisterTest(&TTest_ASWLog_FileLog::Test_SyncToDiskAtLevel_FlushesAndSyncsEntriesAtOrAboveTheLevel, "SyncToDiskAtLevel_FlushesAndSyncsEntriesAtOrAboveTheLevel");
    RegisterTest(&TTest_ASWLog_FileLog::Test_Write_EarlierRecordDoesNotRollLogBack, "Write_EarlierRecordDoesNotRollLogBack");
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
void TTest_ASWLog_FileLog::Test_Async_EntryAtWaitAtLevelIsInTheFileWhenTheCallReturns()
{
    // Arrange: neither the flush mode nor the flush level would flush these entries
    const auto logFile = TestTempDir / "async_wait_level.log";
    auto config = MakeRotationTestConfig(logFile);
    config.Async.Enabled = true;
    config.File.Flush = ASWLog::FlushMode::Manual;
    config.File.FlushImmediatelyAtLevel = ASWLog::Level::Off;

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act
    logger.LogInfo("info_entry");
    logger.LogError("error_entry");
    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(std::string(": info_entry\n: error_entry\n"), contents,
        "When the Error call returns, its entry and those before it should be in the file");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Async_EveryWriteFlushesEachBatch()
{
    // Arrange
    const auto logFile = TestTempDir / "async_every_write.log";
    auto config = MakeRotationTestConfig(logFile);
    config.Async.Enabled = true;
    config.File.Flush = ASWLog::FlushMode::EveryWrite;
    config.File.FlushImmediatelyAtLevel = ASWLog::Level::Off;

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act: no Flush() call and no further entries
    logger.LogInfo("entry_1");
    logger.LogInfo("entry_2");
    const auto contents = WaitForFileText(logFile, "entry_2");

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(std::string(": entry_1\n: entry_2\n"), contents, "The logger's thread should flush what it wrote");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Async_PeriodicFlushesOnTheSameThread()
{
    // Arrange
    const auto logFile = TestTempDir / "async_periodic.log";
    auto config = MakeRotationTestConfig(logFile);
    config.Async.Enabled = true;
    config.File.Flush = ASWLog::FlushMode::Periodic;
    config.File.FlushInterval = std::chrono::milliseconds(10);
    config.File.FlushImmediatelyAtLevel = ASWLog::Level::Off;

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act
    logger.LogInfo("entry");
    const auto contents = WaitForFileText(logFile, "entry");

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(std::string(": entry\n"), contents, "The logger's thread should also flush every FlushInterval");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Async_ReconfigureWritesQueuedEntriesToTheOldFile()
{
    // Arrange
    const auto oldFile = TestTempDir / "async_old.log";
    const auto newFile = TestTempDir / "async_new.log";
    auto config = MakeRotationTestConfig(oldFile);
    config.Async.Enabled = true;
    constexpr int EntryCount = 2000;

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act: queue entries faster than the logger's thread writes them, then move to another file
    for (int entry = 0; entry < EntryCount; ++entry)
        logger.LogInfo(std::format("old_{}", entry));

    config.File.FilePath = newFile;
    const bool reconfigured = logger.Reconfigure(config);
    logger.LogInfo("new_entry");
    logger.Close();
    const auto oldContents = ReadFileText(oldFile);
    const auto newContents = ReadFileText(newFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(reconfigured, "Reconfigure should succeed");
    CheckEquals(EntryCount, std::count(oldContents.begin(), oldContents.end(), '\n'),
        "Every entry queued before Reconfigure should go to the old file");
    CheckEquals(std::string(": new_entry\n"), newContents, "Only the entry logged after Reconfigure should go to the new file");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_AutoOpenClose_StaysInitializedBetweenWrites()
{
    // Arrange: with File.AutoOpenClosePerWrite the file is closed after every entry, which must not uninitialize the
    // logger
    const auto logFile = TestTempDir / "auto_open_close.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.AutoOpenClosePerWrite = true;
    config.Startup.Banner = "startup_banner";
    config.Shutdown.WriteLine = true;

    // Act
    bool initialized = false;
    bool initializedAgain = true;
    {
        ASWLog::TASWFileLog logger;
        initialized = logger.Initialize(config);
        logger.LogInfo("entry");
        initializedAgain = logger.Initialize(config);
    }

    const auto contents = ReadFileText(logFile);
    const auto bannerPos = contents.find("startup_banner");
    const auto entryPos = contents.find(": entry\n");
    const auto shutdownPos = contents.find(": Logger shutdown: ");

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckFalse(initializedAgain,
        "A second Initialize() should fail while the logger is initialized, although its file is closed between writes");
    CheckTrue(bannerPos != std::string::npos && contents.find("startup_banner", bannerPos + 1) == std::string::npos,
        "The startup banner should be written exactly once");
    CheckTrue(entryPos != std::string::npos, "The entry should be written");
    CheckTrue(shutdownPos != std::string::npos && shutdownPos > entryPos,
        "The destructor should write the shutdown line after the last entry: " + contents);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_BackupLimits_ApplyOnlyToThisLogsBackups()
{
    // Arrange: every file is old enough for File.RetentionMaxAge, and File.MaxBackupFiles keeps only the new backup
    using namespace std::chrono_literals;
    const std::vector<std::string> ownBackups{
        "app.size.2026-01-01_000000_000.bak",
        "app.daily.2026-01-02.bak", // Also the form of every backup before 0.43
        "app.manual.2026-01-03_000000_000_1.bak",
        "app.daily.2026-01-04_2.bak",
    };
    const std::vector<std::string> otherFiles{
        "app.audit.size.2026-01-01_000000_000.bak", // A backup of app.audit.log
        "application.size.2026-01-01_000000_000.bak",
        "app.notes.bak",
        "app.size.not-a-date.bak",
        "app.pre.upgrade.2026-01-01.bak", // A reason with a '.' isn't recognized
        "app.size.2026-01-01_000000_000.bak.old",
    };
    for (const auto& name : ownBackups)
        CreateAgedFile(TestTempDir / name, 10, 48h);
    for (const auto& name : otherFiles)
        CreateAgedFile(TestTempDir / name, 10, 48h);

    auto config = MakeRotationTestConfig(TestTempDir / "app.log");
    config.File.RetentionMaxAge = 24h;
    config.File.MaxBackupFiles = 1;
    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("current");

    // Act
    const bool rotated = logger.RotateLogFiles("manual");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(rotated, "RotateLogFiles should succeed");
    for (const auto& name : ownBackups)
        CheckFalse(std::filesystem::exists(TestTempDir / name), "This log's old backup should be deleted: " + name);
    for (const auto& name : otherFiles)
        CheckTrue(std::filesystem::exists(TestTempDir / name), "A file that isn't this log's backup should be kept: " + name);
    CheckEquals(1, CountFilesStartingWith("app.manual."), "The new backup should be kept");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_BackupLimits_MaxBackupFilesKeepsNewestByLastWrite()
{
    // Arrange: last write times in a different order than the names. The daily backup holds the end of its day, so it
    // is newer than that day's size backups although its name sorts before theirs.
    using namespace std::chrono_literals;
    const auto daily = TestTempDir / "count.daily.2026-01-01.bak"; // 1 hour old
    const auto sizeTwo = TestTempDir / "count.size.2026-01-02_000000_000.bak"; // 2 hours
    const auto sizeThree = TestTempDir / "count.size.2026-01-03_000000_000_1.bak"; // 3 hours
    const auto sizeLatestName = TestTempDir / "count.size.2026-01-05_000000_000.bak"; // 4 hours
    CreateAgedFile(daily, 10, 1h);
    CreateAgedFile(sizeTwo, 10, 2h);
    CreateAgedFile(sizeThree, 10, 3h);
    CreateAgedFile(sizeLatestName, 10, 4h);

    auto config = MakeRotationTestConfig(TestTempDir / "count.log");
    config.File.MaxBackupFiles = 3;
    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("current");

    // Act: the new backup is the newest, so the 2 next newest are kept with it
    const bool rotated = logger.RotateLogFiles("manual");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(rotated, "RotateLogFiles should succeed");
    CheckEquals(1, CountFilesStartingWith("count.manual."), "The new backup should be kept");
    CheckTrue(std::filesystem::exists(daily), "The backup last written 1 hour ago should be kept");
    CheckTrue(std::filesystem::exists(sizeTwo), "The backup last written 2 hours ago should be kept");
    CheckFalse(std::filesystem::exists(sizeThree), "The backup last written 3 hours ago is beyond the limit");
    CheckFalse(std::filesystem::exists(sizeLatestName), "The oldest backup should be deleted, whatever its name says");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_BackupLimits_MaxBackupTotalBytesDeletesOldestFirst()
{
    // Arrange
    using namespace std::chrono_literals;
    const auto first = TestTempDir / "bytes.size.2026-01-04_000000_000.bak";
    const auto second = TestTempDir / "bytes.size.2026-01-03_000000_000.bak";
    const auto third = TestTempDir / "bytes.size.2026-01-02_000000_000.bak";
    const auto smallOldest = TestTempDir / "bytes.size.2026-01-01_000000_000.bak";
    CreateAgedFile(first, 100, 1h);
    CreateAgedFile(second, 100, 2h);
    CreateAgedFile(third, 100, 3h);
    CreateAgedFile(smallOldest, 10, 4h);

    // The new backup holds ": current\n" (10 bytes): it and the 2 newest fit (210 bytes), the third doesn't (310)
    auto config = MakeRotationTestConfig(TestTempDir / "bytes.log");
    config.File.MaxBackupTotalBytes = 260;
    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("current");

    // Act
    const bool rotated = logger.RotateLogFiles("manual");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(rotated, "RotateLogFiles should succeed");
    CheckEquals(1, CountFilesStartingWith("bytes.manual."), "The new backup should be kept");
    CheckTrue(std::filesystem::exists(first), "The newest old backup should be kept");
    CheckTrue(std::filesystem::exists(second), "The second newest old backup should be kept");
    CheckFalse(std::filesystem::exists(third), "The backup that doesn't fit should be deleted");
    CheckFalse(std::filesystem::exists(smallOldest), "An older backup should be deleted too, even though it would fit");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_BackupLimits_MaxBackupTotalBytesIsStrict()
{
    // Arrange: a limit smaller than the backup that rotation makes
    using namespace std::chrono_literals;
    const auto older = TestTempDir / "strict.size.2026-01-01_000000_000.bak";
    CreateAgedFile(older, 10, 1h);

    auto config = MakeRotationTestConfig(TestTempDir / "strict.log");
    config.File.MaxBackupTotalBytes = 1;
    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("current");

    // Act
    const bool rotated = logger.RotateLogFiles("manual");
    logger.LogInfo("after");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(rotated, "RotateLogFiles should succeed");
    CheckEquals(0, CountFilesStartingWith("strict.manual."), "Even the new backup should be deleted when it alone exceeds the limit");
    CheckFalse(std::filesystem::exists(older), "The older backup should be deleted");
    CheckEquals(std::string(": after\n"), ReadFileText(TestTempDir / "strict.log"), "Logging should go on in the new log file");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_BackupLimits_ZeroKeepsEveryBackup()
{
    // Arrange: File.RetentionMaxAge is set, so the cleanup runs, but none of the backups has expired
    using namespace std::chrono_literals;
    for (int hours = 1; hours <= 3; ++hours)
        CreateAgedFile(TestTempDir / std::format("zero.size.2026-01-0{}_000000_000.bak", hours), 1000, std::chrono::hours(hours));

    auto config = MakeRotationTestConfig(TestTempDir / "zero.log");
    config.File.RetentionMaxAge = 24h; // File.MaxBackupFiles and File.MaxBackupTotalBytes stay 0 (unlimited)
    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("current");

    // Act
    const bool rotated = logger.RotateLogFiles("manual");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(rotated, "RotateLogFiles should succeed");
    CheckEquals(3, CountFilesStartingWith("zero.size."), "Limits of 0 should keep every backup");
    CheckEquals(1, CountFilesStartingWith("zero.manual."), "The new backup should be kept");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_ChildProcess_DoesNotInheritLogFile()
{
    // Arrange
    const auto logFile = TestTempDir / "not_inherited.log";
    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(MakeRotationTestConfig(logFile));
    logger.LogInfo("before_child");

#if defined(_WIN32)
    // A child process that inherits every inheritable handle, created suspended so it never runs. If it had the log
    // file's handle, renaming the file would fail while the child exists.
    std::wstring exePath(MAX_PATH, L'\0');
    exePath.resize(GetModuleFileNameW(nullptr, exePath.data(), static_cast<DWORD>(exePath.size())));
    std::wstring commandLine = L"\"" + exePath + L"\"";
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    const bool childCreated = CreateProcessW(exePath.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED, nullptr, nullptr, &startupInfo, &processInfo) != FALSE;

    // Act
    const bool rotated = logger.RotateLogFiles("manual");

    if (childCreated)
    {
        TerminateProcess(processInfo.hProcess, 0);
        WaitForSingleObject(processInfo.hProcess, 5000);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
    }

    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(childCreated, "The child process should be created");
    CheckTrue(rotated, "Rotation should succeed while a child process that inherited handles exists");
#else
    // Act: find the log file's descriptor among this process's open files
    int logFileDescriptor = -1;
    std::error_code errorCode;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd", errorCode))
    {
        std::error_code entryError;
        const auto target = std::filesystem::read_symlink(entry.path(), entryError);
        if (!entryError && std::filesystem::equivalent(target, logFile, entryError) && !entryError)
            logFileDescriptor = std::stoi(entry.path().filename().string());
    }

    const int descriptorFlags = logFileDescriptor >= 0 ? fcntl(logFileDescriptor, F_GETFD) : -1;
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckGreaterThanOrEqual(logFileDescriptor, 0, "The log file should be open");
    CheckTrue(descriptorFlags >= 0 && (descriptorFlags & FD_CLOEXEC) != 0, "The log file should be closed when a child process starts another program");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_DailyRolling_KeepsExistingBackupForSameDate()
{
    // Arrange
    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "rolling_existing.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.EnableDailyRolling = true;

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
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(std::string("earlier_backup"), ReadFileText(existingBackup), "Daily rolling should not replace an existing backup for the same day");
    CheckContains(newBackupContents, "day_one_entry", "Daily rolling should add _1 to the name when the day's backup already exists");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_DailyRolling_KeepsLeftoverLogFromSameDay()
{
    // Arrange: a log last written 2 hours before the logger starts at 10:00 UTC, i.e. earlier the same UTC day
    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "same_day.log";
    {
        std::ofstream leftoverStream(logFile);
        leftoverStream << "earlier_today_entry\n";
    }

    std::filesystem::last_write_time(logFile, std::chrono::file_clock::now() - 2h);

    auto config = MakeRotationTestConfig(logFile);
    config.File.EnableDailyRolling = true;

    TFixedClockFileLog logger;
    logger.CurrentTime = std::chrono::sys_days{ 2026y / 1 / 15 } + 10h;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("new_entry");
    logger.Close();

    // Assert
    const auto contents = ReadFileText(logFile);
    CheckTrue(initialized, "Initialize should succeed");
    CheckFalse(std::filesystem::exists(TestTempDir / "same_day.daily.2026-01-15.bak"), "A log from the same day should not be rotated");
    CheckContains(contents, "earlier_today_entry", "Today's earlier entries should stay in the log");
    CheckContains(contents, "new_entry", "New entries should be appended to the log");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_DailyRolling_NamesBackupForContentDate()
{
    // Arrange
    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "rolling.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.EnableDailyRolling = true;

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
    CheckTrue(initialized, "Initialize should succeed");
    CheckContains(backupContents, "day_one_entry", "The daily backup should be named for the day its entries are from");
    CheckNotContains(backupContents, "day_two_entry", "The daily backup should not contain entries from the new day");
    CheckFalse(std::filesystem::exists(TestTempDir / "rolling.daily.2026-01-16.bak"), "The daily backup should not be named for the day that just started");
    CheckContains(currentContents, "day_two_entry", "The new day's entries should go to the reopened log file");
    CheckNotContains(currentContents, "day_one_entry", "The reopened log file should not contain the previous day's entries");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_DailyRolling_RotatesLeftoverLogFromEarlierDay()
{
    // Arrange: a log last written 30 hours before the logger starts at 10:00 UTC on 2026-01-15, i.e. on 2026-01-14
    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "leftover.log";
    {
        std::ofstream leftoverStream(logFile);
        leftoverStream << "yesterday_entry\n";
    }

    std::filesystem::last_write_time(logFile, std::chrono::file_clock::now() - 30h);

    auto config = MakeRotationTestConfig(logFile);
    config.File.EnableDailyRolling = true;

    TFixedClockFileLog logger;
    logger.CurrentTime = std::chrono::sys_days{ 2026y / 1 / 15 } + 10h;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("today_entry");
    logger.Close();

    // Assert
    const auto backupContents = ReadFileText(TestTempDir / "leftover.daily.2026-01-14.bak");
    const auto currentContents = ReadFileText(logFile);
    CheckTrue(initialized, "Initialize should succeed");
    CheckContains(backupContents, "yesterday_entry", "The leftover log should be rotated to a backup named for the day it was written");
    CheckNotContains(backupContents, "today_entry", "The backup should not contain today's entries");
    CheckContains(currentContents, "today_entry", "Today's entries should go to a new log file");
    CheckNotContains(currentContents, "yesterday_entry", "The new log file should not contain the earlier day's entries");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_DailyRolling_SharedLogRollsOverOnce()
{
    // Arrange: two loggers sharing one log, as two processes would (File.AutoOpenClosePerWrite), both logging at 23:59 UTC
    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "shared.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.EnableDailyRolling = true;
    config.File.AutoOpenClosePerWrite = true;

    const auto dayOne = std::chrono::sys_days{ 2026y / 1 / 15 };
    TFixedClockFileLog firstLogger;
    TFixedClockFileLog secondLogger;
    firstLogger.CurrentTime = dayOne + 23h + 59min;
    secondLogger.CurrentTime = dayOne + 23h + 59min;

    const bool firstInitialized = firstLogger.Initialize(config);
    const bool secondInitialized = secondLogger.Initialize(config);
    firstLogger.LogInfo("first_day_one_entry");
    secondLogger.LogInfo("second_day_one_entry");

    // The file was written at 23:59; make its age match the clocks' jump past midnight below
    std::filesystem::last_write_time(logFile, std::chrono::file_clock::now() - 2min);
    firstLogger.CurrentTime = dayOne + 24h + 30s;
    secondLogger.CurrentTime = dayOne + 24h + 40s;

    // Act: the first logger rolls the log over; the second then finds a log from today
    firstLogger.LogInfo("first_day_two_entry");
    secondLogger.LogInfo("second_day_two_entry");
    firstLogger.Close();
    secondLogger.Close();

    // Assert
    const auto backupContents = ReadFileText(TestTempDir / "shared.daily.2026-01-15.bak");
    const auto currentContents = ReadFileText(logFile);
    CheckTrue(firstInitialized && secondInitialized, "Initialize should succeed");
    CheckContains(backupContents, "first_day_one_entry", "The daily backup should hold both loggers' entries from the first day");
    CheckContains(backupContents, "second_day_one_entry", "The daily backup should hold both loggers' entries from the first day");
    CheckFalse(std::filesystem::exists(TestTempDir / "shared.daily.2026-01-15_1.bak"), "The log should be rolled over only once");
    CheckContains(currentContents, "first_day_two_entry", "Both loggers' entries from the second day should be in the current log");
    CheckContains(currentContents, "second_day_two_entry", "Both loggers' entries from the second day should be in the current log");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_DeleteOldLogs_AcceptsShortRelativeFolder()
{
    // Arrange: an old file in "lg", a relative folder name as short as a root folder such as "C:\"
    const auto shortFolder = TestTempDir / "lg";
    std::filesystem::create_directories(shortFolder);
    const auto oldFile = shortFolder / "old_example.log";
    {
        std::ofstream oldStream(oldFile);
        oldStream << "old";
    }
    std::filesystem::last_write_time(oldFile, std::chrono::file_clock::now() - std::chrono::hours(2));

    // Act
    std::size_t deletedCount = 0;
    {
        const TScopedCurrentPath currentPath(TestTempDir);
        deletedCount = ASWLog::TASWFileLog::DeleteOldLogs("lg", "*.log", std::chrono::hours(1));
    }

    // Assert
    CheckEquals(1, deletedCount, "DeleteOldLogs should accept a short relative folder name");
    CheckFalse(std::filesystem::exists(oldFile), "The old file in the relative folder should be removed");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_DeleteOldLogs_EmptyPatternDeletesNothing()
{
    // Arrange
    const auto oldFile = TestTempDir / "old_example.log";
    {
        std::ofstream oldStream(oldFile);
        oldStream << "old";
    }
    std::filesystem::last_write_time(oldFile, std::chrono::file_clock::now() - std::chrono::hours(2));

    // Act
    const auto emptyPatternCount = ASWLog::TASWFileLog::DeleteOldLogs(TestTempDir, "", std::chrono::hours(1));
    const bool existsAfterEmptyPattern = std::filesystem::exists(oldFile);
    const auto starPatternCount = ASWLog::TASWFileLog::DeleteOldLogs(TestTempDir, "*", std::chrono::hours(1));

    // Assert
    CheckEquals(0, emptyPatternCount, "An empty pattern should delete nothing");
    CheckTrue(existsAfterEmptyPattern, "An empty pattern should leave the old file");
    CheckEquals(1, starPatternCount, "The \"*\" pattern should delete every old file");
    CheckFalse(std::filesystem::exists(oldFile), "The \"*\" pattern should remove the old file");
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
    CheckFalse(threw, "DeleteOldLogs should not throw for a file name the ANSI code page can't represent");
    CheckEquals(1, deletedCount, "DeleteOldLogs should match UTF-8 patterns against UTF-8 file names");
    CheckFalse(std::filesystem::exists(oldFile), "The matching old file should be removed");
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
    CheckEquals(1, deletedCount, "DeleteOldLogs should remove the stale matching file");
    CheckFalse(std::filesystem::exists(oldFile), "Old log file should be removed");
    CheckTrue(std::filesystem::exists(newFile), "Recent log file should remain");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_FailedReopen_RetriesAndResumesLogging()
{
    // Arrange
    if (!CanBlockFiles())
        Skip("File permissions don't stop the root user, so the failure can't be simulated");

    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "reopen.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.CircuitBreakerResetDelay = 200ms;
    config.OnError = IgnoreError;

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

    // The file is available again, but the circuit breaker waits File.CircuitBreakerResetDelay before trying it
    logger.CurrentTime = startTime + 100ms;
    logger.LogInfo("too_soon");
    logger.CurrentTime = startTime + 250ms;
    logger.LogInfo("after_recovery");
    const bool openAfterRecovery = logger.IsOpen();
    logger.Close();

    // Assert
    const auto contents = ReadFileText(logFile);
    CheckTrue(initialized, "Initialize should succeed");
    CheckFalse(rotated, "RotateLogFiles should fail while the file is held");
    CheckFalse(openDuringOutage, "The log should report closed after it couldn't be reopened");
    CheckContains(contents, "before_outage", "Entries before the outage should be kept");
    CheckNotContains(contents, "during_outage", "Entries while the file can't be opened are dropped");
    CheckNotContains(contents, "too_soon", "The reopen should not be retried before File.CircuitBreakerResetDelay has passed");
    CheckContains(contents, "after_recovery", "Logging should resume once File.CircuitBreakerResetDelay has passed and the file can be opened");
    CheckTrue(openAfterRecovery, "The log should report open again after recovering");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_FailedReopen_ZeroResetDelayRetriesOnNextWrite()
{
    // Arrange
    if (!CanBlockFiles())
        Skip("File permissions don't stop the root user, so the failure can't be simulated");

    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "reopen_zero.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.CircuitBreakerResetDelay = 0ms;
    config.OnError = IgnoreError;

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
    CheckTrue(initialized, "Initialize should succeed");
    CheckFalse(rotated, "RotateLogFiles should fail while the file is held");
    CheckContains(ReadFileText(logFile), "next_write", "With File.CircuitBreakerResetDelay 0, the next write should reopen the file");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_FailedSizeRotation_WaitsBeforeRetrying()
{
    // Arrange
    if (!CanBlockFiles())
        Skip("File permissions don't stop the root user, so the failure can't be simulated");

    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "rotate_retry.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.EnableRotation = true;
    config.File.MaxFileSizeBytes = 50;
    config.File.RotationRetryDelay = 200ms;
    config.OnError = IgnoreError;

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

    // Still within File.RotationRetryDelay: no new rotation attempt yet
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
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(1, backupCount, "The rotation should succeed once File.RotationRetryDelay has passed");
    CheckContains(backupContents, "line_two", "The entry written when the rotation failed should still reach the file");
    CheckContains(backupContents, "line_three", "A failed rotation should not be retried before File.RotationRetryDelay has passed");
    CheckContains(currentContents, "line_four", "The entry after the retry interval should go to the new log file");
    CheckNotContains(currentContents, "line_three", "The new log file should only hold entries after the successful rotation");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_FileStream_FlushWorksAfterFailedWrite()
{
    // Arrange: a stream left in a failed state, as by a failed write (e.g. a full disk that has room again). The
    // standard flush() does nothing in that state, so the data would stay in the buffer until the file is closed.
    const auto file = TestTempDir / "flush_after_failure.log";
    ASWLog::TASWFileStream stream;
    const bool opened = stream.Open(file);
    stream.setstate(std::ios::failbit);

    // Act
    const bool written = stream.Write("buffered");
    const bool flushed = stream.Flush();
    const auto contents = ReadFileText(file); // Read through a separate handle while the stream is still open
    stream.Close();

    // Assert
    CheckTrue(opened, "Open should succeed");
    CheckTrue(written, "Write should succeed");
    CheckTrue(flushed, "Flush should succeed after an earlier failure");
    CheckEquals(std::string("buffered"), contents, "Flush should write the buffered data to the file");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_FlushImmediatelyAtLevel_FlushesEntriesAtOrAboveTheLevel()
{
    // Arrange: FlushMode::Manual keeps the entries below the level in the file's buffer
    const auto logFile = TestTempDir / "flush_at_level.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.Flush = ASWLog::FlushMode::Manual;
    config.File.FlushImmediatelyAtLevel = ASWLog::Level::Warn;

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act: read through a separate handle while the file is open
    logger.LogInfo("info_entry");
    const auto contentsAfterInfo = ReadFileText(logFile);
    logger.LogWarn("warn_entry");
    const auto contentsAfterWarn = ReadFileText(logFile);
    logger.LogDebug("debug_entry");
    logger.LogRaw(ASWLog::Level::Critical, "raw_critical_entry");
    const auto contentsAfterRaw = ReadFileText(logFile);
    logger.LogForce(ASWLog::Level::Trace, "forced_trace_entry");
    const auto contentsAfterForcedTrace = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(std::string(), contentsAfterInfo, "An entry below the level should stay buffered");
    CheckEquals(std::string(": info_entry\n: warn_entry\n"), contentsAfterWarn,
        "An entry at the level should be flushed with the entries buffered before it");
    CheckEquals(std::string(": info_entry\n: warn_entry\n: debug_entry\nraw_critical_entry"), contentsAfterRaw,
        "A raw entry above the level should be flushed too");
    CheckEquals(contentsAfterRaw, contentsAfterForcedTrace, "A forced entry below the level should stay buffered");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_FlushImmediatelyAtLevel_OffLeavesFlushingToFlushMode()
{
    // Arrange
    const auto logFile = TestTempDir / "flush_at_level_off.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.Flush = ASWLog::FlushMode::Manual;
    config.File.FlushImmediatelyAtLevel = ASWLog::Level::Off;

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act
    logger.LogCritical("critical_entry");
    logger.LogForce(ASWLog::Level::Critical, "forced_critical_entry");
    const auto contentsBeforeFlush = ReadFileText(logFile);
    logger.Flush();
    const auto contentsAfterFlush = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(std::string(), contentsBeforeFlush, "With Level::Off, even a Critical entry should stay buffered");
    CheckEquals(std::string(": critical_entry\n: forced_critical_entry\n"), contentsAfterFlush, "Flush() should write the buffered entries");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Flush_WritesBufferedManualModeEntries()
{
    // Arrange: FlushMode::Manual keeps each entry in the file's buffer until Flush()
    const auto logFile = TestTempDir / "flush_manual.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.Flush = ASWLog::FlushMode::Manual;

    ASWLog::TASWFileLog fileLog;
    ASWLog::IASWLog& logger = fileLog; // As generic code would flush it

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("buffered_entry");
    const auto contentsBefore = ReadFileText(logFile);
    const bool flushed = logger.Flush();
    const auto contentsAfter = ReadFileText(logFile);
    logger.Close();
    const bool flushedWhileClosed = logger.Flush();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckNotContains(contentsBefore, "buffered_entry", "Before Flush(), the entry should still be buffered");
    CheckTrue(flushed, "Flush() should succeed while the file is open");
    CheckContains(contentsAfter, "buffered_entry", "Flush() should write the buffered entry to the file");
    CheckFalse(flushedWhileClosed, "Flush() should return false while the file is closed");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_GetInstance_ReturnsSameInstance()
{
    // Act
    auto& first = ASWLog::TASWFileLog::GetInstance();
    auto& second = ASWLog::TASWFileLog::GetInstance();

    // Assert
    CheckTrue(&first == &second, "GetInstance should return the same logger on every call");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_InitializeAndLogInfo_WritesText()
{
    // Arrange
    const auto logFile = TestTempDir / "aswlog_runtime.log";

    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = logFile;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowWorkingSet = false;
    config.Line.ShowPeakWorkingSet = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.Startup.WriteTimeInfo = false;
    config.Startup.WriteOSInfo = false;
    config.Startup.WriteDriveInfo = false;
    config.Startup.WriteSystemMemoryInfo = false;
    config.Startup.WriteApplicationInfo = false;
    config.Startup.WriteMemoryUsage = false;
    config.File.OpenRetryCount = 1;

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("unit_test_message");
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(logger.IsOpen() == false, "Logger should be closed after explicit close");
    CheckContains(contents, "unit_test_message", "Logged file should contain the test message");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Initialize_SuppressesInfoBannersBelowMinimumLevel()
{
    // Arrange
    const auto logFile = TestTempDir / "suppressed_banners.log";

    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Warn;
    config.Startup.Banner = "should_not_appear_banner";
    config.File.OpenRetryCount = 1;
    // Startup.Write* toggles are left at their defaults (all true) so this test exercises
    // every internal Info-level banner writer, not just a subset.

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogError("this_error_should_appear");
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckNotContains(contents, "should_not_appear_banner", "Startup.Banner (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckNotContains(contents, "Time:", "Init time info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckNotContains(contents, "OS:", "Init OS info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckNotContains(contents, "Drive:", "Init drive info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckNotContains(contents, "System memory:", "Init system memory info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckNotContains(contents, "App:", "Init application info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckNotContains(contents, "App Memory:", "Init app memory info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckContains(contents, "this_error_should_appear", "Messages at or above InitialMinimumLevel should still be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_LogFormatMethods_FormatsMessage()
{
    // Arrange
    const auto logFile = TestTempDir / "format_message.log";

    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.File.OpenRetryCount = 1;

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfoFmt("value={}, suffix={}", 42, "done");
    logger.LogTraceFmt("trace {}", "ok");
    logger.LogForceFmt(ASWLog::Level::Warn, "forced {}", "value");
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckContains(contents, "value=42, suffix=done", "LogInfoFmt should format the message");
    CheckContains(contents, "trace ok", "LogTraceFmt should format the message");
    CheckContains(contents, "forced value", "LogForceFmt should bypass filter and format the message");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_LogFormatMethods_WriteCallerSourceLine()
{
    // Arrange
    const auto logFile = TestTempDir / "fmt_location.log";

    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = logFile;
    config.Line.ShowSourceLine = true;
    config.File.OpenRetryCount = 1;

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    const int callLine = __LINE__ + 1;
    logger.LogInfoFmt("located {}", 1);
    logger.Close();

    // Assert
    const auto contents = ReadFileText(logFile);
    const auto expected = "[Test_ASWLog_FileLog.cpp:" + std::to_string(callLine) + "]: located 1";
    CheckTrue(initialized, "Initialize should succeed");
    CheckContains(contents, expected, "Line.ShowSourceLine should show the file and line of the LogInfoFmt call: " + expected);
    CheckNotContains(contents, "ASWLog_Interface.h", "Line.ShowSourceLine should not show the header that implements the *Fmt methods");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_LogLineMetadata_Options()
{
    // Arrange
    const auto logFile = TestTempDir / "metadata_line.log";

    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.Line.ShowTimestamp = true;
    config.Line.ShowLevel = true;
    config.Line.ShowProcessId = true;
    config.Line.ShowThreadId = true;
    config.Line.ShowWorkingSet = true;
    config.Line.ShowPeakWorkingSet = true;
    config.Line.ShowFunctionName = true;
    config.Line.ShowSourceLine = true;
    config.File.OpenRetryCount = 1;

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("metadata_message");
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckContains(contents, "Z", "Line.ShowTimestamp should add a UTC timestamp");
    CheckContains(contents, "INFO", "Line.ShowLevel should include the log level");
    CheckContains(contents, "[P:", "Line.ShowProcessId should include the process id");
    CheckContains(contents, "[T:", "Line.ShowThreadId should include the thread id");
    CheckContains(contents, "[WS:", "Line.ShowWorkingSet should include working set memory");
    CheckContains(contents, "[PWS:", "Line.ShowPeakWorkingSet should include peak working set memory");
    CheckContains(contents, "Test_LogLineMetadata_Options", "Line.ShowFunctionName should include the calling method name");
    CheckContains(contents, "metadata_message", "Metadata log line should still contain the message");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_LogNewLineAndForceOptions()
{
    // Arrange
    const auto logFile = TestTempDir / "newline_force.log";

    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Error;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.File.OpenRetryCount = 1;

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogWarn("filtered_message");
    logger.LogForce(ASWLog::Level::Warn, "forced_message");
    logger.LogInfo("ignored_message");
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckNotContains(contents, "filtered_message", "Log should respect the minimum level unless forced");
    CheckContains(contents, "forced_message", "LogForce should bypass the minimum level");
    CheckContains(contents, "forced_message\n", "LogForce should append a newline by default");
    CheckNotContains(contents, "ignored_message", "Log should not write a message below the configured minimum level");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_LogProcessAndThreadIds_AreOSIds()
{
    // Arrange
    const auto logFile = TestTempDir / "os_ids.log";
    auto config = MakeRotationTestConfig(logFile);
    config.Line.ShowProcessId = true;
    config.Line.ShowThreadId = true;

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act: log from this thread and from another one
    const auto processId = ASWLog::GetCurrentOSProcessId();
    const auto mainThreadId = ASWLog::GetCurrentOSThreadId();
    logger.LogInfo("main_thread_entry");

    std::uint32_t workerThreadId = 0;
    std::thread worker([&] {
        workerThreadId = ASWLog::GetCurrentOSThreadId();
        logger.LogInfo("worker_thread_entry");
            });
    worker.join();
    logger.Close();

    // Assert
    const auto contents = ReadFileText(logFile);
    const auto processTag = "[P:" + std::to_string(processId) + "]";
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(mainThreadId != workerThreadId, "Two threads should have different OS thread ids");
    CheckContains(contents, processTag + "[T:" + std::to_string(mainThreadId) + "]: main_thread_entry", "The entry should show the OS process id and the logging thread's OS thread id");
    CheckContains(contents, processTag + "[T:" + std::to_string(workerThreadId) + "]: worker_thread_entry", "An entry from another thread should show that thread's OS thread id");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_LogRawOptions()
{
    // Arrange
    const auto logFile = TestTempDir / "raw.log";

    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.File.OpenRetryCount = 1;

    ASWLog::TASWFileLog logger;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.LogRaw(ASWLog::Level::Info, "raw_message");
    logger.LogForceRaw(ASWLog::Level::Warn, "raw_force_message");
    logger.Close();

    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckContains(contents, "raw_message", "LogRaw should write the raw message");
    CheckContains(contents, "raw_force_message", "LogForceRaw should write the raw message even when filtered");
    CheckNotContains(contents, "raw_message\n", "LogRaw should not append a newline by default");
    CheckNotContains(contents, "raw_force_message\n", "LogForceRaw should not append a trailing newline");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_MultiThreadedStress_WritesAllMessagesToDisk()
{
    // Arrange
    const auto logFile = TestTempDir / "stress.log";
    constexpr int threadCount = 4;
    constexpr int messagesPerThread = 100;

    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.File.OpenRetryCount = 1;

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
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(!contents.empty(), "Stress log file should contain at least one entry");
    for (const auto& message : expectedMessages)
    {
        CheckContains(contents, message,
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
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.File.OpenRetryCount = 1;
    config.File.AutoOpenClosePerWrite = true;

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
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(!contents.empty(), "Stress log file should contain at least one entry");
    for (const auto& message : expectedMessages)
    {
        CheckContains(contents, message,
            "Multi-threaded stress log should persist every expected message to disk: " + message);
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnBackupCreated_IsNotCalledWhenRotationFails()
{
    // Arrange
    if (!CanBlockFiles())
        Skip("File permissions don't stop the root user, so the failure can't be simulated");

    const auto logFile = TestTempDir / "not_rotated.log";
    auto config = MakeRotationTestConfig(logFile);
    config.OnError = IgnoreError;
    int callCount = 0;
    config.File.OnBackupCreated = [&callCount](const ASWLog::TASWBackupInfo& /*backup*/) {
            ++callCount;
        };

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("entry");

    // Act
    bool rotated = true;
    {
        TFileBlocker blocker(logFile, false);
        rotated = logger.RotateLogFiles("manual");
    }
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckFalse(rotated, "RotateLogFiles should fail while the file is held");
    CheckEquals(0, callCount, "No backup was made, so OnBackupCreated should not be called");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnBackupCreated_LeavesARenamedBackupToTheApp()
{
    // Arrange: the callback "compresses" the backup to a name outside the backup form, and the size limit is too small
    // for any backup
    const auto logFile = TestTempDir / "compressed.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.MaxBackupTotalBytes = 1;
    std::filesystem::path compressedPath;
    config.File.OnBackupCreated = [&compressedPath](const ASWLog::TASWBackupInfo& backup) {
            compressedPath = backup.BackupPath;
            compressedPath += ".gz";
            std::filesystem::rename(backup.BackupPath, compressedPath);
        };

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("entry");

    // Act
    const bool rotated = logger.RotateLogFiles("manual");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(rotated, "RotateLogFiles should succeed");
    CheckFalse(compressedPath.empty(), "OnBackupCreated should be called");
    CheckTrue(std::filesystem::exists(compressedPath), "The cleanup should leave a file renamed out of the backup form to the application");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnBackupCreated_ReportsDailyBackupAtInitialize()
{
    // Arrange: a log last written on 2026-01-14 (30 hours before the logger starts at 10:00 UTC on 2026-01-15)
    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "daily_event.log";
    CreateAgedFile(logFile, 10, 30h);

    auto config = MakeRotationTestConfig(logFile);
    config.File.EnableDailyRolling = true;
    std::vector<ASWLog::TASWBackupInfo> backups;
    config.File.OnBackupCreated = [&backups](const ASWLog::TASWBackupInfo& backup) {
            backups.push_back(backup);
        };

    TFixedClockFileLog logger;
    logger.CurrentTime = std::chrono::sys_days{ 2026y / 1 / 15 } + 10h;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(1, backups.size(), "The rollover in Initialize() should report its backup");
    if (backups.empty())
        return;

    CheckEquals(std::string("daily"), backups[0].Reason, "The reason should be \"daily\"");
    CheckTrue(backups[0].BackupPath.filename() == "daily_event.daily.2026-01-14.bak", "The backup should be named for the day it holds");
    CheckTrue(backups[0].LogPath == config.File.ResolvePath(), "LogPath should be the log file");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnBackupCreated_ReportsEachBackup()
{
    // Arrange
    const auto logFile = TestTempDir / "event.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.EnableRotation = true;
    config.File.MaxFileSizeBytes = 50;

    std::vector<ASWLog::TASWBackupInfo> backups;
    std::vector<bool> backupExisted;
    config.File.OnBackupCreated = [&backups, &backupExisted](const ASWLog::TASWBackupInfo& backup) {
            backups.push_back(backup);
            backupExisted.push_back(std::filesystem::exists(backup.BackupPath));
        };

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act
    logger.LogInfo("line_one_long_enough_to_reach_the_size_limit_alone");
    logger.LogInfo("line_two"); // Rotates for size first
    const bool rotated = logger.RotateLogFiles("before-upgrade");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(rotated, "RotateLogFiles should succeed");
    CheckEquals(2, backups.size(), "Each rotation should report its backup once");
    if (backups.size() != 2)
        return;

    CheckEquals(std::string("size"), backups[0].Reason, "The size rotation's reason should be \"size\"");
    CheckEquals(std::string("before-upgrade"), backups[1].Reason, "A manual rotation's reason should be its tag");
    CheckTrue(backups[0].LogPath == config.File.ResolvePath(), "LogPath should be the log file");
    CheckStartsWith(backups[1].BackupPath.filename().string(), "event.before-upgrade.", "BackupPath should be the manual backup");
    CheckTrue(backupExisted == std::vector<bool>{ true, true }, "The backup should exist during the call");
    CheckContains(ReadFileText(backups[0].BackupPath), "line_one", "The size backup should hold the entries before the rotation");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnBackupCreated_RunsOutsideTheLockBeforeCleanup()
{
    // Arrange: a size limit too small for any backup, so the cleanup deletes the new one right after the callback
    const auto logFile = TestTempDir / "before_cleanup.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.MaxBackupTotalBytes = 1;

    TLockCheckFileLog logger;
    std::filesystem::path backupPath;
    bool existedInCallback = false;
    bool wasMutexFree = false;
    config.File.OnBackupCreated = [&](const ASWLog::TASWBackupInfo& backup) {
            backupPath = backup.BackupPath;
            existedInCallback = std::filesystem::exists(backup.BackupPath);
            wasMutexFree = logger.IsMutexFree();
            logger.LogInfo("logged_from_the_callback"); // Would deadlock if the lock were held
        };
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("entry");

    // Act
    const bool rotated = logger.RotateLogFiles("manual");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(rotated, "RotateLogFiles should succeed");
    CheckTrue(wasMutexFree, "OnBackupCreated should be called after the logger's lock is released");
    CheckTrue(existedInCallback, "The backup should still exist during the callback");
    CheckFalse(backupPath.empty() || std::filesystem::exists(backupPath), "The cleanup should run after the callback");
    CheckContains(ReadFileText(logFile), "logged_from_the_callback", "The callback should be able to log");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnBackupCreated_ThrowingCallbackStillCleansUp()
{
    // Arrange
    const auto logFile = TestTempDir / "throwing_event.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.MaxBackupTotalBytes = 1;
    std::filesystem::path backupPath;
    config.File.OnBackupCreated = [&backupPath](const ASWLog::TASWBackupInfo& backup) {
            backupPath = backup.BackupPath;
            throw std::runtime_error("callback failed");
        };

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("entry");

    // Act
    bool threw = false;
    bool rotated = false;
    try
    {
        rotated = logger.RotateLogFiles("manual");
    }
    catch (...)
    {
        threw = true;
    }
    logger.LogInfo("after");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckFalse(threw, "An exception from OnBackupCreated should not escape");
    CheckTrue(rotated, "RotateLogFiles should succeed");
    CheckFalse(backupPath.empty() || std::filesystem::exists(backupPath), "The cleanup should still run after the callback threw");
    CheckEquals(std::string(": after\n"), ReadFileText(logFile), "Logging should go on");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnError_ReportsFailedDelete()
{
#if defined(_WIN32)
    // Arrange: an expired backup that another program holds open without delete sharing, so retention can't delete it
    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "retention_held.log";
    const auto heldBackup = TestTempDir / "retention_held.size.2020-01-01_000000_000.bak";
    std::ofstream(heldBackup) << "old";
    std::filesystem::last_write_time(heldBackup, std::filesystem::file_time_type::clock::now() - 48h);

    auto config = MakeRotationTestConfig(logFile);
    config.File.RetentionMaxAge = 24h;
    std::vector<ASWLog::TASWLogError> reports;
    config.OnError = [&reports](const ASWLog::TASWLogError& error) {
            reports.push_back(error);
        };

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("entry");

    // Act
    std::FILE* otherHandle = _wfsopen(heldBackup.c_str(), L"rb", _SH_DENYNO);
    const bool rotated = logger.RotateLogFiles("manual");
    if (otherHandle != nullptr)
        std::fclose(otherHandle);
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(otherHandle != nullptr, "The test should be able to hold the backup open");
    CheckTrue(rotated, "The rotation itself should succeed");
    CheckEquals(1, reports.size(), "The backup that couldn't be deleted should be reported once");
    if (reports.empty())
        return;

    CheckTrue(reports[0].Kind == ASWLog::ErrorKind::DeleteFailed, "The failure should be reported as DeleteFailed");
    CheckTrue(reports[0].Path.filename() == heldBackup.filename(), "The report should name the backup");
    CheckTrue(static_cast<bool>(reports[0].Code), "The report should carry the operating system's error");
#else
    Skip("POSIX deletes a file that another program holds open");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnError_ReportsFailedOpen()
{
    // Arrange: a "folder" that is a file can't hold the log, on any OS and for any user
    const auto notAFolder = TestTempDir / "not_a_folder";
    std::ofstream(notAFolder) << "file";

    auto config = MakeRotationTestConfig(notAFolder / "unopenable.log");
    std::vector<ASWLog::TASWLogError> reports;
    config.OnError = [&reports](const ASWLog::TASWLogError& error) {
            reports.push_back(error);
        };

    // Act
    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Assert
    CheckFalse(initialized, "Initialize should fail");
    CheckEquals(1, reports.size(), "The failed open should be reported once");
    if (reports.empty())
        return;

    CheckTrue(reports[0].Kind == ASWLog::ErrorKind::OpenFailed, "The failure should be reported as OpenFailed");
    CheckTrue(reports[0].Path == config.File.ResolvePath().parent_path(), "The report should name the folder that couldn't be created");
    CheckTrue(static_cast<bool>(reports[0].Code), "The report should carry the operating system's error");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnError_ReportsFailedRotationAndReopen()
{
    // Arrange
    if (!CanBlockFiles())
        Skip("File permissions don't stop the root user, so the failure can't be simulated");

    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "reported.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.CircuitBreakerResetDelay = 0ms; // Each entry tries to reopen the file
    std::vector<ASWLog::TASWLogError> reports;
    config.OnError = [&reports](const ASWLog::TASWLogError& error) {
            reports.push_back(error);
        };

    const auto startTime = std::chrono::sys_days{ 2026y / 1 / 15 } + 10h;
    TFixedClockFileLog logger;
    logger.CurrentTime = startTime;
    const bool initialized = logger.Initialize(config);

    // Act
    {
        // The rotation's rename fails, and so does reopening the log afterward and for each entry
        TFileBlocker blocker(logFile, true);
        logger.RotateLogFiles("manual");
        logger.LogInfo("dropped_1"); // Within ErrorReportInterval (1 minute): not reported
        logger.LogInfo("dropped_2");
        logger.CurrentTime = startTime + 1min;
        logger.LogInfo("dropped_3"); // Reported, with the two left out
    }
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(3, reports.size(), "The rotation and two of the failed opens should be reported");
    if (reports.size() != 3)
        return;

    CheckTrue(reports[0].Kind == ASWLog::ErrorKind::RotationFailed, "The failed rename should be reported as RotationFailed");
    CheckTrue(reports[0].Path == config.File.ResolvePath(), "The rotation report should name the log file");
    CheckTrue(static_cast<bool>(reports[0].Code), "The rotation report should carry the operating system's error");
    CheckTrue(reports[1].Kind == ASWLog::ErrorKind::OpenFailed, "The failed reopen should be reported as OpenFailed");
    CheckTrue(reports[1].Path == config.File.ResolvePath(), "The open report should name the log file");
    CheckTrue(static_cast<bool>(reports[1].Code), "The open report should carry the operating system's error");
    CheckEquals(0, reports[1].SuppressedCount, "Nothing was left out before the first open report");
    CheckTrue(reports[2].Kind == ASWLog::ErrorKind::OpenFailed, "The failed open after the interval should be reported");
    CheckEquals(2, reports[2].SuppressedCount, "The report should count the failed opens left out");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnError_ReportsFailedSync()
{
    // Arrange: the null device takes every write, but can't be synced to disk (FlushFileBuffers fails with
    // ERROR_INVALID_FUNCTION on Windows, fsync with EINVAL on Linux)
#if defined(_WIN32)
    auto config = MakeRotationTestConfig("NUL"); // A device name in any folder
#else
    if (!std::filesystem::exists("/dev/null"))
        Skip("/dev/null doesn't exist");

    auto config = MakeRotationTestConfig("/dev/null"); // Absolute, so it replaces the folder
#endif
    config.File.SyncToDiskAtLevel = ASWLog::Level::Error;
    config.ErrorReportInterval = std::chrono::milliseconds(0);
    std::vector<ASWLog::TASWLogError> reports;
    config.OnError = [&reports](const ASWLog::TASWLogError& error) {
            reports.push_back(error);
        };

    // Act
    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    logger.LogWarn("flushed, not synced"); // Below the level, and FlushMode::EveryWrite flushes it
    const auto reportsAfterWarn = reports.size();
    logger.LogError("flushed and synced");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed: the null device can be opened");
    CheckEquals(0, reportsAfterWarn, "An entry below the level shouldn't be synced");
    CheckEquals(1, reports.size(), "The failed sync should be reported once");
    if (reports.size() == 1)
    {
        CheckTrue(reports[0].Kind == ASWLog::ErrorKind::SyncFailed, "The report should be SyncFailed");
        CheckTrue(reports[0].Path == config.File.ResolvePath(), "The report should name the log file");
        CheckTrue(static_cast<bool>(reports[0].Code), "The report should carry the operating system's error");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnError_ReportsFullDisk()
{
#if defined(__linux__)
    // Arrange: every write to /dev/full fails with "No space left on device"
    if (!std::filesystem::exists("/dev/full"))
        Skip("/dev/full doesn't exist");

    auto config = MakeRotationTestConfig("/dev/full"); // Absolute, so it replaces the folder
    config.ErrorReportInterval = std::chrono::milliseconds(0);
    std::vector<ASWLog::TASWLogError> reports;
    config.OnError = [&reports](const ASWLog::TASWLogError& error) {
            reports.push_back(error);
        };

    // Act
    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("buffered, then flushed"); // FlushMode::EveryWrite: the flush fails
    logger.LogInfo(std::string(256 * 1024, 'x')); // Larger than the stdio buffer: the write itself fails
    logger.Close();

    // Assert
    const auto isReported = [&reports](ASWLog::ErrorKind kind) {
            for (const auto& error : reports)
            {
                if (error.Kind == kind && error.Code == std::errc::no_space_on_device && error.Path == "/dev/full")
                    return true;
            }

            return false;
        };

    CheckTrue(initialized, "Initialize should succeed: /dev/full can be opened");
    CheckTrue(isReported(ASWLog::ErrorKind::FlushFailed), "The failed flush should be reported, with ENOSPC");
    CheckTrue(isReported(ASWLog::ErrorKind::WriteFailed), "The failed write should be reported, with ENOSPC");
#else
    Skip("Needs Linux's /dev/full");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnLogEntry_FiresForQualifyingLevelsOnly()
{
    // Arrange
    const auto logFile = TestTempDir / "callback.log";

    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.File.OpenRetryCount = 1;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Error;

    std::vector<ASWLog::Level> callbackLevels;
    std::vector<std::string> callbackMessages;
    std::vector<std::string> callbackRecordMessages;
    std::vector<ASWLog::TASWLogRecord> callbackRecords; // Message cleared: it's only valid during the call
    config.OnLogEntry = [&](const ASWLog::TASWLogRecord& record, std::string_view line)
        {
            callbackLevels.push_back(record.LogLevel);
            callbackMessages.emplace_back(line);
            callbackRecordMessages.emplace_back(record.Message);
            callbackRecords.push_back(record);
            callbackRecords.back().Message = {};
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
    CheckTrue(initialized, "Initialize should succeed");
    CheckContains(contents, "below_threshold", "Entries below OnLogEntryMinimumLevel should still be written to the file");
    CheckEquals(2, callbackMessages.size(), "OnLogEntry should only fire for entries at or above OnLogEntryMinimumLevel");
    if (callbackMessages.size() == 2)
    {
        CheckEquals(static_cast<int32_t>(ASWLog::Level::Error), static_cast<int32_t>(callbackLevels[0]), "First callback should report the Error entry's level");
        CheckContains(callbackMessages[0], "at_threshold", "Callback should receive the same formatted line written to disk");
        CheckEquals(static_cast<int32_t>(ASWLog::Level::Critical), static_cast<int32_t>(callbackLevels[1]), "Second callback should report the Critical entry's level");
        CheckContains(callbackMessages[1], "above_threshold", "Callback should receive the same formatted line written to disk");
        CheckEquals(std::string("at_threshold"), callbackRecordMessages[0], "Callback should receive the entry's record, with the unformatted message");
        CheckTrue(callbackRecords[0].Timestamp != std::chrono::system_clock::time_point{}, "The callback's record should be stamped with its time");
        CheckEquals(static_cast<int64_t>(ASWLog::GetCurrentOSThreadId()), static_cast<int64_t>(callbackRecords[0].ThreadId), "The callback's record should carry the logging thread's id");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_OnLogEntry_ReentrantCallbackDoesNotDeadlock()
{
    // Arrange
    const auto logFile = TestTempDir / "reentrant.log";

    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.File.OpenRetryCount = 1;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Error;

    ASWLog::TASWFileLog logger;
    bool reentered = false;
    config.OnLogEntry = [&logger, &reentered](const ASWLog::TASWLogRecord&, std::string_view)
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
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(reentered, "Callback should have fired and re-entered the logger");
    CheckContains(contents, "reentrant_message", "Re-entrant Log call from the callback should complete and be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Periodic_CloseStopsAndOpenRestartsTheThread()
{
    // Arrange
    const auto logFile = TestTempDir / "periodic_reopen.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.Flush = ASWLog::FlushMode::Periodic;
    config.File.FlushInterval = std::chrono::milliseconds(10);

    TWakeCountingFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act: whether a stopped thread would still wake can only be seen by waiting several of its intervals
    const bool closed = logger.Close();
    const int wakeCountAfterClose = logger.WakeCount.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const int wakeCountWhileClosed = logger.WakeCount.load();
    const bool opened = logger.Open();
    logger.LogInfo("entry_after_reopen");
    const auto contents = WaitForFileText(logFile, "entry_after_reopen");

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(closed, "Close should succeed");
    CheckEquals(wakeCountAfterClose, wakeCountWhileClosed, "Close should stop the flush thread");
    CheckTrue(opened, "Open should succeed");
    CheckEquals(std::string(": entry_after_reopen\n"), contents, "Open should start the flush thread again");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Periodic_CloseStopsTheThreadWithoutWaitingForTheInterval()
{
    // Arrange: an interval the test would never live to see
    const auto logFile = TestTempDir / "periodic_close.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.Flush = ASWLog::FlushMode::Periodic;
    config.File.FlushInterval = std::chrono::hours(1);

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    GiveWorkerTimeToStartWaiting();

    // Act
    logger.LogInfo("buffered_entry");
    const auto contentsBeforeClose = ReadFileText(logFile);
    const auto closeStart = std::chrono::steady_clock::now();
    const bool closed = logger.Close();
    const auto closeTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - closeStart);
    const auto contentsAfterClose = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(std::string(), contentsBeforeClose, "Writing an entry shouldn't flush it in Periodic mode");
    CheckTrue(closed, "Close should succeed");
    CheckLessThan(closeTime.count(), WaitTimeout.count(), "Close should stop the flush thread without waiting for its interval");
    CheckEquals(std::string(": buffered_entry\n"), contentsAfterClose, "Close should flush the buffered entry");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Periodic_ConcurrentLoggingAndReconfigure()
{
    // Arrange: a short interval, so the flush thread runs often while the other threads log and reconfigure
    const auto logFile = TestTempDir / "periodic_concurrent.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.Flush = ASWLog::FlushMode::Periodic;
    config.File.FlushInterval = std::chrono::milliseconds(1);

    constexpr int ThreadCount = 4;
    constexpr int MaxEntriesPerThread = 5000;

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act: while the threads log, switch between Periodic (with changing intervals) and Manual, which starts, wakes and
    // stops the flush thread
    std::atomic<bool> stopLogging{ false };
    std::vector<int> entryCounts(ThreadCount, 0); // Each thread sets its own
    std::vector<std::thread> threads;
    for (int threadIndex = 0; threadIndex < ThreadCount; ++threadIndex)
    {
        threads.emplace_back([&logger, &stopLogging, &entryCounts, threadIndex] {
                int entry = 0;
                while (entry < MaxEntriesPerThread && !stopLogging.load())
                    logger.LogInfo(std::format("t{}_{}", threadIndex, entry++));

                entryCounts[threadIndex] = entry;
            });
    }

    int failedReconfigures = 0;
    for (int change = 0; change < 30; ++change)
    {
        config.File.Flush = change % 3 == 2 ? ASWLog::FlushMode::Manual : ASWLog::FlushMode::Periodic;
        config.File.FlushInterval = std::chrono::milliseconds(1 + change % 3);
        if (!logger.Reconfigure(config))
            ++failedReconfigures;
    }

    stopLogging.store(true);
    for (auto& thread : threads)
        thread.join();

    logger.Flush();
    const auto contents = ReadFileText(logFile);
    int entryCount = 0;
    for (const auto count : entryCounts)
        entryCount += count;

    // Assert
    Log(std::format("  Logged {} entries while reconfiguring", entryCount));
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(0, failedReconfigures, "Every Reconfigure should succeed");
    CheckEquals(entryCount, std::count(contents.begin(), contents.end(), '\n'), "Every entry should be written once");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Periodic_DestructorStopsTheThreadWithoutWaitingForTheInterval()
{
    // Arrange
    const auto logFile = TestTempDir / "periodic_destructor.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.Flush = ASWLog::FlushMode::Periodic;
    config.File.FlushInterval = std::chrono::hours(1);

    // Act
    bool initialized = false;
    std::chrono::steady_clock::time_point destroyStart;
    {
        ASWLog::TASWFileLog logger;
        initialized = logger.Initialize(config);
        GiveWorkerTimeToStartWaiting();
        logger.LogInfo("buffered_entry");
        destroyStart = std::chrono::steady_clock::now();
    }
    const auto destroyTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - destroyStart);
    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckLessThan(destroyTime.count(), WaitTimeout.count(), "The destructor should stop the flush thread without waiting for its interval");
    CheckEquals(std::string(": buffered_entry\n"), contents, "The destructor should flush the buffered entry");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Periodic_ErrorHandlerCanCloseAndReopenOnTheThread()
{
    // Arrange: the flush thread's first flush fails, and its OnError call, on that thread, closes and reopens the
    // logger, which stops and then keeps that same thread
    const auto logFile = TestTempDir / "periodic_error_handler.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.Flush = ASWLog::FlushMode::Periodic;
    config.File.FlushInterval = std::chrono::milliseconds(10);

    std::atomic<bool> reported{ false };
    ASWLog::ErrorKind reportedKind = ASWLog::ErrorKind::Exception;
    std::thread::id reportThreadId;
    bool closedInHandler = false;
    bool reopenedInHandler = false;

    TFailingFlushFileLog logger;
    logger.FailedFlushCount = 1;
    config.OnError = [&](const ASWLog::TASWLogError& error) {
            reportedKind = error.Kind;
            reportThreadId = std::this_thread::get_id();
            closedInHandler = logger.Close();
            reopenedInHandler = logger.Open();
            reported.store(true);
        };

    const bool initialized = logger.Initialize(config);

    // Act
    const bool wasReported = WaitUntil([&] {
            return reported.load();
        });
    logger.LogInfo("entry_after_reopen");
    const auto contents = WaitForFileText(logFile, "entry_after_reopen");

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(wasReported, "The failed flush should be reported");
    if (wasReported)
    {
        CheckEquals(static_cast<int>(ASWLog::ErrorKind::FlushFailed), static_cast<int>(reportedKind), "The report should be a FlushFailed");
        CheckTrue(reportThreadId != std::this_thread::get_id(), "The flush thread should report its own failure");
        CheckTrue(closedInHandler, "Close should succeed from OnError on the flush thread");
        CheckTrue(reopenedInHandler, "Open should succeed from OnError on the flush thread");
    }
    CheckContains(contents, "entry_after_reopen", "After the reopen, the flush thread should flush again");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Periodic_FlushesAfterTheLastEntry()
{
    // Arrange
    const auto logFile = TestTempDir / "periodic_last_entry.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.Flush = ASWLog::FlushMode::Periodic;
    config.File.FlushInterval = std::chrono::milliseconds(10);

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act: nothing more is logged after this entry
    logger.LogInfo("last_entry");
    const auto contents = WaitForFileText(logFile, "last_entry");

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(std::string(": last_entry\n"), contents, "The flush thread should flush the last entry within the interval");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Periodic_ReconfigureAppliesANewInterval()
{
    // Arrange: the thread starts out waiting for an interval the test would never live to see
    const auto logFile = TestTempDir / "periodic_new_interval.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.Flush = ASWLog::FlushMode::Periodic;
    config.File.FlushInterval = std::chrono::hours(1);

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    GiveWorkerTimeToStartWaiting();

    // Act
    config.File.FlushInterval = std::chrono::milliseconds(10);
    const bool reconfigured = logger.Reconfigure(config);
    logger.LogInfo("entry");
    const auto contents = WaitForFileText(logFile, "entry");

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(reconfigured, "Reconfigure should succeed");
    CheckEquals(std::string(": entry\n"), contents, "The flush thread should use the new interval at once");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Periodic_ReconfigureStartsAndStopsTheThread()
{
    // Arrange
    const auto logFile = TestTempDir / "periodic_reconfigure.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.Flush = ASWLog::FlushMode::Manual;
    config.File.FlushInterval = std::chrono::milliseconds(10);

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act: start the thread, then stop it. Whether a stopped thread would still flush can only be seen by waiting
    // several of its intervals.
    config.File.Flush = ASWLog::FlushMode::Periodic;
    const bool startedPeriodic = logger.Reconfigure(config);
    logger.LogInfo("periodic_entry");
    const auto contentsInPeriodic = WaitForFileText(logFile, "periodic_entry");

    config.File.Flush = ASWLog::FlushMode::Manual;
    const bool stoppedPeriodic = logger.Reconfigure(config);
    logger.LogInfo("manual_entry");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto contentsInManual = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(startedPeriodic, "Reconfigure to Periodic should succeed");
    CheckTrue(stoppedPeriodic, "Reconfigure to Manual should succeed");
    CheckEquals(std::string(": periodic_entry\n"), contentsInPeriodic, "Reconfigure to Periodic should start the flush thread");
    CheckEquals(std::string(": periodic_entry\n"), contentsInManual, "Reconfigure to Manual should stop the flush thread");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Periodic_ZeroIntervalFlushesEveryEntry()
{
    // Arrange
    const auto logFile = TestTempDir / "periodic_zero_interval.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.Flush = ASWLog::FlushMode::Periodic;
    config.File.FlushInterval = std::chrono::milliseconds(0);

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act
    logger.LogInfo("entry");
    const auto contents = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(std::string(": entry\n"), contents, "With no interval, each entry should be flushed as it is written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Reconfigure_FlushesEntriesBufferedByPreviousMode()
{
    // Arrange: FlushMode::Manual keeps each entry in the file's buffer until it is flushed
    const auto logFile = TestTempDir / "reconfigure_flush.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.Flush = ASWLog::FlushMode::Manual;

    ASWLog::TASWFileLog logger;
    CheckTrue(logger.Initialize(config), "Initialize should succeed");

    // Act
    logger.LogInfo("buffered_entry");
    const auto contentsBefore = ReadFileText(logFile);
    config.File.Flush = ASWLog::FlushMode::EveryWrite;
    const bool reconfigured = logger.Reconfigure(config);
    const auto contentsAfterReconfigure = ReadFileText(logFile);
    logger.LogInfo("flushed_entry");
    const auto contentsAfterEntry = ReadFileText(logFile);

    // Assert
    CheckNotContains(contentsBefore, "buffered_entry",
        "With FlushMode::Manual, the entry should still be buffered");
    CheckTrue(reconfigured, "Reconfigure() should succeed");
    CheckEquals(std::string(": buffered_entry\n"), contentsAfterReconfigure,
        "Reconfigure() should flush the entries buffered under the previous flush mode");
    CheckEquals(std::string(": buffered_entry\n: flushed_entry\n"), contentsAfterEntry,
        "The new flush mode should apply to the next entry");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Reconfigure_MovesOutputToNewFile()
{
    // Arrange
    const auto firstFile = TestTempDir / "reconfigure_first.log";
    const auto secondFile = TestTempDir / "reconfigure_second.log";
    auto config = MakeRotationTestConfig(firstFile);

    ASWLog::TASWFileLog logger;
    CheckTrue(logger.Initialize(config), "Initialize should succeed");

    // Act
    logger.LogInfo("to_first");
    config.File.FilePath = secondFile;
    const bool movedToSecond = logger.Reconfigure(config);
    logger.LogInfo("to_second");

    // On Windows, an open file can't be deleted, which shows that the first file was closed
    const auto firstContents = ReadFileText(firstFile);
    std::error_code removeError;
    const bool firstFileRemoved = std::filesystem::remove(firstFile, removeError);

    config.File.AutoOpenClosePerWrite = true;
    const bool switchedToAutoOpenClose = logger.Reconfigure(config);
    const bool openAfterSwitch = logger.IsOpen();
    logger.LogInfo("auto_open_close");

    // Assert
    CheckTrue(movedToSecond, "Reconfigure() to a new file path should succeed");
    CheckEquals(std::string(": to_first\n"), firstContents, "Entries before Reconfigure() should stay in the first file");
    CheckTrue(firstFileRemoved, "Reconfigure() should close the first file");
    CheckTrue(switchedToAutoOpenClose, "Reconfigure() to File.AutoOpenClosePerWrite should succeed");
    CheckFalse(openAfterSwitch, "With File.AutoOpenClosePerWrite, the file should stay closed until the next entry");
    CheckEquals(std::string(": to_second\n: auto_open_close\n"), ReadFileText(secondFile),
        "Entries after Reconfigure() should go to the new file, in either open mode");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Reconfigure_UnopenableFileFailsButLoggerStaysInitialized()
{
    // Arrange: a "folder" that is a file can't hold the new log, on any OS and for any user
    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "reconfigure_kept.log";
    const auto notAFolder = TestTempDir / "not_a_folder";
    std::ofstream(notAFolder) << "file";

    auto config = MakeRotationTestConfig(logFile);
    config.File.CircuitBreakerResetDelay = 0ms;
    config.OnError = IgnoreError;

    auto unopenableConfig = config;
    unopenableConfig.File.FilePath = notAFolder / "unopenable.log";

    ASWLog::TASWFileLog logger;
    CheckTrue(logger.Initialize(config), "Initialize should succeed");

    // Act
    const bool reconfiguredToUnopenable = logger.Reconfigure(unopenableConfig);
    logger.LogInfo("dropped");
    const bool reconfiguredBack = logger.Reconfigure(config);
    logger.LogInfo("written");

    // Assert
    CheckFalse(reconfiguredToUnopenable, "Reconfigure() should fail if the new file can't be opened");
    CheckTrue(logger.GetConfig()->File.FilePath == config.File.FilePath,
        "GetConfig() should return the config of the last Reconfigure()");
    CheckTrue(reconfiguredBack, "The logger should stay initialized after the failure, so it can be reconfigured again");
    CheckEquals(std::string(": written\n"), ReadFileText(logFile),
        "Logging should resume in the file of the last Reconfigure()");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_RetentionMaxAge_DefaultDisabledPreservesOldBackups()
{
    // Arrange
    const auto logFile = TestTempDir / "retention_disabled.log";

    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.File.OpenRetryCount = 1;
    // config.File.RetentionMaxAge left at its default (0 = disabled)

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
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(rotated, "RotateLogFiles should succeed");
    CheckTrue(std::filesystem::exists(staleBackup), "File.RetentionMaxAge left at its default (disabled) should not delete old backups after rotation");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_RetentionMaxAge_DeletesExpiredBackupsAfterRotation()
{
    // Arrange
    const auto logFile = TestTempDir / "retention.log";

    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = logFile;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.File.OpenRetryCount = 1;
    config.File.RetentionMaxAge = std::chrono::hours(1);

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
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(rotated, "RotateLogFiles should succeed");
    CheckFalse(std::filesystem::exists(staleBackup), "Stale backup older than File.RetentionMaxAge should be deleted automatically after rotation");
    CheckTrue(std::filesystem::exists(logFile), "Log file should be recreated after rotation");
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
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(rotatedFirst, "The first RotateLogFiles should succeed");
    CheckTrue(rotatedSecond, "The second RotateLogFiles should succeed");
    CheckContains(firstContents, "first_segment", "The backup should be named <stem>.<reason>.<YYYY-MM-DD_HHMMSS_mmm>.bak and keep the first segment");
    CheckNotContains(firstContents, "second_segment", "A second rotation should not replace the first backup");
    CheckContains(secondContents, "second_segment", "A second rotation with a taken name should add _1 to the name");
    CheckContains(ReadFileText(logFile), "third_segment", "Entries after the last rotation should go to the reopened log file");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_SetEnabled_FalseStopsAutoOpenCloseLogging()
{
    // Arrange: with File.AutoOpenClosePerWrite the file is only open while an entry is written
    const auto logFile = TestTempDir / "disabled.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.AutoOpenClosePerWrite = true;
    ASWLog::TASWFileLog logger;
    CheckTrue(logger.Initialize(config), "Initialize should succeed");

    // Act: Close() doesn't stop logging in this mode, since the next entry reopens the file
    logger.Close();
    logger.LogInfo("after_close");
    const auto contentsAfterClose = ReadFileText(logFile);

    logger.SetEnabled(false);
    std::filesystem::remove(logFile);
    logger.LogInfo("disabled");
    logger.LogForce(ASWLog::Level::Critical, "disabled_forced");
    const bool fileExistsWhileDisabled = std::filesystem::exists(logFile);

    logger.SetEnabled(true);
    logger.LogInfo("enabled_again");

    // Assert
    CheckEquals(std::string(": after_close\n"), contentsAfterClose,
        "With File.AutoOpenClosePerWrite, an entry after Close() reopens the file (documented on IASWLog::Close())");
    CheckFalse(fileExistsWhileDisabled, "A disabled logger should not reopen (or recreate) its file, even for a forced entry");
    CheckEquals(std::string(": enabled_again\n"), ReadFileText(logFile), "Logging should resume once enabled again");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_SizeRotation_AutoOpenCloseCountsOtherWriters()
{
    // Arrange: in File.AutoOpenClosePerWrite mode the log is closed between writes, so other processes can append to it
    const auto logFile = TestTempDir / "sized_shared.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.AutoOpenClosePerWrite = true;
    config.File.EnableRotation = true;
    config.File.MaxFileSizeBytes = 100;

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
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(1, backupCount, "Another writer's bytes should count toward File.MaxFileSizeBytes, since the size is read again at each open");
    CheckContains(backupContents, "own_first", "The backup should hold this logger's earlier entry");
    CheckContains(backupContents, otherWriterContents, "The backup should hold the other writer's entry");
    CheckContains(currentContents, "own_second", "The entry that triggered the rotation should go to the new log file");
    CheckNotContains(currentContents, "x", "The new log file should not contain the other writer's earlier entry");
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
    config.File.EnableRotation = true;
    config.File.MaxFileSizeBytes = 100;

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
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(1, backupCount, "The first write should rotate a log that was already over File.MaxFileSizeBytes when opened");
    CheckEquals(earlierContents, backupContents, "The backup should hold the earlier run's contents");
    CheckContains(currentContents, "after_restart", "The new entry should go to the reopened log file");
    CheckNotContains(currentContents, "x", "The reopened log file should not contain the earlier run's contents");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_SizeRotation_RotatesWhenLimitReached()
{
    // Arrange: ten lines of about 18 bytes each against a 50-byte limit need several rotations
    const auto logFile = TestTempDir / "sized.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.EnableRotation = true;
    config.File.MaxFileSizeBytes = 50;

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

    CheckTrue(initialized, "Initialize should succeed");
    CheckGreaterThanOrEqual(backupCount, 2, "Reaching File.MaxFileSizeBytes should rotate the open log to a .size. backup each time");
    CheckNotContains(currentContents, "size_line_0_end", "The first lines should have been rotated out of the current log");

    for (int index = 0; index < LineCount; ++index)
    {
        const auto marker = "size_line_" + std::to_string(index) + "_end";
        const auto first = allContents.find(marker);
        const bool foundOnce = first != std::string::npos && allContents.find(marker, first + 1) == std::string::npos;
        CheckTrue(foundOnce, "Each line should be in exactly one of the log and its backups: " + marker);
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_SyncToDiskAtLevel_FlushesAndSyncsEntriesAtOrAboveTheLevel()
{
    // Arrange: only SyncToDiskAtLevel makes the logger flush (whether the data reached the disk itself can't be seen
    // from here, but a failed sync would be reported)
    const auto logFile = TestTempDir / "sync_at_level.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.Flush = ASWLog::FlushMode::Manual;
    config.File.FlushImmediatelyAtLevel = ASWLog::Level::Off;
    config.File.SyncToDiskAtLevel = ASWLog::Level::Warn;
    config.ErrorReportInterval = std::chrono::milliseconds(0);
    std::vector<ASWLog::TASWLogError> reports;
    config.OnError = [&reports](const ASWLog::TASWLogError& error) {
            reports.push_back(error);
        };

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act: read through a separate handle while the file is open
    logger.LogInfo("info_entry");
    const auto contentsAfterInfo = ReadFileText(logFile);
    logger.LogRaw(ASWLog::Level::Warn, "raw_warn_entry");
    const auto contentsAfterWarn = ReadFileText(logFile);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(std::string(), contentsAfterInfo, "An entry below the level should stay buffered");
    CheckEquals(std::string(": info_entry\nraw_warn_entry"), contentsAfterWarn,
        "An entry at the level should be flushed with the entries buffered before it");
    CheckEquals(0, reports.size(), "Syncing a log file should succeed");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_FileLog::Test_Write_EarlierRecordDoesNotRollLogBack()
{
    // Arrange: the logger's clock is just past UTC midnight, so the log already belongs to the new day, and an entry
    // stamped just before midnight arrives late (e.g. its thread got the lock after another thread's entry)
    using namespace std::chrono_literals;
    const auto logFile = TestTempDir / "late_entry.log";
    auto config = MakeRotationTestConfig(logFile);
    config.File.EnableDailyRolling = true;
    config.Line.ShowTimestamp = true; // Shows which time each line got

    const auto dayTwo = std::chrono::sys_days{ 2026y / 1 / 16 };
    TFixedClockFileLog logger;
    logger.CurrentTime = dayTwo + 1min;

    ASWLog::TASWLogRecord lateRecord;
    lateRecord.LogLevel = ASWLog::Level::Info;
    lateRecord.Message = "late_entry";
    lateRecord.Timestamp = dayTwo - 1ms;

    // Act
    const bool initialized = logger.Initialize(config);
    logger.Write(lateRecord);
    logger.LogInfo("next_entry");
    logger.Close();

    // Assert
    int backupCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(TestTempDir))
    {
        if (entry.path().extension() == ".bak")
            ++backupCount;
    }

    const auto contents = ReadFileText(logFile);
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(0, backupCount, "An entry stamped before midnight should not roll the new day's log back over");
    CheckContains(contents, "[2026-01-15T23:59:59.999Z]", "The late entry's line should show its record's time: " + contents);
    CheckContains(contents, "late_entry", "Both entries should be in the current log");
    CheckContains(contents, "next_entry", "Both entries should be in the current log");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_FileLog)
