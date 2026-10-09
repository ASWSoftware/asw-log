/* **************************************************************************
Test_ASWLog_TextLogBase.cpp
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
#include "Test_ASWLog_TextLogBase.h"
//---------------------------------------------------------------------------
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_FileLog.h"
#include "ASWLog_Formatter.h"
#include "ASWLog_TextLogBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

const auto GroupBaseTempDir = std::filesystem::temp_directory_path() / "aswlog_textlogbase_tests";
const auto TestTempDir = GroupBaseTempDir / "test";

// How long a test waits for something another thread does (e.g. an asynchronous logger's worker), before it fails
constexpr std::chrono::milliseconds WaitTimeout = std::chrono::seconds(5);

// How long a test waits to see that another thread did NOT get past a point (e.g. a call that must wait)
constexpr std::chrono::milliseconds AbsenceWait = std::chrono::milliseconds(50);

// What a TMemoryTextLog wrote. Outlives the logger, so a test can check what its destructor wrote.
struct TMemoryOutput
{
    std::vector<std::string> Lines;
    std::vector<bool> EndsLine;
    std::vector<std::thread::id> WriteThreadIds; // The thread that wrote each line
    int AfterEntryCount = 0;
    int FlushCount = 0;
    std::size_t LineCountAtLastFlush = 0; // How many lines were written when FlushUnlocked() was last called
    std::size_t LineCountAtLastReconfigure = 0; // The same for ReconfigureUnlocked()
};

// Holds every write of a TMemoryTextLog while closed, so a test can keep an asynchronous logger's worker busy with an
// entry while it queues more
class TWriteGate
{
private:
    std::mutex m_Mutex;
    std::condition_variable m_Changed;
    bool m_IsOpen = false;
    int m_WaitingCount = 0;

public:
    // Lets every write through, now and from then on
    void Open()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_IsOpen = true;
        m_Changed.notify_all();
    }

    // Called by each write: waits until the gate is open
    void Pass()
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        ++m_WaitingCount;
        m_Changed.notify_all();
        m_Changed.wait(lock, [this] {
                    return m_IsOpen;
                });
        --m_WaitingCount;
    }

    // Waits until a write is held at the gate, for at most WaitTimeout. Returns false if none came.
    bool WaitForWrite()
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        return m_Changed.wait_for(lock, WaitTimeout, [this] {
                    return m_WaitingCount > 0;
                });
    }
};

// A text logger built only from the TASWTextLogBase hooks, writing to memory
class TMemoryTextLog final : public ASWLog::TASWTextLogBase
{
private:
    TMemoryOutput& m_Output;

public:
    bool IsReady = true; // Result of EnsureReadyUnlocked() while initialized and open
    bool AcceptsWrites = true; // Result of PrepareWriteUnlocked()
    bool FlushResult = true; // Result of FlushUnlocked()
    bool ReconfigureResult = true; // Result of ReconfigureUnlocked()
    bool ReportsOnWrite = false; // WriteLineUnlocked() reports a WriteFailed failure, but writes the line
    bool ThrowsOnFlush = false;
    bool ThrowsOnWrite = false;
    TWriteGate* Gate = nullptr; // If set, each write waits at it
    // What ReconfigureUnlocked() saw: the banner of the config it replaced, and of the config stored by then
    std::vector<std::string> ReconfiguredFromBanners;
    std::vector<std::string> ReconfiguredToBanners;

protected:
    void AfterEntryUnlocked() override
    {
        ++m_Output.AfterEntryCount;
    }

    bool CloseUnlocked() override
    {
        m_IsOpen.store(false);
        m_IsInitialized.store(false);
        return true;
    }

    bool EnsureReadyUnlocked() override
    {
        return IsReady && TASWTextLogBase::EnsureReadyUnlocked();
    }

    bool FlushUnlocked() override
    {
        if (ThrowsOnFlush)
            throw std::runtime_error("flush failed");

        ++m_Output.FlushCount;
        m_Output.LineCountAtLastFlush = m_Output.Lines.size();
        return FlushResult;
    }

    std::string_view GetLoggerClassName() const noexcept override
    {
        return "TMemoryTextLog";
    }

    bool InitializeUnlocked() override
    {
        return OpenUnlocked();
    }

    bool OpenUnlocked() override
    {
        m_IsOpen.store(true);
        m_IsInitialized.store(true);
        return true;
    }

    bool PrepareWriteUnlocked(std::chrono::system_clock::time_point /*now*/) override
    {
        return AcceptsWrites;
    }

    bool ReconfigureUnlocked(const ASWLog::TASWLogConfig& previous) override
    {
        ReconfiguredFromBanners.push_back(previous.Startup.Banner);
        ReconfiguredToBanners.push_back(GetConfigUnlocked().Startup.Banner);
        m_Output.LineCountAtLastReconfigure = m_Output.Lines.size();
        return ReconfigureResult;
    }

    void WriteLineUnlocked(ASWLog::Level /*level*/, std::string_view line, bool endsLine) override
    {
        if (Gate != nullptr)
            Gate->Pass();

        if (ThrowsOnWrite)
            throw std::runtime_error("write failed");

        if (ReportsOnWrite)
        {
            ASWLog::TASWLogError error;
            error.Kind = ASWLog::ErrorKind::WriteFailed;
            error.Message = "memory write failed";
            ReportErrorUnlocked(std::move(error));
        }

        m_Output.Lines.emplace_back(line);
        m_Output.EndsLine.push_back(endsLine);
        m_Output.WriteThreadIds.push_back(std::this_thread::get_id());
    }

public:
    explicit TMemoryTextLog(TMemoryOutput& output)
        : m_Output(output)
    {
    }

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

    ~TMemoryTextLog() override
    {
        Finalize();
    }
};

// A user's own line layout, "LEVEL|message"
class TPipeFormatter final : public ASWLog::IASWLogFormatter
{
public:
    std::string Format(const ASWLog::TASWLogRecord& record, const ASWLog::TASWLogConfig& /*config*/) const override
    {
        return std::string(ASWLog::Level_ToString(record.LogLevel)).append("|").append(record.Message);
    }
};

// A formatter that formats raw entries too, as "raw|message", and the others as "line|message"
class TRawFormattingFormatter final : public ASWLog::IASWLogFormatter
{
public:
    std::string Format(const ASWLog::TASWLogRecord& record, const ASWLog::TASWLogConfig& /*config*/) const override
    {
        return std::string(record.Raw ? "raw|" : "line|").append(record.Message);
    }

    bool FormatsRawEntries() const noexcept override
    {
        return true;
    }
};

// A faulty user formatter: throws for every line (IASWLogFormatter::Format() may throw; the line is then dropped)
class TThrowingFormatter final : public ASWLog::IASWLogFormatter
{
public:
    std::string Format(const ASWLog::TASWLogRecord& /*record*/, const ASWLog::TASWLogConfig& /*config*/) const override
    {
        throw std::runtime_error("format failed");
    }
};

