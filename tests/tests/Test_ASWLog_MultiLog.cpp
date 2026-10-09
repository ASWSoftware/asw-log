/* **************************************************************************
Test_ASWLog_MultiLog.cpp
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
#include "Test_ASWLog_MultiLog.h"
//---------------------------------------------------------------------------
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
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
    bool ReconfigureResult = true; // Returned by Reconfigure()
    bool WasInitialized = false; // Initialize() was called
    bool InitializeHadBeforeWrite = false; // The config passed to Initialize() had an OnBeforeWrite hook

    bool Initialize(const ASWLog::TASWLogConfig& config) noexcept override
    {
        WasInitialized = true;
        InitializeHadBeforeWrite = config.OnBeforeWrite != nullptr;
        return true;
    }

    bool Reconfigure(const ASWLog::TASWLogConfig& config) noexcept override
    {
        Calls.emplace_back("Reconfigure");
        [[maybe_unused]] const auto previousConfig = SetConfig(config);
        return ReconfigureResult;
    }

    bool Open() noexcept override
    {
        return true;
    }

    bool Close() noexcept override
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
    bool Initialize(const ASWLog::TASWLogConfig& config) noexcept override
    {
        try
        {
            [[maybe_unused]] const auto previousConfig = SetConfig(config); // For its OnError
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool Reconfigure(const ASWLog::TASWLogConfig& /*config*/) noexcept override
    {
        return false;
    }

    bool Open() noexcept override
    {
        return true;
    }

    bool Close() noexcept override
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

