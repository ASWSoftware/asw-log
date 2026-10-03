/* **************************************************************************
Test_ASWLog_MultiLog.cpp
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
#include "Test_ASWLog_MultiLog.h"
//---------------------------------------------------------------------------
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_FileLog.h"
#include "ASWLog_MultiLog.h"
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace
{

// A value whose formatting is counted in MultiLogFormatCount, to check how often a *Fmt call through a multi-log formats
struct TMultiLogCountedValue
{
};

int MultiLogFormatCount = 0;

} // namespace

template<>
struct std::formatter<TMultiLogCountedValue>
{
    constexpr std::format_parse_context::iterator parse(std::format_parse_context& context)
    {
        return context.begin();
    }

    std::format_context::iterator format(const TMultiLogCountedValue& /*value*/, std::format_context& context) const
    {
        ++MultiLogFormatCount;
        return std::format_to(context.out(), "counted");
    }
};

namespace ASWUnitTests
{

namespace
{

const auto GroupBaseTempDir = std::filesystem::temp_directory_path() / "aswlog_multilog_tests";
const auto TestTempDir = GroupBaseTempDir / "test";

// A sink that records each entry it writes, named for the logging method that makes such a record, e.g.
// "LogForce:text", and the records themselves. Like any TASWLogBase sink it applies its own level (Info by default).
class TRecordingLogger final : public ASWLog::TASWLogBase
{
protected:
    std::string_view GetLoggerClassName() const noexcept override
    {
        return "TRecordingLogger";
    }

    void WriteRecord(const ASWLog::TASWLogRecord& record) override
    {
        const char* method = record.Raw ? (record.Forced ? "LogForceRaw:" : "LogRaw:") : (record.Forced ? "LogForce:" : "Log:");
        Calls.push_back(std::string(method).append(record.Message));
        Records.push_back(record);
        Records.back().Message = {}; // The caller's message is only valid during the call; Calls keeps a copy
    }

public:
    std::vector<std::string> Calls;
    std::vector<ASWLog::TASWLogRecord> Records; // Every field but Message
    bool FlushResult = true; // Returned by Flush()

    bool Initialize(const ASWLog::TASWLogConfig& /*config*/) override
    {
        return true;
    }

    bool Open() override
    {
        return true;
    }

    bool Close() override
    {
        return true;
    }

    bool Flush() noexcept override
    {
        Calls.emplace_back("Flush");
        return FlushResult;
    }

    bool IsOpen() const noexcept override
    {
        return true;
    }
};

// A TASWMultiLog whose clock the test sets, through TASWLogBase's NowUTC() hook
class TFixedClockMultiLog final : public ASWLog::TASWMultiLog
{
public:
    std::chrono::system_clock::time_point CurrentTime{};

protected:
    std::chrono::system_clock::time_point NowUTC() const noexcept override
    {
        return CurrentTime;
    }
};

// A sink that throws for every entry, standing in for a faulty custom IASWLog
class TThrowingLogger final : public ASWLog::TASWLogBase
{
protected:
    std::string_view GetLoggerClassName() const noexcept override
    {
        return "TThrowingLogger";
    }

    void WriteRecord(const ASWLog::TASWLogRecord& /*record*/) override
    {
        throw std::runtime_error("sink failed");
    }

public:
    bool Initialize(const ASWLog::TASWLogConfig& /*config*/) override
    {
        return true;
    }

    bool Open() override
    {
        return true;
    }

    bool Close() override
    {
        return true;
    }

    bool Flush() noexcept override
    {
        return false; // Flush() is noexcept, so a faulty sink can only report the failure
    }