// For lines "[INFO]: <thread index> <entry>\n" from 'threadCount' threads that each logged their entries 0, 1, 2, ...:
// counts the lines that don't parse, name an unknown thread, or aren't that thread's next entry (out of order,
// repeated, or after a missing one)
int CountOutOfOrderEntries(const std::vector<std::string>& lines, int threadCount)
{
    constexpr std::string_view Prefix = "[INFO]: ";
    std::vector<int> nextEntry(threadCount, 0);
    int outOfOrder = 0;
    for (const std::string_view line : lines)
    {
        const char* const end = line.data() + line.size();
        int threadIndex = -1;
        int entry = -1;
        bool isParsed = line.starts_with(Prefix);
        if (isParsed)
        {
            const auto threadResult = std::from_chars(line.data() + Prefix.size(), end, threadIndex);
            isParsed = threadResult.ec == std::errc() && threadResult.ptr != end && *threadResult.ptr == ' ';
            if (isParsed)
            {
                const auto entryResult = std::from_chars(threadResult.ptr + 1, end, entry);
                isParsed = entryResult.ec == std::errc() && std::string_view(entryResult.ptr, end) == "\n";
            }
        }

        if (!isParsed || threadIndex < 0 || threadIndex >= threadCount || entry != nextEntry[threadIndex])
        {
            ++outOfOrder;
            continue;
        }

        ++nextEntry[threadIndex];
    }

    return outOfOrder;
}

ASWLog::TASWLogConfig MakeQuietConfig(); // See below

// MakeQuietConfig() with asynchronous writing on
ASWLog::TASWLogConfig MakeAsyncConfig()
{
    auto config = MakeQuietConfig();
    config.Async.Enabled = true;
    return config;
}

// A config with no startup or shutdown lines, and only the level in each line, e.g. "[INFO]: message\n"
ASWLog::TASWLogConfig MakeQuietConfig()
{
    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = "textlogbase.log";
    config.InitialMinimumLevel = ASWLog::Level::Info;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = true;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.File.OpenRetryCount = 1;
    config.Shutdown.WriteLine = false;
    config.Startup.WriteTimeInfo = false;
    config.Startup.WriteOSInfo = false;
    config.Startup.WriteDriveInfo = false;
    config.Startup.WriteSystemMemoryInfo = false;
    config.Startup.WriteApplicationInfo = false;
    config.Startup.WriteMemoryUsage = false;
    return config;
}

// Makes an asynchronous logger's worker pause for AbsenceWait after it writes "entry_1", outside the lock and before
// it writes the next queued entry (OnLogEntry runs there). A call that should wait for the queued entries but doesn't
// then gets the lock during the pause and acts too early, which the test can see.
void PauseAfterFirstEntry(ASWLog::TASWLogConfig& config)
{
    config.OnLogEntryMinimumLevel = ASWLog::Level::Info;
    config.OnLogEntry = [](const ASWLog::TASWLogRecord& record, std::string_view /*line*/) {
            if (record.Message == "entry_1")
                std::this_thread::sleep_for(AbsenceWait);
        };
}