// A config with a backtrace of 5 entries from Trace up, written at Error, for a logger whose minimum level is
// 'minimumLevel'
ASWLog::TASWLogConfig MakeBacktraceConfig(ASWLog::Level minimumLevel)
{
    ASWLog::TASWLogConfig config;
    config.InitialMinimumLevel = minimumLevel;
    config.Backtrace.Capacity = 5;
    config.Backtrace.LowestLevel = ASWLog::Level::Trace;
    config.Backtrace.DumpAtLevel = ASWLog::Level::Error;
    return config;
}

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
    config.File.FolderPath = dir;
    config.File.FilePath = file;
    config.InitialMinimumLevel = minLevel;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = false;
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
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Backtrace_EachLoggerKeepsItsOwn, "Backtrace_EachLoggerKeepsItsOwn");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Backtrace_IsNotKeptByTheMultiLog, "Backtrace_IsNotKeptByTheMultiLog");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Close_AllowsInitializeAgain, "Close_AllowsInitializeAgain");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Contains_ReflectsRegistrationState, "Contains_ReflectsRegistrationState");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_DumpBacktrace_ReachesEverySinkUnlessDisabled, "DumpBacktrace_ReachesEverySinkUnlessDisabled");
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
    RegisterTest(&TTest_ASWLog_MultiLog::Test_OnBeforeWrite_RunsOnceBeforeTheFanOut, "OnBeforeWrite_RunsOnceBeforeTheFanOut");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Reconfigure_PassesConfigToEverySink, "Reconfigure_PassesConfigToEverySink");
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
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), "sinkA should initialize");

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
    CheckTrue(firstAdd, "First AddLogger call should register the sink and return true");
    CheckFalse(secondAdd, "Second AddLogger call with the same sink should be a no-op and return false");
    CheckTrue(firstPos != std::string::npos, "The message should reach the sink");
    CheckTrue(secondPos == std::string::npos, "The message should not be duplicated in the sink");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_AddLogger_RejectsSelfRegistration()
{
    // Arrange
    ASWLog::TASWMultiLog multiLog;

    // Act
    const bool added = multiLog.AddLogger(multiLog);

    // Assert
    CheckFalse(added, "AddLogger should reject registering the composite with itself to avoid infinite recursion in Log()");

    // A message logged afterward must return normally - if self-registration had
    // succeeded, this call would infinitely re-enter Log() via the fanned-out sink list.
    multiLog.LogInfo("no_recursion_expected");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Backtrace_EachLoggerKeepsItsOwn()
{
    // Arrange: the multi-log lets every level through; each sink keeps what is below its own minimum level
    const auto config = MakeBacktraceConfig(ASWLog::Level::Trace);
    TRecordingLogger infoSink;
    TRecordingLogger warnSink;
    infoSink.Reconfigure(config);
    warnSink.Reconfigure(config);
    infoSink.SetMinimumLevel(ASWLog::Level::Info);
    warnSink.SetMinimumLevel(ASWLog::Level::Warn);
    infoSink.Calls.clear();
    warnSink.Calls.clear();

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(infoSink);
    multiLog.AddLogger(warnSink);
    multiLog.Initialize(config);

    // Act
    multiLog.LogDebug("debug_1");
    multiLog.LogInfo("info_1");
    multiLog.LogError("error_1");

    // Assert
    const std::vector<std::string> expectedInfo{ "Log:info_1", "LogForce:Backtrace: the last 1 entry below the minimum level",
                                                 "LogForce:debug_1", "LogForce:Backtrace end", "Log:error_1" };
    CheckTrue(infoSink.Calls == expectedInfo, "The Info sink should keep only the Debug entry");
    const std::vector<std::string> expectedWarn{ "LogForce:Backtrace: the last 2 entries below the minimum level", "LogForce:debug_1",
                                                 "LogForce:info_1", "LogForce:Backtrace end", "Log:error_1" };
    CheckTrue(warnSink.Calls == expectedWarn, "The Warn sink should keep the Debug and Info entries");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Backtrace_IsNotKeptByTheMultiLog()
{
    // Arrange: the multi-log's own minimum level is Info, and its config asks for a backtrace
    const auto config = MakeBacktraceConfig(ASWLog::Level::Info);
    TRecordingLogger sink;
    sink.Reconfigure(config);
    sink.Calls.clear();

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sink);
    multiLog.Initialize(config);

    // Act
    multiLog.LogDebug("debug_1");
    multiLog.LogError("error_1");

    // Assert: it filters the Debug entry first, and keeps nothing itself
    CheckFalse(multiLog.ShouldLog(ASWLog::Level::Debug), "The multi-log's gate should stay its minimum level");
    CheckTrue(sink.Calls == std::vector<std::string>{ "Log:error_1" }, "Neither the multi-log nor the sink should keep the Debug entry");
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
    CheckTrue(firstInitialize, "The first Initialize should succeed");
    CheckTrue(secondInitialize, "Initialize after Close should succeed");
    CheckEquals(ASWLog::Level::Error, levelAfterSecondInitialize, "Initialize after Close should seed the level from the new config");
    CheckContains(ReadFileText(fileA), "after_reinitialize", "Initialize after Close should initialize the sinks again");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Contains_ReflectsRegistrationState()
{
    // Arrange
    const auto fileA = TestTempDir / "contains_sink.log";
    ASWLog::TASWFileLog sinkA;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), "sinkA should initialize");

    ASWLog::TASWMultiLog multiLog;

    // Act & Assert
    CheckFalse(multiLog.Contains(sinkA), "A sink should not be reported as contained before it is added");

    multiLog.AddLogger(sinkA);
    CheckTrue(multiLog.Contains(sinkA), "A sink should be reported as contained after AddLogger");

    multiLog.RemoveLogger(sinkA);
    CheckFalse(multiLog.Contains(sinkA), "A sink should no longer be reported as contained after RemoveLogger");

    sinkA.Close();
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_DumpBacktrace_ReachesEverySinkUnlessDisabled()
{
    // Arrange
    const auto config = MakeBacktraceConfig(ASWLog::Level::Trace);
    TRecordingLogger sinkA;
    TRecordingLogger sinkB;
    for (auto* sink : { &sinkA, &sinkB })
    {
        sink->Reconfigure(config);
        sink->SetMinimumLevel(ASWLog::Level::Info);
    }

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.AddLogger(sinkB);
    multiLog.Initialize(config);
    multiLog.LogDebug("debug_1");
    sinkA.Calls.clear();
    sinkB.Calls.clear();

    // Act
    multiLog.SetEnabled(false);
    multiLog.DumpBacktrace();
    const auto callsWhileDisabled = sinkA.Calls.size() + sinkB.Calls.size();
    multiLog.SetEnabled(true);
    multiLog.DumpBacktrace();

    // Assert
    CheckEquals(0, callsWhileDisabled, "A disabled multi-log should pass nothing on");
    const std::vector<std::string> expected{ "LogForce:Backtrace: the last 1 entry below the minimum level", "LogForce:debug_1",
                                             "LogForce:Backtrace end" };
    CheckTrue(sinkA.Calls == expected, "The first sink should write its backtrace");
    CheckTrue(sinkB.Calls == expected, "The second sink should write its backtrace");
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
    CheckFalse(flushedWithFailure, "Flush() should return false if any sink's flush fails");
    CheckTrue(flushed, "Flush() should return true once every sink's flush succeeds");
    CheckTrue(failingSink.Calls == twoFlushes, "Each Flush() should reach the failing sink");
    CheckTrue(sink.Calls == twoFlushes, "A sink's failed flush should not stop the other sinks from being flushed");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Flush_SucceedsWithNoSinks()
{
    // Arrange
    ASWLog::TASWMultiLog multiLog;

    // Act & Assert
    CheckTrue(multiLog.Flush(), "Flush() with no registered sinks should succeed, like IsOpen()");
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
    CheckTrue(flushed, "Flush() should succeed while the composite is disabled");
    CheckTrue(sink.Calls == std::vector<std::string>{ "Flush" }, "A disabled composite should still flush its sinks; disabling only stops new entries");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Flush_WritesBufferedEntriesOfEveryFileSink()
{
    // Arrange: FlushMode::Manual keeps each entry in the file's buffer until Flush()
    const auto fileA = TestTempDir / "flush_sink_a.log";
    const auto fileB = TestTempDir / "flush_sink_b.log";
    auto configA = MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace);
    auto configB = MakeFileConfig(TestTempDir, fileB, ASWLog::Level::Trace);
    configA.File.Flush = ASWLog::FlushMode::Manual;
    configB.File.Flush = ASWLog::FlushMode::Manual;

    ASWLog::TASWFileLog sinkA;
    ASWLog::TASWFileLog sinkB;
    CheckTrue(sinkA.Initialize(configA), "sinkA should initialize");
    CheckTrue(sinkB.Initialize(configB), "sinkB should initialize");

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
    CheckNotContains(contentsBeforeA, "buffered_entry", "Before Flush(), sinkA's entry should still be buffered");
    CheckNotContains(contentsBeforeB, "buffered_entry", "Before Flush(), sinkB's entry should still be buffered");
    CheckTrue(flushed, "Flush() should succeed");
    CheckContains(contentsAfterA, "buffered_entry", "Flush() should write sinkA's buffered entry to its file");
    CheckContains(contentsAfterB, "buffered_entry", "Flush() should write sinkB's buffered entry to its file");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_GetLoggerCount_ReflectsAddAndRemove()
{
    // Arrange
    const auto fileA = TestTempDir / "count_a.log";
    const auto fileB = TestTempDir / "count_b.log";

    ASWLog::TASWFileLog sinkA;
    ASWLog::TASWFileLog sinkB;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), "sinkA should initialize");
    CheckTrue(sinkB.Initialize(MakeFileConfig(TestTempDir, fileB, ASWLog::Level::Trace)), "sinkB should initialize");

    ASWLog::TASWMultiLog multiLog;

    // Act & Assert
    CheckEquals(0, multiLog.GetLoggerCount(), "A new composite should start with no registered sinks");

    multiLog.AddLogger(sinkA);
    CheckEquals(1, multiLog.GetLoggerCount(), "Count should increase after AddLogger");

    multiLog.AddLogger(sinkB);
    CheckEquals(2, multiLog.GetLoggerCount(), "Count should increase for each distinct sink");

    multiLog.AddLogger(sinkA); // Duplicate - should be a no-op
    CheckEquals(2, multiLog.GetLoggerCount(), "Duplicate AddLogger should not increase the count");

    multiLog.RemoveLogger(sinkA);
    CheckEquals(1, multiLog.GetLoggerCount(), "Count should decrease after RemoveLogger");

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
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), "sinkA should initialize");
    CheckTrue(sinkB.Initialize(MakeFileConfig(TestTempDir, fileB, ASWLog::Level::Trace)), "sinkB should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.AddLogger(sinkB);

    // Act
    const auto loggers = multiLog.GetLoggers();

    // Assert
    CheckEquals(2, loggers.size(), "GetLoggers should return one entry per registered sink");
    CheckTrue(std::find(loggers.begin(), loggers.end(), &sinkA) != loggers.end(), "GetLoggers should include sinkA");
    CheckTrue(std::find(loggers.begin(), loggers.end(), &sinkB) != loggers.end(), "GetLoggers should include sinkB");

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
    config.Startup.Banner.assign(64 * 1024, 'x');

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
    CheckEquals(roundCount, roundsWithOneSuccess, "Exactly one of the concurrent Initialize calls should succeed");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_IsOpen_RequiresAllSinksOpen()
{
    // Arrange
    const auto fileA = TestTempDir / "open_a.log";
    const auto fileB = TestTempDir / "open_b.log";

    ASWLog::TASWFileLog sinkA;
    ASWLog::TASWFileLog sinkB;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), "sinkA should initialize");
    CheckTrue(sinkB.Initialize(MakeFileConfig(TestTempDir, fileB, ASWLog::Level::Trace)), "sinkB should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.AddLogger(sinkB);

    // Act & Assert
    CheckTrue(multiLog.IsOpen(), "Composite should report open while every registered sink is open");

    sinkB.Close();

    CheckFalse(multiLog.IsOpen(), "Composite should report closed if any registered sink is closed");

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
    CheckEmpty(callsAtOffLevel, "A message at Off should not reach any sink, even when forced");
    CheckTrue(sink.Calls == std::vector<std::string>{ "LogForce:forced", "LogForceRaw:forced_raw" },
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
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), "sinkA should initialize");
    CheckTrue(sinkB.Initialize(MakeFileConfig(TestTempDir, fileB, ASWLog::Level::Trace)), "sinkB should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.AddLogger(sinkB);

    // Act
    multiLog.LogInfo("fanned_out_message");
    sinkA.Close();
    sinkB.Close();

    // Assert
    CheckContains(ReadFileText(fileA), "fanned_out_message", "sinkA should receive the fanned-out message");
    CheckContains(ReadFileText(fileB), "fanned_out_message", "sinkB should receive the fanned-out message");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Log_ThrowingSinkDoesNotStopOtherSinks()
{
    // Arrange: the throwing sink is registered first, so it's called before the file sink
    const auto file = TestTempDir / "after_throwing_sink.log";

    TThrowingLogger throwingSink;
    int reportCount = 0;
    ASWLog::TASWLogConfig throwingSinkConfig;
    throwingSinkConfig.ErrorReportInterval = std::chrono::milliseconds(0);
    throwingSinkConfig.OnError = [&reportCount](const ASWLog::TASWLogError& /*error*/) {
            ++reportCount;
        };
    throwingSink.Initialize(throwingSinkConfig);

    ASWLog::TASWFileLog fileSink;
    CheckTrue(fileSink.Initialize(MakeFileConfig(TestTempDir, file, ASWLog::Level::Trace)), "fileSink should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(throwingSink);
    multiLog.AddLogger(fileSink);

    // Act
    CheckNoThrow([&] {
            multiLog.LogInfo("log_message");
            multiLog.LogRaw(ASWLog::Level::Info, "raw_message\n");
            multiLog.LogForce(ASWLog::Level::Info, "force_message");
            multiLog.LogForceRaw(ASWLog::Level::Info, "force_raw_message\n");
        }, "An exception from one sink should not escape the composite");
    fileSink.Close();

    // Assert
    const auto contents = ReadFileText(file);
    CheckContains(contents, "log_message", "Log should still reach the other sinks");
    CheckContains(contents, "raw_message", "LogRaw should still reach the other sinks");
    CheckContains(contents, "force_message", "LogForce should still reach the other sinks");
    CheckContains(contents, "force_raw_message", "LogForceRaw should still reach the other sinks");
    CheckEquals(4, reportCount, "The failing sink should report each entry it dropped, through its own OnError");
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
    CheckEquals(0, countAfterFilteredEntry, "An entry no sink would write should not be formatted");
    CheckEquals(1, MultiLogFormatCount, "An entry should be formatted once, however many sinks get it");
    CheckTrue(sinkA.Calls == std::vector<std::string>{ "Log:counted" } && sinkB.Calls == sinkA.Calls,
        "Both sinks should get the formatted entry");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_LogForce_BypassesCompositeGate()
{
    // Arrange
    const auto fileA = TestTempDir / "force_sink.log";

    ASWLog::TASWFileLog sinkA;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), "sinkA should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.SetMinimumLevel(ASWLog::Level::Error);

    // Act
    multiLog.LogForce(ASWLog::Level::Info, "forced_past_composite_gate");
    sinkA.Close();

    // Assert
    CheckContains(ReadFileText(fileA), "forced_past_composite_gate", "LogForce should bypass the composite's own MinimumLevel gate");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_OnBeforeWrite_RunsOnceBeforeTheFanOut()
{
    // Arrange: the second sink has a hook of its own, set directly
    TRecordingLogger first;
    TRecordingLogger second;
    ASWLog::TASWLogConfig secondConfig;
    secondConfig.OnBeforeWrite = [](ASWLog::TASWPendingEntry& entry) {
            entry.SetMessage(std::format("{}+second", entry.GetRecord().Message));
            return true;
        };
    second.Reconfigure(secondConfig);

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(first);
    multiLog.AddLogger(second);
    int hookCount = 0;
    ASWLog::TASWLogConfig config;
    config.OnBeforeWrite = [&hookCount](ASWLog::TASWPendingEntry& entry) {
            ++hookCount;
            if (entry.GetRecord().Message == "dropped")
                return false;

            entry.SetMessage(std::format("multi:{}", entry.GetRecord().Message));
            return true;
        };
    CheckTrue(multiLog.Initialize(config), "Initialize should succeed");

    // Act
    multiLog.LogInfo("entry");
    multiLog.LogInfo("dropped");
    const bool reconfigured = multiLog.Reconfigure(config);
    multiLog.LogInfo("after");

    // Assert
    CheckEquals(3, hookCount, "The multi-log's hook should run once per entry, not once per sink");
    CheckTrue(first.WasInitialized && !first.InitializeHadBeforeWrite, "The config passed to a sink's Initialize() should have no hook");
    CheckTrue(reconfigured, "Reconfigure should succeed");
    CheckNull(first.GetConfig()->OnBeforeWrite, "The config passed to a sink's Reconfigure() should have no hook");
    CheckTrue(first.Calls == std::vector<std::string>{ "Log:multi:entry", "Reconfigure", "Log:multi:after" },
        "The sinks should get the entries as the multi-log's hook changed them, not the dropped one");
    CheckTrue(second.Calls == std::vector<std::string>{ "Reconfigure", "Log:multi:entry+second", "Reconfigure", "Log:multi:after" },
        "A sink's own hook should run after the multi-log's, until the multi-log's Reconfigure() replaces its config");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_Reconfigure_PassesConfigToEverySink()
{
    // Arrange: the first sink's Reconfigure() fails
    TRecordingLogger failingSink;
    failingSink.ReconfigureResult = false;
    TRecordingLogger sink;

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(failingSink);
    multiLog.AddLogger(sink);

    ASWLog::TASWLogConfig config;
    config.Startup.Banner = "reconfigured";

    // Act
    const bool reconfiguredBeforeInitialize = multiLog.Reconfigure(config);
    const auto callsBeforeInitialize = sink.Calls;
    multiLog.Initialize(ASWLog::TASWLogConfig{});
    const bool reconfiguredWithFailure = multiLog.Reconfigure(config);
    failingSink.ReconfigureResult = true;
    const bool reconfigured = multiLog.Reconfigure(config);

    // Assert
    const std::vector<std::string> twoReconfigures{ "Reconfigure", "Reconfigure" };
    CheckFalse(reconfiguredBeforeInitialize, "Reconfigure() should fail before Initialize()");
    CheckEmpty(callsBeforeInitialize, "Reconfigure() before Initialize() should not reach the sinks");
    CheckFalse(reconfiguredWithFailure, "Reconfigure() should return false if any sink's Reconfigure() fails");
    CheckTrue(reconfigured, "Reconfigure() should return true once every sink's Reconfigure() succeeds");
    CheckTrue(failingSink.Calls == twoReconfigures, "Each Reconfigure() should reach the failing sink");
    CheckTrue(sink.Calls == twoReconfigures, "A sink's failed Reconfigure() should not stop the other sinks");
    CheckEquals(std::string("reconfigured"), sink.GetConfig()->Startup.Banner, "Each sink should get the new config");
    CheckEquals(std::string("reconfigured"), multiLog.GetConfig()->Startup.Banner, "The multi-log should keep the new config");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_RemoveAllLoggers_ClearsRegistrationAndReturnsCount()
{
    // Arrange
    const auto fileA = TestTempDir / "remove_all_a.log";
    const auto fileB = TestTempDir / "remove_all_b.log";

    ASWLog::TASWFileLog sinkA;
    ASWLog::TASWFileLog sinkB;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), "sinkA should initialize");
    CheckTrue(sinkB.Initialize(MakeFileConfig(TestTempDir, fileB, ASWLog::Level::Trace)), "sinkB should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.AddLogger(sinkB);

    // Act
    const auto removedCount = multiLog.RemoveAllLoggers();
    multiLog.LogInfo("should_not_reach_any_sink");
    sinkA.Close();
    sinkB.Close();

    // Assert
    CheckEquals(2, removedCount, "RemoveAllLoggers should return the number of sinks that were registered");
    CheckEquals(0, multiLog.GetLoggerCount(), "No sinks should remain registered after RemoveAllLoggers");
    CheckNotContains(ReadFileText(fileA), "should_not_reach_any_sink", "sinkA should not receive entries after RemoveAllLoggers");
    CheckNotContains(ReadFileText(fileB), "should_not_reach_any_sink", "sinkB should not receive entries after RemoveAllLoggers");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_RemoveLogger_StopsReceivingEntries()
{
    // Arrange
    const auto fileA = TestTempDir / "remove_sink.log";

    ASWLog::TASWFileLog sinkA;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), "sinkA should initialize");

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
    CheckTrue(removed, "RemoveLogger should return true when the sink was registered");
    CheckFalse(removedAgain, "RemoveLogger should return false when the sink is no longer registered");
    CheckContains(contents, "before_removal", "Message logged before removal should reach the sink");
    CheckNotContains(contents, "after_removal", "Message logged after removal should not reach the sink");
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
    CheckEmpty(callsWhileDisabled, "A disabled composite should fan out nothing, not even forced entries");
    CheckTrue(sink.IsEnabled(), "Disabling the composite should not change its sinks");
    CheckTrue(sink.Calls == std::vector<std::string>{ "Log:enabled_again" }, "Fan-out should resume once enabled again");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MultiLog::Test_SetMinimumLevel_GatesFanOutBeforeSinks()
{
    // Arrange
    const auto fileA = TestTempDir / "gate_sink.log";

    ASWLog::TASWFileLog sinkA;
    CheckTrue(sinkA.Initialize(MakeFileConfig(TestTempDir, fileA, ASWLog::Level::Trace)), "sinkA should initialize");

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(sinkA);
    multiLog.SetMinimumLevel(ASWLog::Level::Error);

    // Act
    multiLog.LogInfo("blocked_by_composite_gate");
    multiLog.LogError("passes_composite_gate");
    sinkA.Close();

    const auto contents = ReadFileText(fileA);

    // Assert
    CheckNotContains(contents, "blocked_by_composite_gate", "Composite MinimumLevel should gate fan-out even though the sink's own level would allow it");
    CheckContains(contents, "passes_composite_gate", "Entries at or above the composite level should still reach the sink");
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
    CheckFalse(shouldLogWithoutSinks, "A composite with no sinks should log nothing");
    CheckFalse(shouldLogInfo, "No sink accepts Info");
    CheckTrue(shouldLogWarn, "One sink accepts Warn, which is enough");
    CheckFalse(shouldLogWarnWithWarnSinkDisabled, "A disabled sink should not count");
    CheckTrue(shouldLogErrorWithWarnSinkDisabled, "The enabled sink still accepts Error");
    CheckFalse(shouldLogErrorBelowCompositeLevel, "The composite's own minimum level should apply first");
    CheckFalse(shouldLogErrorWithCompositeDisabled, "A disabled composite should log nothing");
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
    CheckTrue(sinkA.Calls == std::vector<std::string>{ "Log:warn", "LogForceRaw:forced_raw" },
        "sinkA should get both entries, with their Raw and Forced flags");
    CheckTrue(sinkB.Calls == std::vector<std::string>{ "LogForceRaw:forced_raw" },
        "sinkB should apply its own level, so only the forced entry reaches it");

    std::vector<ASWLog::TASWLogRecord> records = sinkA.Records;
    records.insert(records.end(), sinkB.Records.begin(), sinkB.Records.end());
    for (const auto& record : records)
    {
        CheckTrue(record.Timestamp == multiLog.CurrentTime, "Each sink should keep the composite's stamp, not read its own clock");
        CheckEquals(static_cast<int64_t>(ASWLog::GetCurrentOSThreadId()), static_cast<int64_t>(record.ThreadId), "The record should carry the logging thread's id");
    }
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_MultiLog)