    bool IsOpen() const noexcept override
    {
        return true;
    }
};

std::string ReadFileText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};

    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

// Builds a quiet, Trace-permissive file config so tests can focus on
// composite fan-out behavior rather than formatting/metadata.
ASWLog::TASWLogConfig MakeFileConfig(const std::filesystem::path& dir, const std::filesystem::path& file, ASWLog::Level minLevel)
{
    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = dir;
    config.LogFilePath = file;
    config.InitialMinimumLevel = minLevel;
    config.LogUTCDateTime = false;
    config.LogLevelStr = false;
    config.LogProcessId = false;
    config.LogThreadId = false;
    config.LogMethodName = false;
    config.LogSourceLine = false;
    config.OpenRetryCount = 1;
    config.WriteShutdownLog = false;
    config.Init_LogTimeInfo = false;
    config.Init_LogOSInfo = false;
    config.Init_LogDriveInfo = false;
    config.Init_LogSysMemInfo = false;
    config.Init_LogApplicationInfo = false;
    config.Init_LogMemoryUsage = false;
    return config;
}

} // namespace

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_MultiLog
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_MultiLog::TTest_ASWLog_MultiLog()
    : inherited("ASWLog_MultiLog_Tests")
{
    RegisterTest(&TTest_ASWLog_MultiLog::Test_AddLogger_RejectsDuplicateRegistration, "AddLogger_RejectsDuplicateRegistration");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_AddLogger_RejectsSelfRegistration, "AddLogger_RejectsSelfRegistration");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Close_AllowsInitializeAgain, "Close_AllowsInitializeAgain");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Contains_ReflectsRegistrationState, "Contains_ReflectsRegistrationState");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Flush_ReachesEverySinkEvenAfterAFailure, "Flush_ReachesEverySinkEvenAfterAFailure");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Flush_SucceedsWithNoSinks, "Flush_SucceedsWithNoSinks");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Flush_WorksWhileDisabled, "Flush_WorksWhileDisabled");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Flush_WritesBufferedEntriesOfEveryFileSink, "Flush_WritesBufferedEntriesOfEveryFileSink");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_GetLoggerCount_ReflectsAddAndRemove, "GetLoggerCount_ReflectsAddAndRemove");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_GetLoggers_ReturnsSnapshotOfRegisteredSinks, "GetLoggers_ReturnsSnapshotOfRegisteredSinks");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Initialize_ConcurrentCallsSucceedOnce, "Initialize_ConcurrentCallsSucceedOnce");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_IsOpen_RequiresAllSinksOpen, "IsOpen_RequiresAllSinksOpen");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Log_AtLevelOffIsNotFannedOut, "Log_AtLevelOffIsNotFannedOut");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Log_FansOutToAllRegisteredSinks, "Log_FansOutToAllRegisteredSinks");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Log_ThrowingSinkDoesNotStopOtherSinks, "Log_ThrowingSinkDoesNotStopOtherSinks");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_LogFmt_FormatsOnceForAllSinks, "LogFmt_FormatsOnceForAllSinks");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_LogForce_BypassesCompositeGate, "LogForce_BypassesCompositeGate");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_RemoveAllLoggers_ClearsRegistrationAndReturnsCount, "RemoveAllLoggers_ClearsRegistrationAndReturnsCount");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_RemoveLogger_StopsReceivingEntries, "RemoveLogger_StopsReceivingEntries");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_SetEnabled_FalseStopsFanOut, "SetEnabled_FalseStopsFanOut");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_SetMinimumLevel_GatesFanOutBeforeSinks, "SetMinimumLevel_GatesFanOutBeforeSinks");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_ShouldLog_RequiresCompositeGateAndAnySink, "ShouldLog_RequiresCompositeGateAndAnySink");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Write_PassesOneStampedRecordToEverySink, "Write_PassesOneStampedRecordToEverySink");
}
//---------------------------------------------------------------------------
TTest_ASWLog_MultiLog::~TTest_ASWLog_MultiLog()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::SetUp_Group()
{
    Log("Setting up temp group folder: " + GroupBaseTempDir.string());
    std::filesystem::create_directories(GroupBaseTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::SetUp_Test(ITestCase& testCase)
{
    Log("  Setting up temp folder for " + testCase.GetName() + ": " + TestTempDir.string());
    std::filesystem::create_directories(TestTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::TearDown_Group()
{
    Log("Cleaning up temp group folder:" + GroupBaseTempDir.string());
    std::filesystem::remove_all(GroupBaseTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::TearDown_Test(ITestCase& testCase)
{
    Log("  Cleaning up temp folder for " + testCase.GetName() + ": " + TestTempDir.string());
    std::filesystem::remove_all(TestTempDir);
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_AddLogger_RejectsDuplicateRegistration()
{
    // Arrange
    const auto fileA = TestTempDir / "duplicate_sink.log";
    ASWLog::TASWFileLog sinkA;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), __func__, __LINE__, "sinkA should initialize");

    ASWLog::TASWMultiLog multiLog;

    // Act
    const bool firstAdd = multiLog.AddLogger(sinkA);
    const bool secondAdd = multiLog.AddLogger(sinkA);
    multiLog.LogInfo("should_appear_once");
    sinkA.Close();

    const auto contents = ReadFileText(fileA);
    const auto firstPos = contents.find("should_appear_once");
    const auto secondPos = (firstPos == std::string::npos) ? std::string::npos : contents.find("should_appear_once", firstPos + 1);

    // Assert
    CheckTrue(firstAdd, __func__, __LINE__, "First AddLogger call should register the sink and return true");
    CheckFalse(secondAdd, __func__, __LINE__, "Second AddLogger call with the same sink should be a no-op and return false");
    CheckTrue(firstPos != std::string::npos, __func__, __LINE__, "The message should reach the sink");
    CheckTrue(secondPos == std::string::npos, __func__, __LINE__, "The message should not be duplicated in the sink");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_AddLogger_RejectsSelfRegistration()
{
    // Arrange
    ASWLog::TASWMultiLog multiLog;

    // Act
    const bool added = multiLog.AddLogger(multiLog);

    // Assert
    CheckFalse(added, __func__, __LINE__, "AddLogger should reject registering the composite with itself to avoid infinite recursion in Log()");

    // A message logged afterward must return normally - if self-registration had
    // succeeded, this call would infinitely re-enter Log() via the fanned-out sink list.
    multiLog.LogInfo("no_recursion_expected");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Close_AllowsInitializeAgain()
{
    // Arrange
    const auto fileA = TestTempDir / "reinitialize_sink.log";
    ASWLog::TASWFileLog sinkA;
    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);

    // Act: initialize, close, then initialize again with a different level
    const bool firstInitialize = multiLog.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Warn));
    multiLog.Close();
    const bool secondInitialize = multiLog.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Error));
    const auto levelAfterSecondInitialize = multiLog.GetMinimumLevel();
    multiLog.LogError("after_reinitialize");
    multiLog.Close();

    // Assert
    CheckTrue(firstInitialize, __func__, __LINE__, "The first Initialize should succeed");
    CheckTrue(secondInitialize, __func__, __LINE__, "Initialize after Close should succeed");
    CheckTrue(levelAfterSecondInitialize == ASWLog::Level::Error, __func__, __LINE__, "Initialize after Close should seed the level from the new config");
    CheckTrue(ReadFileText(fileA).find("after_reinitialize") != std::string::npos, __func__, __LINE__, "Initialize after Close should initialize the sinks again");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Contains_ReflectsRegistrationState()
{
    // Arrange
    const auto fileA = TestTempDir / "contains_sink.log";
    ASWLog::TASWFileLog sinkA;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), __func__, __LINE__, "sinkA should initialize");

    ASWLog::TASWMultiLog multiLog;

    // Act & Assert
    CheckFalse(multiLog.Contains(sinkA), __func__, __LINE__, "A sink should not be reported as contained before it is added");

    multiLog.AddLogger(sinkA);
    CheckTrue(multiLog.Contains(sinkA), __func__, __LINE__, "A sink should be reported as contained after AddLogger");

    multiLog.RemoveLogger(sinkA);
    CheckFalse(multiLog.Contains(sinkA), __func__, __LINE__, "A sink should no longer be reported as contained after RemoveLogger");

    sinkA.Close();
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Flush_ReachesEverySinkEvenAfterAFailure()
{
    // Arrange: the first sink's flush fails
    TRecordingLogger failingSink;
    failingSink.FlushResult = false;
    TRecordingLogger sink;

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(failingSink);
    multiLog.AddLogger(sink);

    // Act
    const bool flushedWithFailure = multiLog.Flush();
    failingSink.FlushResult = true;
    const bool flushed = multiLog.Flush();

    // Assert
    const std::vector<std::string> twoFlushes{ "Flush", "Flush" };
    CheckFalse(flushedWithFailure, __func__, __LINE__, "Flush() should return false if any sink's flush fails");
    CheckTrue(flushed, __func__, __LINE__, "Flush() should return true once every sink's flush succeeds");
    CheckTrue(failingSink.Calls == twoFlushes, __func__, __LINE__, "Each Flush() should reach the failing sink");
    CheckTrue(sink.Calls == twoFlushes, __func__, __LINE__, "A sink's failed flush should not stop the other sinks from being flushed");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Flush_SucceedsWithNoSinks()
{
    // Arrange
    ASWLog::TASWMultiLog multiLog;

    // Act & Assert
    CheckTrue(multiLog.Flush(), __func__, __LINE__, "Flush() with no registered sinks should succeed, like IsOpen()");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Flush_WorksWhileDisabled()
{
    // Arrange
    TRecordingLogger sink;
    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sink);
    multiLog.SetEnabled(false);

    // Act
    const bool flushed = multiLog.Flush();

    // Assert
    CheckTrue(flushed, __func__, __LINE__, "Flush() should succeed while the composite is disabled");
    CheckTrue(sink.Calls == std::vector<std::string>{ "Flush" }, __func__, __LINE__, "A disabled composite should still flush its sinks; disabling only stops new entries");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Flush_WritesBufferedEntriesOfEveryFileSink()
{
    // Arrange: FlushMode::Manual keeps each entry in the file's buffer until Flush()
    const auto fileA = TestTempDir / "flush_sink_a.log";
    const auto fileB = TestTempDir / "flush_sink_b.log";
    auto configA = MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace);
    auto configB = MakeFileConfig(TestTempDir, fileB, ASWLog::Level::Trace);
    configA.LogFlushMode = ASWLog::FlushMode::Manual;
    configB.LogFlushMode = ASWLog::FlushMode::Manual;

    ASWLog::TASWFileLog sinkA;
    ASWLog::TASWFileLog sinkB;
    CheckTrue(sinkA.Initialize(configA), __func__, __LINE__, "sinkA should initialize");
    CheckTrue(sinkB.Initialize(configB), __func__, __LINE__, "sinkB should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.AddLogger(sinkB);
    ASWLog::IASWLog& logger = multiLog; // As generic code would flush it

    // Act
    logger.LogInfo("buffered_entry");
    const auto contentsBeforeA = ReadFileText(fileA);
    const auto contentsBeforeB = ReadFileText(fileB);
    const bool flushed = logger.Flush();
    const auto contentsAfterA = ReadFileText(fileA);
    const auto contentsAfterB = ReadFileText(fileB);

    sinkA.Close();
    sinkB.Close();

    // Assert
    CheckTrue(contentsBeforeA.find("buffered_entry") == std::string::npos, __func__, __LINE__, "Before Flush(), sinkA's entry should still be buffered");
    CheckTrue(contentsBeforeB.find("buffered_entry") == std::string::npos, __func__, __LINE__, "Before Flush(), sinkB's entry should still be buffered");
    CheckTrue(flushed, __func__, __LINE__, "Flush() should succeed");
    CheckTrue(contentsAfterA.find("buffered_entry") != std::string::npos, __func__, __LINE__, "Flush() should write sinkA's buffered entry to its file");
    CheckTrue(contentsAfterB.find("buffered_entry") != std::string::npos, __func__, __LINE__, "Flush() should write sinkB's buffered entry to its file");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_GetLoggerCount_ReflectsAddAndRemove()
{
    // Arrange
    const auto fileA = TestTempDir / "count_a.log";
    const auto fileB = TestTempDir / "count_b.log";

    ASWLog::TASWFileLog sinkA;
    ASWLog::TASWFileLog sinkB;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), __func__, __LINE__, "sinkA should initialize");
    CheckTrue(sinkB.Initialize(MakeFileConfig(TestTempDir, fileB, ASWLog::Level::Trace)), __func__, __LINE__, "sinkB should initialize");

    ASWLog::TASWMultiLog multiLog;

    // Act & Assert
    CheckEquals(static_cast<size_t>(0), multiLog.GetLoggerCount(), __func__, __LINE__, "A new composite should start with no registered sinks");

    multiLog.AddLogger(sinkA);
    CheckEquals(static_cast<size_t>(1), multiLog.GetLoggerCount(), __func__, __LINE__, "Count should increase after AddLogger");

    multiLog.AddLogger(sinkB);
    CheckEquals(static_cast<size_t>(2), multiLog.GetLoggerCount(), __func__, __LINE__, "Count should increase for each distinct sink");

    multiLog.AddLogger(sinkA); // Duplicate - should be a no-op
    CheckEquals(static_cast<size_t>(2), multiLog.GetLoggerCount(), __func__, __LINE__, "Duplicate AddLogger should not increase the count");

    multiLog.RemoveLogger(sinkA);
    CheckEquals(static_cast<size_t>(1), multiLog.GetLoggerCount(), __func__, __LINE__, "Count should decrease after RemoveLogger");

    sinkA.Close();
    sinkB.Close();
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_GetLoggers_ReturnsSnapshotOfRegisteredSinks()
{
    // Arrange
    const auto fileA = TestTempDir / "loggers_a.log";
    const auto fileB = TestTempDir / "loggers_b.log";

    ASWLog::TASWFileLog sinkA;
    ASWLog::TASWFileLog sinkB;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), __func__, __LINE__, "sinkA should initialize");
    CheckTrue(sinkB.Initialize(MakeFileConfig(TestTempDir, fileB, ASWLog::Level::Trace)), __func__, __LINE__, "sinkB should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.AddLogger(sinkB);

    // Act
    const auto loggers = multiLog.GetLoggers();

    // Assert
    CheckEquals(static_cast<size_t>(2), loggers.size(), __func__, __LINE__, "GetLoggers should return one entry per registered sink");
    CheckTrue(std::find(loggers.begin(), loggers.end(), &sinkA) != loggers.end(), __func__, __LINE__, "GetLoggers should include sinkA");
    CheckTrue(std::find(loggers.begin(), loggers.end(), &sinkB) != loggers.end(), __func__, __LINE__, "GetLoggers should include sinkB");

    sinkA.Close();
    sinkB.Close();
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Initialize_ConcurrentCallsSucceedOnce()
{
    // Arrange
    constexpr int roundCount = 200;
    constexpr int threadCount = 4;
    int roundsWithOneSuccess = 0;

    // A large config takes longer to copy, which widens the gap an unsynchronized check-then-set would leave
    ASWLog::TASWLogConfig config;
    config.BannerMessage_Init.assign(64 * 1024, 'x');

    // Act: in each round, several threads call Initialize on a new composite at the same moment
    for (int round = 0; round < roundCount; ++round)
    {
        ASWLog::TASWMultiLog multiLog;
        std::atomic<bool> start{ false };
        std::atomic<int> successCount{ 0 };

        std::vector<std::thread> threads;
        for (int index = 0; index < threadCount; ++index)
        {
            threads.emplace_back([&] {
                    while (!start.load())
                        std::this_thread::yield();

                    if (multiLog.Initialize(config))
                        ++successCount;
                });
        }

        start.store(true);
        for (auto& thread : threads)
            thread.join();

        if (successCount.load() == 1)
            ++roundsWithOneSuccess;
    }

    // Assert
    CheckEquals(roundCount, roundsWithOneSuccess, __func__, __LINE__, "Exactly one of the concurrent Initialize calls should succeed");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_IsOpen_RequiresAllSinksOpen()
{
    // Arrange
    const auto fileA = TestTempDir / "open_a.log";
    const auto fileB = TestTempDir / "open_b.log";

    ASWLog::TASWFileLog sinkA;
    ASWLog::TASWFileLog sinkB;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), __func__, __LINE__, "sinkA should initialize");
    CheckTrue(sinkB.Initialize(MakeFileConfig(TestTempDir, fileB, ASWLog::Level::Trace)), __func__, __LINE__, "sinkB should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.AddLogger(sinkB);

    // Act & Assert
    CheckTrue(multiLog.IsOpen(), __func__, __LINE__, "Composite should report open while every registered sink is open");

    sinkB.Close();

    CheckFalse(multiLog.IsOpen(), __func__, __LINE__, "Composite should report closed if any registered sink is closed");

    sinkA.Close();
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Log_AtLevelOffIsNotFannedOut()
{
    // Arrange
    TRecordingLogger sink;
    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sink);
    multiLog.SetMinimumLevel(ASWLog::Level::Trace);

    // Act
    multiLog.Log(ASWLog::Level::Off, "off");
    multiLog.LogRaw(ASWLog::Level::Off, "off_raw");
    multiLog.LogForce(ASWLog::Level::Off, "off_forced");
    multiLog.LogForceRaw(ASWLog::Level::Off, "off_forced_raw");
    const auto callsAtOffLevel = sink.Calls;

    multiLog.SetMinimumLevel(ASWLog::Level::Off);
    multiLog.LogCritical("critical");
    multiLog.LogRaw(ASWLog::Level::Critical, "critical_raw");
    multiLog.LogForce(ASWLog::Level::Info, "forced");
    multiLog.LogForceRaw(ASWLog::Level::Info, "forced_raw");

    // Assert
    CheckTrue(callsAtOffLevel.empty(), __func__, __LINE__, "A message at Off should not reach any sink, even when forced");
    CheckTrue(sink.Calls == std::vector<std::string>{ "LogForce:forced", "LogForceRaw:forced_raw" }, __func__, __LINE__,
        "With the composite's minimum level at Off, only forced entries should be fanned out");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Log_FansOutToAllRegisteredSinks()
{
    // Arrange
    const auto fileA = TestTempDir / "sink_a.log";
    const auto fileB = TestTempDir / "sink_b.log";

    ASWLog::TASWFileLog sinkA;
    ASWLog::TASWFileLog sinkB;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), __func__, __LINE__, "sinkA should initialize");
    CheckTrue(sinkB.Initialize(MakeFileConfig(TestTempDir, fileB, ASWLog::Level::Trace)), __func__, __LINE__, "sinkB should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.AddLogger(sinkB);

    // Act
    multiLog.LogInfo("fanned_out_message");
    sinkA.Close();
    sinkB.Close();

    // Assert
    CheckTrue(ReadFileText(fileA).find("fanned_out_message") != std::string::npos, __func__, __LINE__, "sinkA should receive the fanned-out message");
    CheckTrue(ReadFileText(fileB).find("fanned_out_message") != std::string::npos, __func__, __LINE__, "sinkB should receive the fanned-out message");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Log_ThrowingSinkDoesNotStopOtherSinks()
{
    // Arrange: the throwing sink is registered first, so it's called before the file sink
    const auto file = TestTempDir / "after_throwing_sink.log";

    TThrowingLogger throwingSink;
    ASWLog::TASWFileLog fileSink;
    CheckTrue(fileSink.Initialize(MakeFileConfig(TestTempDir, file, ASWLog::Level::Trace)), __func__, __LINE__, "fileSink should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(throwingSink);
    multiLog.AddLogger(fileSink);

    bool threw = false;

    // Act
    try
    {
        multiLog.LogInfo("log_message");
        multiLog.LogRaw(ASWLog::Level::Info, "raw_message\n");
        multiLog.LogForce(ASWLog::Level::Info, "force_message");
        multiLog.LogForceRaw(ASWLog::Level::Info, "force_raw_message\n");
    }
    catch (...)
    {
        threw = true;
    }
    fileSink.Close();

    // Assert
    const auto contents = ReadFileText(file);
    CheckFalse(threw, __func__, __LINE__, "An exception from one sink should not escape the composite");
    CheckTrue(contents.find("log_message") != std::string::npos, __func__, __LINE__, "Log should still reach the other sinks");
    CheckTrue(contents.find("raw_message") != std::string::npos, __func__, __LINE__, "LogRaw should still reach the other sinks");
    CheckTrue(contents.find("force_message") != std::string::npos, __func__, __LINE__, "LogForce should still reach the other sinks");
    CheckTrue(contents.find("force_raw_message") != std::string::npos, __func__, __LINE__, "LogForceRaw should still reach the other sinks");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_LogFmt_FormatsOnceForAllSinks()
{
    // Arrange: two sinks at the default minimum level (Info), and a composite that lets everything through
    TRecordingLogger sinkA;
    TRecordingLogger sinkB;
    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.AddLogger(sinkB);
    multiLog.SetMinimumLevel(ASWLog::Level::Trace);
    MultiLogFormatCount = 0;

    // Act
    multiLog.LogDebugFmt("{}", TMultiLogCountedValue{});
    const int countAfterFilteredEntry = MultiLogFormatCount;
    multiLog.LogInfoFmt("{}", TMultiLogCountedValue{});

    // Assert
    CheckEquals(0, countAfterFilteredEntry, __func__, __LINE__, "An entry no sink would write should not be formatted");
    CheckEquals(1, MultiLogFormatCount, __func__, __LINE__, "An entry should be formatted once, however many sinks get it");
    CheckTrue(sinkA.Calls == std::vector<std::string>{ "Log:counted" } && sinkB.Calls == sinkA.Calls, __func__, __LINE__,
        "Both sinks should get the formatted entry");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_LogForce_BypassesCompositeGate()
{
    // Arrange
    const auto fileA = TestTempDir / "force_sink.log";

    ASWLog::TASWFileLog sinkA;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), __func__, __LINE__, "sinkA should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.SetMinimumLevel(ASWLog::Level::Error);

    // Act
    multiLog.LogForce(ASWLog::Level::Info, "forced_past_composite_gate");
    sinkA.Close();

    // Assert
    CheckTrue(ReadFileText(fileA).find("forced_past_composite_gate") != std::string::npos, __func__, __LINE__, "LogForce should bypass the composite's own MinimumLevel gate");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_RemoveAllLoggers_ClearsRegistrationAndReturnsCount()
{
    // Arrange
    const auto fileA = TestTempDir / "remove_all_a.log";
    const auto fileB = TestTempDir / "remove_all_b.log";

    ASWLog::TASWFileLog sinkA;
    ASWLog::TASWFileLog sinkB;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), __func__, __LINE__, "sinkA should initialize");
    CheckTrue(sinkB.Initialize(MakeFileConfig(TestTempDir, fileB, ASWLog::Level::Trace)), __func__, __LINE__, "sinkB should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.AddLogger(sinkB);

    // Act
    const auto removedCount = multiLog.RemoveAllLoggers();
    multiLog.LogInfo("should_not_reach_any_sink");
    sinkA.Close();
    sinkB.Close();

    // Assert
    CheckEquals(static_cast<size_t>(2), removedCount, __func__, __LINE__, "RemoveAllLoggers should return the number of sinks that were registered");
    CheckEquals(static_cast<size_t>(0), multiLog.GetLoggerCount(), __func__, __LINE__, "No sinks should remain registered after RemoveAllLoggers");
    CheckTrue(ReadFileText(fileA).find("should_not_reach_any_sink") == std::string::npos, __func__, __LINE__, "sinkA should not receive entries after RemoveAllLoggers");
    CheckTrue(ReadFileText(fileB).find("should_not_reach_any_sink") == std::string::npos, __func__, __LINE__, "sinkB should not receive entries after RemoveAllLoggers");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_RemoveLogger_StopsReceivingEntries()
{
    // Arrange
    const auto fileA = TestTempDir / "remove_sink.log";

    ASWLog::TASWFileLog sinkA;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), __func__, __LINE__, "sinkA should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);

    // Act
    multiLog.LogInfo("before_removal");
    const bool removed = multiLog.RemoveLogger(sinkA);
    const bool removedAgain = multiLog.RemoveLogger(sinkA);
    multiLog.LogInfo("after_removal");
    sinkA.Close();

    const auto contents = ReadFileText(fileA);

    // Assert
    CheckTrue(removed, __func__, __LINE__, "RemoveLogger should return true when the sink was registered");
    CheckFalse(removedAgain, __func__, __LINE__, "RemoveLogger should return false when the sink is no longer registered");
    CheckTrue(contents.find("before_removal") != std::string::npos, __func__, __LINE__, "Message logged before removal should reach the sink");
    CheckTrue(contents.find("after_removal") == std::string::npos, __func__, __LINE__, "Message logged after removal should not reach the sink");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_SetEnabled_FalseStopsFanOut()
{
    // Arrange
    TRecordingLogger sink;
    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sink);
    multiLog.SetMinimumLevel(ASWLog::Level::Trace);

    // Act
    multiLog.SetEnabled(false);
    multiLog.LogCritical("critical");
    multiLog.LogRaw(ASWLog::Level::Critical, "critical_raw");
    multiLog.LogForce(ASWLog::Level::Critical, "forced");
    multiLog.LogForceRaw(ASWLog::Level::Critical, "forced_raw");
    multiLog.LogForceFmt(ASWLog::Level::Critical, "{}", "forced_fmt");
    const auto callsWhileDisabled = sink.Calls;

    multiLog.SetEnabled(true);
    multiLog.LogInfo("enabled_again");

    // Assert
    CheckTrue(callsWhileDisabled.empty(), __func__, __LINE__, "A disabled composite should fan out nothing, not even forced entries");
    CheckTrue(sink.IsEnabled(), __func__, __LINE__, "Disabling the composite should not change its sinks");
    CheckTrue(sink.Calls == std::vector<std::string>{ "Log:enabled_again" }, __func__, __LINE__, "Fan-out should resume once enabled again");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_SetMinimumLevel_GatesFanOutBeforeSinks()
{
    // Arrange
    const auto fileA = TestTempDir / "gate_sink.log";

    ASWLog::TASWFileLog sinkA;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), __func__, __LINE__, "sinkA should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.SetMinimumLevel(ASWLog::Level::Error);

    // Act
    multiLog.LogInfo("blocked_by_composite_gate");
    multiLog.LogError("passes_composite_gate");
    sinkA.Close();

    const auto contents = ReadFileText(fileA);

    // Assert
    CheckTrue(contents.find("blocked_by_composite_gate") == std::string::npos, __func__, __LINE__, "Composite MinimumLevel should gate fan-out even though the sink's own level would allow it");
    CheckTrue(contents.find("passes_composite_gate") != std::string::npos, __func__, __LINE__, "Entries at or above the composite level should still reach the sink");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_ShouldLog_RequiresCompositeGateAndAnySink()
{
    // Arrange
    TRecordingLogger errorSink;
    TRecordingLogger warnSink;
    errorSink.SetMinimumLevel(ASWLog::Level::Error);
    warnSink.SetMinimumLevel(ASWLog::Level::Warn);
    ASWLog::TASWMultiLog multiLog;
    multiLog.SetMinimumLevel(ASWLog::Level::Trace);
    const bool shouldLogWithoutSinks = multiLog.ShouldLog(ASWLog::Level::Critical);
    multiLog.AddLogger(errorSink);
    multiLog.AddLogger(warnSink);

    // Act
    const bool shouldLogInfo = multiLog.ShouldLog(ASWLog::Level::Info);
    const bool shouldLogWarn = multiLog.ShouldLog(ASWLog::Level::Warn);

    warnSink.SetEnabled(false);
    const bool shouldLogWarnWithWarnSinkDisabled = multiLog.ShouldLog(ASWLog::Level::Warn);
    const bool shouldLogErrorWithWarnSinkDisabled = multiLog.ShouldLog(ASWLog::Level::Error);

    multiLog.SetMinimumLevel(ASWLog::Level::Critical);
    const bool shouldLogErrorBelowCompositeLevel = multiLog.ShouldLog(ASWLog::Level::Error);

    multiLog.SetMinimumLevel(ASWLog::Level::Trace);
    multiLog.SetEnabled(false);
    const bool shouldLogErrorWithCompositeDisabled = multiLog.ShouldLog(ASWLog::Level::Error);

    // Assert
    CheckFalse(shouldLogWithoutSinks, __func__, __LINE__, "A composite with no sinks should log nothing");
    CheckFalse(shouldLogInfo, __func__, __LINE__, "No sink accepts Info");
    CheckTrue(shouldLogWarn, __func__, __LINE__, "One sink accepts Warn, which is enough");
    CheckFalse(shouldLogWarnWithWarnSinkDisabled, __func__, __LINE__, "A disabled sink should not count");
    CheckTrue(shouldLogErrorWithWarnSinkDisabled, __func__, __LINE__, "The enabled sink still accepts Error");
    CheckFalse(shouldLogErrorBelowCompositeLevel, __func__, __LINE__, "The composite's own minimum level should apply first");
    CheckFalse(shouldLogErrorWithCompositeDisabled, __func__, __LINE__, "A disabled composite should log nothing");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Write_PassesOneStampedRecordToEverySink()
{
    // Arrange: sinks on the system clock behind a composite with a fixed clock; sinkB only takes Error and above
    TRecordingLogger sinkA;
    TRecordingLogger sinkB;
    sinkB.SetMinimumLevel(ASWLog::Level::Error);

    TFixedClockMultiLog multiLog;
    multiLog.CurrentTime = std::chrono::system_clock::time_point(std::chrono::hours(1000));
    multiLog.SetMinimumLevel(ASWLog::Level::Trace);
    multiLog.AddLogger(sinkA);
    multiLog.AddLogger(sinkB);

    // Act
    multiLog.LogWarn("warn");
    multiLog.LogForceRaw(ASWLog::Level::Debug, "forced_raw");

    // Assert
    CheckTrue(sinkA.Calls == std::vector<std::string>{ "Log:warn", "LogForceRaw:forced_raw" }, __func__, __LINE__,
        "sinkA should get both entries, with their Raw and Forced flags");
    CheckTrue(sinkB.Calls == std::vector<std::string>{ "LogForceRaw:forced_raw" }, __func__, __LINE__,
        "sinkB should apply its own level, so only the forced entry reaches it");

    std::vector<ASWLog::TASWLogRecord> records = sinkA.Records;
    records.insert(records.end(), sinkB.Records.begin(), sinkB.Records.end());
    for (const auto& record : records)
    {
        CheckTrue(record.Timestamp == multiLog.CurrentTime, __func__, __LINE__, "Each sink should keep the composite's stamp, not read its own clock");
        CheckEquals(static_cast<int64_t>(ASWLog::GetCurrentOSThreadId()), static_cast<int64_t>(record.ThreadId), __func__, __LINE__, "The record should carry the logging thread's id");
    }
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_MultiLog)