std::string ReadFileText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};

    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
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

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_TextLogBase
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_TextLogBase::TTest_ASWLog_TextLogBase()
    : inherited("ASWLog_TextLogBase_Tests")
{
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Async_BacktraceIsTakenWhenTheTriggerIsLogged, "Async_BacktraceIsTakenWhenTheTriggerIsLogged");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Async_BlockWaitsForRoomInTheQueue, "Async_BlockWaitsForRoomInTheQueue");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Async_CallReturnsBeforeTheEntryIsWritten, "Async_CallReturnsBeforeTheEntryIsWritten");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Async_DestructorWritesQueuedEntriesBeforeTheShutdownLine, "Async_DestructorWritesQueuedEntriesBeforeTheShutdownLine");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Async_DropNewestReportsAndMarksDroppedEntries, "Async_DropNewestReportsAndMarksDroppedEntries");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Async_EntriesKeepTheirOrderPerThread, "Async_EntriesKeepTheirOrderPerThread");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Async_EntryAtWaitAtLevelIsNeverDropped, "Async_EntryAtWaitAtLevelIsNeverDropped");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Async_EntryAtWaitAtLevelReturnsOnceWrittenAndFlushed, "Async_EntryAtWaitAtLevelReturnsOnceWrittenAndFlushed");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Async_FlushWaitsForQueuedEntries, "Async_FlushWaitsForQueuedEntries");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Async_LoggingFromOnLogEntryDoesNotWait, "Async_LoggingFromOnLogEntryDoesNotWait");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Async_OnLogEntryRunsOnTheWorkerWithItsOwnCopy, "Async_OnLogEntryRunsOnTheWorkerWithItsOwnCopy");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Async_ReconfigureToSyncWritesQueuedEntriesFirst, "Async_ReconfigureToSyncWritesQueuedEntriesFirst");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Async_SwitchingOnAndOffKeepsEachThreadsOrder, "Async_SwitchingOnAndOffKeepsEachThreadsOrder");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Backtrace_IsFormattedAndWrittenBeforeTheTrigger, "Backtrace_IsFormattedAndWrittenBeforeTheTrigger");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Finalize_WritesShutdownLineFromDestructor, "Finalize_WritesShutdownLineFromDestructor");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Flush_CallsHookAndReturnsItsResult, "Flush_CallsHookAndReturnsItsResult");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Flush_ThrowingHookDoesNotEscape, "Flush_ThrowingHookDoesNotEscape");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Formatter_FormatsEveryFileLine, "Formatter_FormatsEveryFileLine");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Initialize_ThrowingFormatterStillInitializes, "Initialize_ThrowingFormatterStillInitializes");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Initialize_WritesStartupLinesThenCallsAfterEntry, "Initialize_WritesStartupLinesThenCallsAfterEntry");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Log_AtLevelOffIsNeverWritten, "Log_AtLevelOffIsNeverWritten");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Log_DroppedWhenNotReadyOrNotPrepared, "Log_DroppedWhenNotReadyOrNotPrepared");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Log_FormatsFiltersAndCallsAfterEntry, "Log_FormatsFiltersAndCallsAfterEntry");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Log_MinimumLevelOffAllowsOnlyForcedEntries, "Log_MinimumLevelOffAllowsOnlyForcedEntries");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Log_ThrowingWriteDoesNotEscape, "Log_ThrowingWriteDoesNotEscape");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_LogRaw_FormatterCanFormatRawEntries, "LogRaw_FormatterCanFormatRawEntries");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_LogRaw_WritesMessageAsIs, "LogRaw_WritesMessageAsIs");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_OnLogEntry_CallbackCanReconfigureTheLogger, "OnLogEntry_CallbackCanReconfigureTheLogger");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Reconfigure_AppliesNewConfigButKeepsLevel, "Reconfigure_AppliesNewConfigButKeepsLevel");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Reconfigure_IsSafeWhileOtherThreadsLog, "Reconfigure_IsSafeWhileOtherThreadsLog");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_ReportErrorUnlocked_ReportsAfterTheLockIsReleased, "ReportErrorUnlocked_ReportsAfterTheLockIsReleased");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_SetEnabled_FalseWritesNothing, "SetEnabled_FalseWritesNothing");
}
//---------------------------------------------------------------------------
TTest_ASWLog_TextLogBase::~TTest_ASWLog_TextLogBase()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::SetUp_Group()
{
    Log("Setting up temp group folder: " + GroupBaseTempDir.string());
    std::filesystem::create_directories(GroupBaseTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::SetUp_Test(ITestCase& testCase)
{
    Log("  Setting up temp folder for " + testCase.GetName() + ": " + TestTempDir.string());
    std::filesystem::create_directories(TestTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::TearDown_Group()
{
    Log("Cleaning up temp group folder:" + GroupBaseTempDir.string());
    std::filesystem::remove_all(GroupBaseTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::TearDown_Test(ITestCase& testCase)
{
    Log("  Cleaning up temp folder for " + testCase.GetName() + ": " + TestTempDir.string());
    std::filesystem::remove_all(TestTempDir);
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Async_BlockWaitsForRoomInTheQueue()
{
    // Arrange: a queue of one entry
    TMemoryOutput output;
    TWriteGate gate;
    auto config = MakeAsyncConfig();
    config.Async.QueueCapacity = 1;
    config.Async.OverflowPolicy = ASWLog::AsyncOverflowPolicy::Block;

    TMemoryTextLog log(output);
    log.Gate = &gate;
    const bool initialized = log.Initialize(config);

    // Act: the worker holds entry_1, entry_2 fills the queue, so entry_3 must wait
    log.LogInfo("entry_1");
    const bool isWriting = gate.WaitForWrite();
    log.LogInfo("entry_2");
    std::atomic<bool> hasReturned{ false };
    std::thread caller([&log, &hasReturned] {
        log.LogInfo("entry_3");
        hasReturned.store(true);
            });
    std::this_thread::sleep_for(AbsenceWait);
    const bool returnedWhileFull = hasReturned.load();
    gate.Open();
    caller.join();
    log.Flush();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(isWriting, "The worker should be writing the first entry");
    CheckFalse(returnedWhileFull, "With Block, a call should wait while the queue is full");
    const std::vector<std::string> expected{ "[INFO]: entry_1\n", "[INFO]: entry_2\n", "[INFO]: entry_3\n" };
    CheckTrue(output.Lines == expected, "Every entry should be written, in order");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Async_BacktraceIsTakenWhenTheTriggerIsLogged()
{
    // Arrange: the error doesn't wait, so the test can log on while the worker is held
    TMemoryOutput output;
    TWriteGate gate;
    auto config = MakeAsyncConfig();
    config.Async.WaitAtLevel = ASWLog::Level::Off;
    config.Backtrace.Capacity = 5;
    config.Backtrace.LowestLevel = ASWLog::Level::Debug;
    TMemoryTextLog log(output);
    log.Gate = &gate;
    const bool initialized = log.Initialize(config);

    // Act: debug_2 is logged after the error, while the worker hasn't written it yet
    log.LogInfo("info_1");
    const bool isWriting = gate.WaitForWrite();
    log.LogDebug("debug_1");
    log.LogError("error_1");
    log.LogDebug("debug_2");
    gate.Open();
    log.Flush();
    const auto linesAfterError = output.Lines;
    log.DumpBacktrace();
    log.Flush();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(isWriting, "The worker should be writing the first entry");
    const std::vector<std::string> expected{ "[INFO]: info_1\n", "[INFO]: Backtrace: the last 1 entry below the minimum level\n",
                                             "[DEBUG]: debug_1\n", "[INFO]: Backtrace end\n", "[ERROR]: error_1\n" };
    CheckTrue(linesAfterError == expected, "The error's backtrace should hold only what was logged before it, queued in order");
    CheckEquals(8, output.Lines.size(), "The entry logged after the error should stay kept, for the next backtrace");
    if (output.Lines.size() == 8)
        CheckEquals(std::string("[DEBUG]: debug_2\n"), output.Lines[6], "DumpBacktrace() should write it");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Async_CallReturnsBeforeTheEntryIsWritten()
{
    // Arrange
    TMemoryOutput output;
    TWriteGate gate;
    TMemoryTextLog log(output);
    log.Gate = &gate;
    const bool initialized = log.Initialize(MakeAsyncConfig());

    // Act: the gate holds the worker's write, not the call
    log.LogInfo("queued_entry");
    const bool isWriting = gate.WaitForWrite();
    const auto lineCountWhileHeld = output.Lines.size();
    gate.Open();
    const bool flushed = log.Flush();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(isWriting, "The worker should be writing the entry");
    CheckEquals(0, lineCountWhileHeld, "The call should return before the entry is written");
    CheckTrue(flushed, "Flush should succeed");
    CheckEquals(1, output.Lines.size(), "Flush should wait for the queued entry");
    if (output.Lines.size() == 1)
    {
        CheckEquals(std::string("[INFO]: queued_entry\n"), output.Lines[0], "The entry should be formatted as usual");
        CheckTrue(output.WriteThreadIds[0] != std::this_thread::get_id(), "The logger's own thread should write it");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Async_DestructorWritesQueuedEntriesBeforeTheShutdownLine()
{
    // Arrange
    TMemoryOutput output;
    auto config = MakeAsyncConfig();
    config.Shutdown.WriteLine = true;
    constexpr int EntryCount = 200;

    // Act
    bool initialized = false;
    {
        TMemoryTextLog log(output);
        initialized = log.Initialize(config);
        for (int entry = 0; entry < EntryCount; ++entry)
            log.LogInfo(std::format("entry_{}", entry));
    }

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(EntryCount + 1, output.Lines.size(), "Every queued entry and the shutdown line should be written");
    if (output.Lines.size() == EntryCount + 1)
    {
        CheckEquals(std::string("[INFO]: entry_0\n"), output.Lines.front(), "The queued entries should come first");
        CheckEquals(std::format("[INFO]: entry_{}\n", EntryCount - 1), output.Lines[EntryCount - 1], "The queued entries should keep their order");
        CheckStartsWith(output.Lines.back(), "[INFO]: Logger shutdown: ", "The shutdown line should come last");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Async_DropNewestReportsAndMarksDroppedEntries()
{
    // Arrange: a queue of two entries
    TMemoryOutput output;
    TWriteGate gate;
    auto config = MakeAsyncConfig();
    config.Async.QueueCapacity = 2;
    config.Async.OverflowPolicy = ASWLog::AsyncOverflowPolicy::DropNewest;

    std::mutex errorsMutex;
    std::vector<ASWLog::TASWLogError> errors;
    config.OnError = [&errorsMutex, &errors](const ASWLog::TASWLogError& error) {
            std::lock_guard<std::mutex> lock(errorsMutex);
            errors.push_back(error);
        };

    TMemoryTextLog log(output);
    log.Gate = &gate;
    const bool initialized = log.Initialize(config);

    // Act: the worker holds entry_1, entry_2 and entry_3 fill the queue, entry_4 to entry_6 are dropped
    log.LogInfo("entry_1");
    const bool isWriting = gate.WaitForWrite();
    for (int entry = 2; entry <= 6; ++entry)
        log.LogInfo(std::format("entry_{}", entry));

    gate.Open();
    log.Flush();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(isWriting, "The worker should be writing the first entry");
    const std::vector<std::string> expected{ "[INFO]: entry_1\n", "[INFO]: entry_2\n", "[INFO]: entry_3\n",
                                             "[WARN]: Dropped 3 entries: the asynchronous queue was full\n" };
    CheckTrue(output.Lines == expected, "The entries that fit should be written, then a line where the dropped ones were");
    std::lock_guard<std::mutex> lock(errorsMutex);
    CheckEquals(1, errors.size(), "The drop should be reported once");
    if (errors.size() == 1)
    {
        CheckEquals(ASWLog::ErrorKind::EntriesDropped, errors[0].Kind, "The report should be EntriesDropped");
        CheckContains(errors[0].Message, "Dropped 3 entries", "The report should say how many were dropped");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Async_EntriesKeepTheirOrderPerThread()
{
    // Arrange
    TMemoryOutput output;
    TMemoryTextLog log(output);
    const bool initialized = log.Initialize(MakeAsyncConfig());
    constexpr int ThreadCount = 4;
    constexpr int EntriesPerThread = 500;

    // Act
    std::vector<std::thread> threads;
    for (int threadIndex = 0; threadIndex < ThreadCount; ++threadIndex)
    {
        threads.emplace_back([&log, threadIndex] {
                for (int entry = 0; entry < EntriesPerThread; ++entry)
                    log.LogInfo(std::format("{} {}", threadIndex, entry));
            });
    }

    for (auto& thread : threads)
        thread.join();

    log.Flush();

    // Assert: each thread's entries are in the order it logged them
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(ThreadCount * EntriesPerThread, output.Lines.size(), "Every entry should be written once");
    CheckEquals(0, CountOutOfOrderEntries(output.Lines, ThreadCount), "Each thread's entries should keep their order");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Async_EntryAtWaitAtLevelIsNeverDropped()
{
    // Arrange: a queue of one entry that drops entries below the wait level
    TMemoryOutput output;
    TWriteGate gate;
    auto config = MakeAsyncConfig();
    config.Async.QueueCapacity = 1;
    config.Async.OverflowPolicy = ASWLog::AsyncOverflowPolicy::DropNewest;
    config.OnError = [](const ASWLog::TASWLogError& /*error*/) {
        };

    TMemoryTextLog log(output);
    log.Gate = &gate;
    const bool initialized = log.Initialize(config);

    // Act: the worker holds entry_1, entry_2 fills the queue, entry_3 is dropped, the Error entry waits for room
    log.LogInfo("entry_1");
    const bool isWriting = gate.WaitForWrite();
    log.LogInfo("entry_2");
    log.LogInfo("entry_3");
    std::atomic<bool> hasReturned{ false };
    std::thread caller([&log, &hasReturned] {
        log.LogError("error_entry");
        hasReturned.store(true);
            });
    std::this_thread::sleep_for(AbsenceWait);
    const bool returnedWhileFull = hasReturned.load();
    gate.Open();
    caller.join();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(isWriting, "The worker should be writing the first entry");
    CheckFalse(returnedWhileFull, "The Error entry should wait for room rather than be dropped");
    const std::vector<std::string> expected{ "[INFO]: entry_1\n", "[INFO]: entry_2\n",
                                             "[WARN]: Dropped 1 entry: the asynchronous queue was full\n",
                                             "[ERROR]: error_entry\n" };
    CheckTrue(output.Lines == expected, "Only the Info entry should be dropped, and the Error entry written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Async_EntryAtWaitAtLevelReturnsOnceWrittenAndFlushed()
{
    // Arrange
    TMemoryOutput output;
    TWriteGate gate;
    TMemoryTextLog log(output);
    log.Gate = &gate;
    const bool initialized = log.Initialize(MakeAsyncConfig());

    // Act: the worker holds the Info entry, so the Error entry can't be written yet
    log.LogInfo("info_entry");
    const bool isWriting = gate.WaitForWrite();
    std::atomic<bool> hasReturned{ false };
    std::thread caller([&log, &hasReturned] {
        log.LogError("error_entry");
        hasReturned.store(true);
            });
    std::this_thread::sleep_for(AbsenceWait);
    const bool returnedBeforeWrite = hasReturned.load();
    gate.Open();
    caller.join();

    // Assert: no Flush() call; the Error call itself waited for the write and the flush
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(isWriting, "The worker should be writing the Info entry");
    CheckFalse(returnedBeforeWrite, "The Error call should wait until its entry is written");
    const std::vector<std::string> expected{ "[INFO]: info_entry\n", "[ERROR]: error_entry\n" };
    CheckTrue(output.Lines == expected, "Both entries should be written when the Error call returns");
    CheckGreaterThanOrEqual(output.FlushCount, 1, "The Error entry should be flushed before its call returns");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Async_FlushWaitsForQueuedEntries()
{
    // Arrange: after writing entry_1, the worker pauses outside the lock (see PauseAfterFirstEntry())
    TMemoryOutput output;
    TWriteGate gate;
    auto config = MakeAsyncConfig();
    PauseAfterFirstEntry(config);
    TMemoryTextLog log(output);
    log.Gate = &gate;
    const bool initialized = log.Initialize(config);

    // Act
    log.LogInfo("entry_1");
    const bool isWriting = gate.WaitForWrite();
    log.LogInfo("entry_2");
    std::atomic<bool> hasReturned{ false };
    std::thread flusher([&log, &hasReturned] {
        log.Flush();
        hasReturned.store(true);
            });
    std::this_thread::sleep_for(AbsenceWait);
    const bool returnedBeforeWrite = hasReturned.load();
    gate.Open();
    flusher.join();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(isWriting, "The worker should be writing the first entry");
    CheckFalse(returnedBeforeWrite, "Flush should wait for the queued entries");
    const std::vector<std::string> expected{ "[INFO]: entry_1\n", "[INFO]: entry_2\n" };
    CheckTrue(output.Lines == expected, "Both entries should be written when Flush returns");
    CheckEquals(2, output.LineCountAtLastFlush, "Flush should flush after both queued entries are written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Async_LoggingFromOnLogEntryDoesNotWait()
{
    // Arrange: OnLogEntry, on the worker, logs an Error entry (which a caller waits for) into a full queue
    TMemoryOutput output;
    auto config = MakeAsyncConfig();
    config.Async.QueueCapacity = 1;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Info;

    TMemoryTextLog log(output);
    std::atomic<bool> hasLoggedFromCallback{ false };
    config.OnLogEntry = [&log, &hasLoggedFromCallback](const ASWLog::TASWLogRecord& record, std::string_view /*line*/) {
            if (record.LogLevel == ASWLog::Level::Info)
            {
                log.LogError("from_callback");
                log.LogError("from_callback_again");
                hasLoggedFromCallback.store(true);
            }
        };
    const bool initialized = log.Initialize(config);

    // Act
    log.LogInfo("trigger");
    const bool callbackReturned = WaitUntil([&hasLoggedFromCallback] {
            return hasLoggedFromCallback.load();
        });
    log.Flush();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(callbackReturned, "Logging from OnLogEntry on the worker should not wait for the worker");
    const std::vector<std::string> expected{ "[INFO]: trigger\n", "[ERROR]: from_callback\n", "[ERROR]: from_callback_again\n" };
    CheckTrue(output.Lines == expected, "The entries logged from the callback should be written after it");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Async_OnLogEntryRunsOnTheWorkerWithItsOwnCopy()
{
    // Arrange
    TMemoryOutput output;
    auto config = MakeAsyncConfig();
    config.OnLogEntryMinimumLevel = ASWLog::Level::Info;

    std::atomic<bool> isCalled{ false };
    std::string seenMessage;
    std::string seenLine;
    std::thread::id seenThreadId;
    std::size_t lineCountSeen = 0;
    TMemoryTextLog log(output);
    config.OnLogEntry = [&](const ASWLog::TASWLogRecord& record, std::string_view line) {
            seenMessage = record.Message;
            seenLine = line;
            seenThreadId = std::this_thread::get_id();
            lineCountSeen = output.Lines.size(); // The worker wrote them
            isCalled.store(true);
        };
    const bool initialized = log.Initialize(config);

    // Act: the message is overwritten as soon as the call returns
    {
        std::string message = "temporary_message";
        log.LogInfo(message);
        message.assign(message.size(), 'x');
    }
    const bool wasCalled = WaitUntil([&isCalled] {
            return isCalled.load();
        });

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(wasCalled, "OnLogEntry should be called");
    if (wasCalled)
    {
        CheckEquals(std::string("temporary_message"), seenMessage, "The record should hold its own copy of the message");
        CheckEquals(std::string("[INFO]: temporary_message\n"), seenLine, "The callback should get the line as written");
        CheckTrue(seenThreadId != std::this_thread::get_id(), "OnLogEntry should run on the logger's own thread");
        CheckEquals(1, lineCountSeen, "OnLogEntry should run after the entry was written");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Async_ReconfigureToSyncWritesQueuedEntriesFirst()
{
    // Arrange: after writing entry_1, the worker pauses outside the lock (see PauseAfterFirstEntry())
    TMemoryOutput output;
    TWriteGate gate;
    auto config = MakeAsyncConfig();
    PauseAfterFirstEntry(config);
    TMemoryTextLog log(output);
    log.Gate = &gate;
    const bool initialized = log.Initialize(config);

    // Act: switch to synchronous writing while entries are queued
    log.LogInfo("entry_1");
    const bool isWriting = gate.WaitForWrite();
    log.LogInfo("entry_2");
    config.Async.Enabled = false;
    bool reconfigured = false;
    std::thread reconfigurer([&log, &config, &reconfigured] {
        reconfigured = log.Reconfigure(config);
            });
    std::this_thread::sleep_for(AbsenceWait);
    gate.Open();
    reconfigurer.join();
    log.LogInfo("sync_entry");

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(isWriting, "The worker should be writing the first entry");
    CheckTrue(reconfigured, "Reconfigure should succeed");
    CheckEquals(2, output.LineCountAtLastReconfigure, "Reconfigure should apply the config after the queued entries are written");
    const std::vector<std::string> expected{ "[INFO]: entry_1\n", "[INFO]: entry_2\n", "[INFO]: sync_entry\n" };
    CheckTrue(output.Lines == expected, "The queued entries should be written before the synchronous one");
    if (output.WriteThreadIds.size() == 3)
        CheckTrue(output.WriteThreadIds[2] == std::this_thread::get_id(), "After the switch, the caller should write its entry");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Async_SwitchingOnAndOffKeepsEachThreadsOrder()
{
    // Arrange
    TMemoryOutput output;
    auto config = MakeAsyncConfig();
    TMemoryTextLog log(output);
    const bool initialized = log.Initialize(config);
    constexpr int ThreadCount = 4;
    constexpr int MaxEntriesPerThread = 5000;

    // Act: while the threads log, switch between asynchronous and synchronous writing
    std::atomic<bool> stopLogging{ false };
    std::vector<std::thread> threads;
    for (int threadIndex = 0; threadIndex < ThreadCount; ++threadIndex)
    {
        threads.emplace_back([&log, &stopLogging, threadIndex] {
                for (int entry = 0; entry < MaxEntriesPerThread && !stopLogging.load(); ++entry)
                    log.LogInfo(std::format("{} {}", threadIndex, entry));
            });
    }

    int failedReconfigures = 0;
    for (int change = 0; change < 40; ++change)
    {
        config.Async.Enabled = change % 2 != 0;
        if (!log.Reconfigure(config))
            ++failedReconfigures;
    }

    stopLogging.store(true);
    for (auto& thread : threads)
        thread.join();

    log.Flush();

    // Assert: each thread's entries are in the order it logged them, none missing
    Log(std::format("  Wrote {} entries while switching", output.Lines.size()));
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(0, failedReconfigures, "Every Reconfigure should succeed");
    CheckEquals(0, CountOutOfOrderEntries(output.Lines, ThreadCount),
        "Each thread's entries should keep their order, with none missing, across the switches");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Backtrace_IsFormattedAndWrittenBeforeTheTrigger()
{
    // Arrange
    TMemoryOutput output;
    auto config = MakeQuietConfig();
    config.Backtrace.Capacity = 5;
    config.Backtrace.LowestLevel = ASWLog::Level::Debug;
    TMemoryTextLog log(output);
    const bool initialized = log.Initialize(config);

    // Act
    log.LogDebug("debug_1");
    log.LogRaw(ASWLog::Level::Debug, "raw_debug|");
    log.LogInfo("info_1");
    log.LogError("error_1");

    // Assert: written through the formatter, a raw entry as is, past the logger's own minimum level check
    CheckTrue(initialized, "Initialize should succeed");
    const std::vector<std::string> expected{ "[INFO]: info_1\n", "[INFO]: Backtrace: the last 2 entries below the minimum level\n",
                                             "[DEBUG]: debug_1\n", "raw_debug|", "[INFO]: Backtrace end\n", "[ERROR]: error_1\n" };
    CheckTrue(output.Lines == expected, "The kept entries should be formatted and written just before the error");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Finalize_WritesShutdownLineFromDestructor()
{
    // Arrange
    TMemoryOutput output;
    auto config = MakeQuietConfig();
    config.Shutdown.WriteLine = true;
    config.Shutdown.Banner = "goodbye";

    // Act
    {
        TMemoryTextLog log(output);
        CheckTrue(log.Initialize(config), "Initialize should succeed");
    }

    // Assert
    CheckEquals(1, output.Lines.size(), "The destructor should write one shutdown line");
    if (output.Lines.size() == 1)
    {
        const auto& line = output.Lines[0];
        CheckStartsWith(line, "[INFO]: Logger shutdown: ", "The shutdown line should be formatted: " + line);
        CheckEndsWith(line, ", goodbye\n", "The shutdown line should end with the shutdown banner");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Flush_CallsHookAndReturnsItsResult()
{
    // Arrange: called through the interface, as generic code would
    TMemoryOutput output;
    TMemoryTextLog log(output);
    ASWLog::IASWLog& logger = log;
    CheckTrue(logger.Initialize(MakeQuietConfig()), "Initialize should succeed");

    // Act
    const bool flushed = logger.Flush();

    log.FlushResult = false;
    const bool failedFlush = logger.Flush();

    log.FlushResult = true;
    logger.SetEnabled(false);
    const bool flushedWhileDisabled = logger.Flush();

    // Assert
    CheckTrue(flushed, "Flush() should return true when FlushUnlocked() succeeds");
    CheckTrue(!failedFlush, "Flush() should return false when FlushUnlocked() fails");
    CheckTrue(flushedWhileDisabled, "Flush() should still flush while the logger is disabled");
    CheckEquals(3, output.FlushCount, "Each Flush() should call FlushUnlocked() once");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Flush_ThrowingHookDoesNotEscape()
{
    // Arrange
    TMemoryOutput output;
    TMemoryTextLog log(output);
    CheckTrue(log.Initialize(MakeQuietConfig()), "Initialize should succeed");
    log.ThrowsOnFlush = true;
    static_assert(noexcept(log.Flush()), "Flush() must be noexcept");

    // Act
    const bool flushed = log.Flush();
    log.ThrowsOnFlush = false;
    log.LogInfo("after_failed_flush");

    // Assert
    CheckTrue(!flushed, "A throwing FlushUnlocked() should make Flush() return false, not throw");
    CheckEquals(1, output.Lines.size(), "Logging should continue after a failed flush (the lock is released)");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Formatter_FormatsEveryFileLine()
{
    // Arrange
    const auto logPath = TestTempDir / "textlogbase.log";
    std::vector<std::string> callbackLines;
    auto config = MakeQuietConfig();
    config.Line.Formatter = std::make_shared<TPipeFormatter>();
    config.Startup.Banner = "start";
    config.Shutdown.WriteLine = true;
    config.Shutdown.Banner = "bye";
    config.OnLogEntryMinimumLevel = ASWLog::Level::Info;
    config.OnLogEntry = [&callbackLines](const ASWLog::TASWLogRecord& /*record*/, std::string_view line) {
            callbackLines.emplace_back(line);
        };

    // Act
    {
        ASWLog::TASWFileLog log;
        CheckTrue(log.Initialize(config), "Initialize should succeed");
        log.LogInfo("hello");
        log.LogWarn("careful");
        log.LogRaw(ASWLog::Level::Info, "raw text\n");
    }

    const auto contents = ReadFileText(logPath);

    // Assert
    const std::string expectedStart = "INFO|start\nINFO|hello\nWARN|careful\nraw text\nINFO|Logger shutdown: ";
    CheckStartsWith(contents, expectedStart,
        "Every formatted line, including the startup and shutdown lines, should use the formatter; raw text should not: " + contents);
    CheckEndsWith(contents, ", bye\n", "The shutdown line should end with the shutdown banner");
    CheckEquals(3, callbackLines.size(), "OnLogEntry should get the three logged entries");
    if (callbackLines.size() == 3)
        CheckEquals(std::string("INFO|hello\n"), callbackLines[0], "OnLogEntry should get the line in the formatter's layout");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Initialize_ThrowingFormatterStillInitializes()
{
    // Arrange: a startup line goes through a formatter that throws
    TMemoryOutput output;
    auto config = MakeQuietConfig();
    config.Startup.Banner = "banner";
    config.Line.Formatter = std::make_shared<TThrowingFormatter>();
    std::vector<std::string> reportedMessages;
    config.ErrorReportInterval = std::chrono::milliseconds(0);
    config.OnError = [&reportedMessages](const ASWLog::TASWLogError& error) {
            reportedMessages.push_back(error.Message);
        };

    TMemoryTextLog log(output);
    bool initialized = false;

    // Act
    CheckNoThrow([&] {
            initialized = log.Initialize(config);
            log.LogInfo("formatted"); // Dropped, since its line can't be formatted
            log.LogRaw(ASWLog::Level::Info, "raw"); // Written: raw entries don't use the formatter
        }, "A throwing formatter should not throw out of Initialize() or the Log* methods");

    // Assert
    CheckTrue(initialized, "Initialize() should succeed: the startup lines are best effort once the output is open");
    CheckTrue(log.IsOpen(), "The logger should be open");
    CheckTrue(output.Lines == std::vector<std::string>{ "raw" }, "Only the raw entry should be written");
    CheckEquals(2, output.AfterEntryCount, "AfterEntryUnlocked should still run after the startup lines, and after the raw entry");
    CheckTrue(reportedMessages == std::vector<std::string>{ "Skipped the startup lines: format failed", "Dropped an entry: format failed" },
        "The skipped startup lines and the dropped entry should be reported");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Initialize_WritesStartupLinesThenCallsAfterEntry()
{
    // Arrange
    TMemoryOutput output;
    TMemoryTextLog log(output);
    auto config = MakeQuietConfig();
    config.Startup.Banner = "starting";
    config.Startup.WriteTimeInfo = true;

    // Act
    const bool initialized = log.Initialize(config);
    const bool initializedAgain = log.Initialize(config);

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckFalse(initializedAgain, "Initialize should fail while the logger is initialized");
    CheckTrue(log.IsOpen(), "The logger should be open after Initialize");
    CheckEquals(2, output.Lines.size(), "Initialize should write the banner and the time line");
    if (output.Lines.size() == 2)
    {
        CheckEquals(std::string("[INFO]: starting\n"), output.Lines[0], "The banner should come first");
        CheckStartsWith(output.Lines[1], "[INFO]: Time: ", "The time line should follow: " + output.Lines[1]);
    }
    CheckEquals(1, output.AfterEntryCount, "Initialize should call AfterEntryUnlocked once, after the startup lines");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Log_AtLevelOffIsNeverWritten()
{
    // Arrange
    TMemoryOutput output;
    int callbackCount = 0;
    auto config = MakeQuietConfig();
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Trace;
    config.OnLogEntry = [&callbackCount](const ASWLog::TASWLogRecord&, std::string_view) {
            ++callbackCount;
        };

    TMemoryTextLog log(output);
    CheckTrue(log.Initialize(config), "Initialize should succeed");
    const int afterEntryCountAtStart = output.AfterEntryCount;

    // Act
    log.Log(ASWLog::Level::Off, "off");
    log.LogRaw(ASWLog::Level::Off, "off_raw\n");
    log.LogForce(ASWLog::Level::Off, "off_forced");
    log.LogForceRaw(ASWLog::Level::Off, "off_forced_raw\n");
    log.LogInfoFmt("{}", "off_fmt_check"); // A normal entry after them, to show the logger still works

    // Assert
    CheckTrue(output.Lines == std::vector<std::string>{ "[INFO]: off_fmt_check\n" },
        "A message logged at Off should never be written, even when forced");
    CheckEquals(1, callbackCount, "OnLogEntry should only fire for the written entry");
    CheckEquals(afterEntryCountAtStart + 1, output.AfterEntryCount,
        "An entry at Off should be dropped before reaching the logger's output");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Log_DroppedWhenNotReadyOrNotPrepared()
{
    // Arrange
    TMemoryOutput output;
    int callbackCount = 0;
    auto config = MakeQuietConfig();
    config.OnLogEntryMinimumLevel = ASWLog::Level::Info;
    config.OnLogEntry = [&callbackCount](const ASWLog::TASWLogRecord&, std::string_view) {
            ++callbackCount;
        };

    TMemoryTextLog log(output);
    log.LogInfo("before_initialize");
    CheckTrue(log.Initialize(config), "Initialize should succeed");
    const int afterEntryCountAtStart = output.AfterEntryCount;

    // Act
    log.IsReady = false;
    log.LogInfo("not_ready");
    log.LogForce(ASWLog::Level::Info, "not_ready_forced");
    log.IsReady = true;

    log.AcceptsWrites = false;
    log.LogInfo("not_prepared");
    log.AcceptsWrites = true;

    log.Close();
    log.LogInfo("after_close");

    // Assert
    CheckEmpty(output.Lines, "No entry should be written");
    CheckEquals(0, callbackCount, "OnLogEntry should not be called for a dropped entry");
    CheckEquals(afterEntryCountAtStart + 1, output.AfterEntryCount,
        "AfterEntryUnlocked should be skipped when EnsureReadyUnlocked fails, but called when only PrepareWriteUnlocked drops the line");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Log_FormatsFiltersAndCallsAfterEntry()
{
    // Arrange
    TMemoryOutput output;
    std::vector<std::string> callbackLines;
    auto config = MakeQuietConfig();
    config.Line.Ending = ASWLog::LineEnding::CRLF;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Trace;
    config.OnLogEntry = [&callbackLines](const ASWLog::TASWLogRecord&, std::string_view line) {
            callbackLines.emplace_back(line);
        };

    TMemoryTextLog log(output);
    CheckTrue(log.Initialize(config), "Initialize should succeed");
    const int afterEntryCountAtStart = output.AfterEntryCount;

    // Act
    log.LogDebug("filtered");
    log.LogInfo("info_entry");
    log.LogForce(ASWLog::Level::Debug, "forced_entry");

    // Assert
    const std::vector<std::string> expected = { "[INFO]: info_entry\r\n", "[DEBUG]: forced_entry\r\n" };
    CheckTrue(output.Lines == expected, "Only the Info entry and the forced Debug entry should be written, formatted");
    CheckTrue(callbackLines == expected, "OnLogEntry should get each written line");
    CheckTrue(output.EndsLine == std::vector<bool>{ true, true }, "A formatted entry should end its line");
    CheckEquals(afterEntryCountAtStart + 2, output.AfterEntryCount, "AfterEntryUnlocked should be called once per entry that reached the logger");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Log_MinimumLevelOffAllowsOnlyForcedEntries()
{
    // Arrange
    TMemoryOutput output;
    int callbackCount = 0;
    auto config = MakeQuietConfig();
    config.InitialMinimumLevel = ASWLog::Level::Off;
    config.Startup.Banner = "startup banner";
    config.Startup.WriteTimeInfo = true;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Off;
    config.OnLogEntry = [&callbackCount](const ASWLog::TASWLogRecord&, std::string_view) {
            ++callbackCount;
        };

    TMemoryTextLog log(output);

    // Act
    const bool initialized = log.Initialize(config);
    const auto linesAfterInitialize = output.Lines.size();
    log.LogCritical("critical");
    log.LogRaw(ASWLog::Level::Critical, "critical_raw\n");
    log.LogCriticalFmt("{}", "critical_fmt");
    log.LogForce(ASWLog::Level::Info, "forced");
    log.LogForceRaw(ASWLog::Level::Trace, "forced_raw\n");

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(ASWLog::Level::Off, log.GetMinimumLevel(), "InitialMinimumLevel should seed the minimum level");
    CheckEquals(0, linesAfterInitialize,
        "With the minimum level at Off, Initialize should write no startup lines");
    CheckTrue(output.Lines == std::vector<std::string>{ "[INFO]: forced\n", "forced_raw\n" },
        "With the minimum level at Off, only forced entries should be written");
    CheckEquals(0, callbackCount, "With OnLogEntryMinimumLevel at Off, OnLogEntry should never fire");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Log_ThrowingWriteDoesNotEscape()
{
    // Arrange
    TMemoryOutput output;
    TMemoryTextLog log(output);
    std::vector<std::string> reportedMessages;
    auto config = MakeQuietConfig();
    config.ErrorReportInterval = std::chrono::milliseconds(0);
    config.OnError = [&reportedMessages](const ASWLog::TASWLogError& error) {
            reportedMessages.push_back(error.Message);
        };
    CheckTrue(log.Initialize(config), "Initialize should succeed");

    // Act
    log.ThrowsOnWrite = true;
    CheckNoThrow([&] {
            log.LogInfo("write_throws");
            log.LogForceRaw(ASWLog::Level::Error, "write_throws_raw");
        }, "An exception from WriteLineUnlocked should not reach the caller");
    log.ThrowsOnWrite = false;
    log.LogInfo("after_throw"); // Would deadlock if the throw had left the mutex locked

    // Assert
    CheckTrue(output.Lines == std::vector<std::string>{ "[INFO]: after_throw\n" }, "Logging should work after a failed write");
    CheckTrue(reportedMessages == std::vector<std::string>{ "Dropped an entry: write failed", "Dropped an entry: write failed" },
        "Each dropped entry should be reported");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_LogRaw_FormatterCanFormatRawEntries()
{
    // Arrange: a synchronous and an asynchronous logger whose formatter formats raw entries too
    TMemoryOutput syncOutput;
    TMemoryOutput asyncOutput;
    auto syncConfig = MakeQuietConfig();
    syncConfig.Line.Formatter = std::make_shared<const TRawFormattingFormatter>();
    auto asyncConfig = MakeAsyncConfig();
    asyncConfig.Line.Formatter = syncConfig.Line.Formatter;
    TMemoryTextLog syncLog(syncOutput);
    TMemoryTextLog asyncLog(asyncOutput);
    CheckTrue(syncLog.Initialize(syncConfig), "Initialize should succeed");
    CheckTrue(asyncLog.Initialize(asyncConfig), "Initialize should succeed (async)");

    const auto logEntries = [](TMemoryTextLog& log) {
            log.LogRaw(ASWLog::Level::Debug, "filtered_raw");
            log.LogRaw(ASWLog::Level::Info, "partial");
            log.LogForceRaw(ASWLog::Level::Trace, "forced\n");
            log.LogInfo("formatted");
        };

    // Act
    logEntries(syncLog);
    logEntries(asyncLog);
    asyncLog.Flush(); // Waits for the queued entries

    // Assert
    const std::vector<std::string> expected{ "raw|partial\n", "raw|forced\n\n", "line|formatted\n" };
    CheckTrue(syncOutput.Lines == expected, "Raw entries should be formatted and get the line ending");
    CheckTrue(syncOutput.EndsLine == std::vector<bool>{ true, true, true }, "A formatted raw entry should end its line");
    CheckTrue(asyncOutput.Lines == expected, "Queued raw entries should be formatted and get the line ending");
    CheckTrue(asyncOutput.EndsLine == std::vector<bool>{ true, true, true }, "A queued formatted raw entry should end its line");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_LogRaw_WritesMessageAsIs()
{
    // Arrange
    TMemoryOutput output;
    TMemoryTextLog log(output);
    CheckTrue(log.Initialize(MakeQuietConfig()), "Initialize should succeed");

    // Act
    log.LogRaw(ASWLog::Level::Debug, "filtered_raw");
    log.LogRaw(ASWLog::Level::Info, "partial");
    log.LogForceRaw(ASWLog::Level::Trace, " line\n");

    // Assert
    CheckTrue(output.Lines == std::vector<std::string>{ "partial", " line\n" }, "Raw entries should be written as is, without a format or a line ending");
    CheckTrue(output.EndsLine == std::vector<bool>{ false, false }, "A raw entry doesn't end its line");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_OnLogEntry_CallbackCanReconfigureTheLogger()
{
    // Arrange: a callback that replaces the config it belongs to, e.g. to turn itself off after the first error. The
    // running callback (and the state it captured) must stay alive until it returns, and calling back into the logger
    // must not deadlock.
    TMemoryOutput output;
    TMemoryTextLog log(output);
    std::vector<std::string> calls;
    const auto configWithoutCallback = MakeQuietConfig();
    const std::string callbackState = "callback_state"; // Copied into the callback, so the config owns it

    auto config = MakeQuietConfig();
    config.OnLogEntry = [&log, &calls, &configWithoutCallback, callbackState](const ASWLog::TASWLogRecord&, std::string_view) {
            const bool reconfigured = log.Reconfigure(configWithoutCallback);
            calls.push_back(callbackState + (reconfigured ? ":reconfigured" : ":failed"));
        };
    CheckTrue(log.Initialize(config), "Initialize should succeed");

    // Act
    log.LogError("first_error");
    log.LogError("second_error");

    // Assert
    CheckTrue(calls == std::vector<std::string>{ "callback_state:reconfigured" },
        "The callback should run once, reconfigure the logger, and still have its own state afterwards");
    CheckTrue(output.Lines == std::vector<std::string>{ "[ERROR]: first_error\n", "[ERROR]: second_error\n" },
        "Both entries should be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Reconfigure_AppliesNewConfigButKeepsLevel()
{
    // Arrange
    TMemoryOutput output;
    TMemoryTextLog log(output);

    auto firstConfig = MakeQuietConfig();
    firstConfig.Startup.Banner = "first";

    auto secondConfig = MakeQuietConfig();
    secondConfig.Startup.Banner = "second";
    secondConfig.Line.ShowLevel = false;
    secondConfig.InitialMinimumLevel = ASWLog::Level::Error;

    // Act
    const bool reconfiguredBeforeInitialize = log.Reconfigure(secondConfig);
    CheckTrue(log.Initialize(firstConfig), "Initialize should succeed");
    log.SetMinimumLevel(ASWLog::Level::Debug);
    log.LogDebug("before");
    const bool reconfigured = log.Reconfigure(secondConfig);
    log.LogDebug("after");

    log.ReconfigureResult = false;
    secondConfig.Startup.Banner = "third";
    const bool reconfiguredWithFailingHook = log.Reconfigure(secondConfig);

    // Assert
    CheckFalse(reconfiguredBeforeInitialize, "Reconfigure() should fail before Initialize()");
    CheckTrue(reconfigured, "Reconfigure() should succeed once initialized");
    CheckTrue(output.Lines == std::vector<std::string>{ "[INFO]: first\n", "[DEBUG]: before\n", ": after\n" },
        "Entries after Reconfigure() should use the new line layout, and Reconfigure() should write no startup lines");
    CheckEquals(ASWLog::Level::Debug, log.GetMinimumLevel(),
        "Reconfigure() should keep the minimum level set with SetMinimumLevel(), not apply InitialMinimumLevel");
    CheckTrue(log.ReconfiguredFromBanners == std::vector<std::string>{ "first", "second" },
        "ReconfigureUnlocked() should get the config it replaced (and not be called before Initialize())");
    CheckTrue(log.ReconfiguredToBanners == std::vector<std::string>{ "second", "third" },
        "The new config should be stored before ReconfigureUnlocked() is called");
    CheckFalse(reconfiguredWithFailingHook, "Reconfigure() should fail if ReconfigureUnlocked() fails");
    CheckEquals(std::string("third"), log.GetConfig()->Startup.Banner,
        "The new config should be kept when ReconfigureUnlocked() fails");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Reconfigure_IsSafeWhileOtherThreadsLog()
{
    // Arrange: one thread keeps switching between configs that differ in the line layout, the formatter and the
    // callback, while others log. Run under ThreadSanitizer (CI) to find data races.
    constexpr int ThreadCount = 4;
    constexpr int EntriesPerThread = 2000;
    constexpr int MinimumReconfigureCount = 3;

    TMemoryOutput output;
    auto callbackCount = std::make_shared<std::atomic<int> >(0);

    auto plainConfig = MakeQuietConfig();
    plainConfig.Line.ShowLevel = false;

    auto callbackConfig = MakeQuietConfig();
    callbackConfig.OnLogEntryMinimumLevel = ASWLog::Level::Info;
    callbackConfig.OnLogEntry = [callbackCount](const ASWLog::TASWLogRecord&, std::string_view) {
            callbackCount->fetch_add(1, std::memory_order_relaxed);
        };

    auto formatterConfig = MakeQuietConfig();
    formatterConfig.Line.Formatter = std::make_shared<TPipeFormatter>();

    TMemoryTextLog log(output);
    CheckTrue(log.Initialize(plainConfig), "Initialize should succeed");

    // Act
    std::atomic<bool> loggingDone{ false };
    int reconfigureCount = 0;
    int failedReconfigureCount = 0;
    std::thread reconfigurer([&] {
        const ASWLog::TASWLogConfig* configs[] = { &callbackConfig, &formatterConfig, &plainConfig };
        for (std::size_t i = 0; !loggingDone.load() || i < MinimumReconfigureCount; ++i)
        {
            if (!log.Reconfigure(*configs[i % 3]))
                ++failedReconfigureCount;

            ++reconfigureCount;
        }
            });

    std::vector<std::thread> loggers;
    for (int threadIndex = 0; threadIndex < ThreadCount; ++threadIndex)
    {
        loggers.emplace_back([&log] {
                for (int entry = 0; entry < EntriesPerThread; ++entry)
                    log.LogInfo("entry");
            });
    }

    for (auto& thread : loggers)
        thread.join();

    loggingDone.store(true);
    reconfigurer.join();

    // Assert: every line has one of the three layouts
    std::size_t unexpectedLines = 0;
    for (const auto& line : output.Lines)
    {
        if (line != "[INFO]: entry\n" && line != ": entry\n" && line != "INFO|entry\n")
            ++unexpectedLines;
    }

    CheckEquals(static_cast<std::size_t>(ThreadCount * EntriesPerThread), output.Lines.size(),
        "Every entry should be written while the logger is reconfigured");
    CheckEquals(0, unexpectedLines, "Every line should be written with one whole config");
    CheckGreaterThanOrEqual(reconfigureCount, MinimumReconfigureCount, "The logger should have been reconfigured");
    CheckEquals(0, failedReconfigureCount, "Every Reconfigure() should succeed");
    CheckLessThanOrEqual(callbackCount->load(), ThreadCount * EntriesPerThread,
        "OnLogEntry should be called at most once per entry");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_ReportErrorUnlocked_ReportsAfterTheLockIsReleased()
{
    // Arrange: WriteLineUnlocked() reports a failure while m_Mutex is held
    TMemoryOutput output;
    TMemoryTextLog log(output);
    std::vector<std::string> reportedMessages;
    std::vector<bool> isMutexFreeInHandler;
    auto config = MakeQuietConfig();
    config.ErrorReportInterval = std::chrono::milliseconds(0);
    config.OnError = [&log, &reportedMessages, &isMutexFreeInHandler](const ASWLog::TASWLogError& error) {
            reportedMessages.push_back(error.Message);
            isMutexFreeInHandler.push_back(log.IsMutexFree());
        };
    CheckTrue(log.Initialize(config), "Initialize should succeed");
    log.ReportsOnWrite = true;

    // Act
    log.LogInfo("first");
    log.LogRaw(ASWLog::Level::Info, "second");

    // Assert
    CheckTrue(reportedMessages == std::vector<std::string>{ "memory write failed", "memory write failed" },
        "Each failure reported by a hook should reach OnError");
    CheckTrue(isMutexFreeInHandler == std::vector<bool>{ true, true },
        "OnError should be called after the logger's lock is released, so it can use the logger");
    CheckTrue(output.Lines == std::vector<std::string>{ "[INFO]: first\n", "second" }, "The entries should still be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_SetEnabled_FalseWritesNothing()
{
    // Arrange
    TMemoryOutput output;
    int callbackCount = 0;
    auto config = MakeQuietConfig();
    config.Startup.Banner = "startup banner";
    config.Startup.WriteTimeInfo = true;
    config.Shutdown.WriteLine = true;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Trace;
    config.OnLogEntry = [&callbackCount](const ASWLog::TASWLogRecord&, std::string_view) {
            ++callbackCount;
        };

    // Act
    bool openWhileDisabled = false;
    std::size_t linesWhileDisabled = 0;
    int afterEntryCountWhileDisabled = 0;
    std::size_t linesBeforeDestruction = 0;
    {
        TMemoryTextLog log(output);
        log.SetEnabled(false);
        CheckTrue(log.Initialize(config), "Initialize should succeed while disabled");
        const int afterEntryCountAtStart = output.AfterEntryCount;

        log.LogCritical("critical");
        log.LogRaw(ASWLog::Level::Critical, "critical_raw\n");
        log.LogForce(ASWLog::Level::Critical, "forced");
        log.LogForceRaw(ASWLog::Level::Critical, "forced_raw\n");
        log.LogCriticalFmt("{}", "critical_fmt");
        openWhileDisabled = log.IsOpen();
        linesWhileDisabled = output.Lines.size();
        afterEntryCountWhileDisabled = output.AfterEntryCount - afterEntryCountAtStart;

        log.SetEnabled(true);
        log.LogInfo("enabled_again");
        linesBeforeDestruction = output.Lines.size();

        log.SetEnabled(false); // So the destructor writes no shutdown line
    }

    // Assert
    CheckTrue(openWhileDisabled, "A disabled logger should stay open");
    CheckEquals(0, linesWhileDisabled,
        "A disabled logger should write nothing: no startup lines, entries, or forced entries");
    CheckEquals(0, afterEntryCountWhileDisabled, "A disabled logger's entries should not reach its output at all");
    CheckEquals(1, linesBeforeDestruction, "Logging should resume once enabled again");
    CheckTrue(output.Lines == std::vector<std::string>{ "[INFO]: enabled_again\n" },
        "Disabling before destruction should suppress the shutdown line");
    CheckEquals(1, callbackCount, "OnLogEntry should only fire for the entry written while enabled");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_TextLogBase)
