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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
//---------------------------------------------------------------------------
#include "ASWLog_FileLog.h"
#include "ASWLog_MultiLog.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

const auto GroupBaseTempDir = std::filesystem::temp_directory_path() / "aswlog_multilog_tests";
const auto TestTempDir = GroupBaseTempDir / "test";

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
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Contains_ReflectsRegistrationState, "Contains_ReflectsRegistrationState");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_GetLoggerCount_ReflectsAddAndRemove, "GetLoggerCount_ReflectsAddAndRemove");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_GetLoggers_ReturnsSnapshotOfRegisteredSinks, "GetLoggers_ReturnsSnapshotOfRegisteredSinks");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_IsOpen_RequiresAllSinksOpen, "IsOpen_RequiresAllSinksOpen");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_Log_FansOutToAllRegisteredSinks, "Log_FansOutToAllRegisteredSinks");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_LogForce_BypassesCompositeGate, "LogForce_BypassesCompositeGate");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_RemoveAllLoggers_ClearsRegistrationAndReturnsCount, "RemoveAllLoggers_ClearsRegistrationAndReturnsCount");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_RemoveLogger_StopsReceivingEntries, "RemoveLogger_StopsReceivingEntries");
    RegisterTest(&TTest_ASWLog_MultiLog::Test_SetMinimumLevel_GatesFanOutBeforeSinks, "SetMinimumLevel_GatesFanOutBeforeSinks");
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

} // namespace ASWUnitTests
