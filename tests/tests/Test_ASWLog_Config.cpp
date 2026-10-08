/* **************************************************************************
Test_ASWLog_Config.cpp
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
#include "Test_ASWLog_Config.h"
//---------------------------------------------------------------------------
#include <chrono>
#include <cstdint>
#include <filesystem>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_Config.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

//---------------------------------------------------------------------------
TTest_ASWLog_Config::TTest_ASWLog_Config()
    : inherited("ASWLog_Config_Tests")
{
    RegisterTest(&TTest_ASWLog_Config::Test_Defaults_MatchDocumentedValues, "Defaults_MatchDocumentedValues");
    RegisterTest(&TTest_ASWLog_Config::Test_ResolveFolder_CustomFolder, "ResolveFolder_CustomFolder");
    RegisterTest(&TTest_ASWLog_Config::Test_ResolvePath_AbsolutePath, "ResolvePath_AbsolutePath");
    RegisterTest(&TTest_ASWLog_Config::Test_ResolvePath_Defaults, "ResolvePath_Defaults");
}
//---------------------------------------------------------------------------
TTest_ASWLog_Config::~TTest_ASWLog_Config()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_Defaults_MatchDocumentedValues()
{
    // Arrange / Act
    const ASWLog::TASWLogConfig config;

    // Assert: top level
    CheckEquals(ASWLog::Level::Info, config.InitialMinimumLevel, "InitialMinimumLevel should default to Info");
    CheckNull(config.OnLogEntry, "OnLogEntry should default to unset");
    CheckEquals(ASWLog::Level::Error, config.OnLogEntryMinimumLevel, "OnLogEntryMinimumLevel should default to Error");
    CheckNull(config.OnError, "OnError should default to unset (reports go to stderr)");
    CheckFalse(config.Async.Enabled, "Async.Enabled should default to false");
    CheckEquals(8192, config.Async.QueueCapacity, "Async.QueueCapacity should default to 8192");
    CheckEquals(ASWLog::AsyncOverflowPolicy::Block, config.Async.OverflowPolicy, "Async.OverflowPolicy should default to Block");
    CheckEquals(ASWLog::Level::Error, config.Async.WaitAtLevel, "Async.WaitAtLevel should default to Error");
    CheckTrue(config.ErrorReportInterval == std::chrono::minutes(1), "ErrorReportInterval should default to 1 minute");

    // Assert: Line
    CheckNull(config.Line.Formatter, "Line.Formatter should default to unset (the built-in layout)");
    CheckEquals(ASWLog::LineEnding::LF, config.Line.Ending, "Line.Ending should default to LF");
    CheckEquals(ASWLog::TimeZone::UTC, config.Line.TimestampZone, "Line.TimestampZone should default to UTC");
    CheckEquals(ASWLog::TimePrecision::Milliseconds, config.Line.TimestampPrecision, "Line.TimestampPrecision should default to Milliseconds");
    CheckTrue(config.Line.ShowTimestamp, "Line.ShowTimestamp should default to true");
    CheckTrue(config.Line.ShowLevel, "Line.ShowLevel should default to true");
    CheckTrue(config.Line.ShowCategory, "Line.ShowCategory should default to true");
    CheckTrue(config.Line.ShowProcessId, "Line.ShowProcessId should default to true");
    CheckTrue(config.Line.ShowThreadId, "Line.ShowThreadId should default to true");
    CheckFalse(config.Line.ShowWorkingSet, "Line.ShowWorkingSet should default to false");
    CheckFalse(config.Line.ShowPeakWorkingSet, "Line.ShowPeakWorkingSet should default to false");
    CheckFalse(config.Line.ShowFunctionName, "Line.ShowFunctionName should default to false");
    CheckFalse(config.Line.ShowSourceLine, "Line.ShowSourceLine should default to false");

    // Assert: Startup
    CheckEmpty(config.Startup.Banner, "Startup.Banner should default to empty");
    CheckTrue(config.Startup.WriteApplicationInfo, "Startup.WriteApplicationInfo should default to true");
    CheckFalse(config.Startup.WriteCommandLine, "Startup.WriteCommandLine should default to false");
    CheckTrue(config.Startup.WriteDriveInfo, "Startup.WriteDriveInfo should default to true");
    CheckTrue(config.Startup.WriteMemoryUsage, "Startup.WriteMemoryUsage should default to true");
    CheckTrue(config.Startup.WriteOSInfo, "Startup.WriteOSInfo should default to true");
    CheckTrue(config.Startup.WriteSystemMemoryInfo, "Startup.WriteSystemMemoryInfo should default to true");
    CheckTrue(config.Startup.WriteTimeInfo, "Startup.WriteTimeInfo should default to true");

    // Assert: Shutdown
    CheckTrue(config.Shutdown.WriteLine, "Shutdown.WriteLine should default to true");
    CheckEmpty(config.Shutdown.Banner, "Shutdown.Banner should default to empty");
    CheckTrue(config.Shutdown.WriteCrashLine, "Shutdown.WriteCrashLine should default to true");

    // Assert: Backtrace
    CheckEquals(0, config.Backtrace.Capacity, "Backtrace.Capacity should default to 0 (off)");
    CheckEquals(ASWLog::Level::Trace, config.Backtrace.LowestLevel, "Backtrace.LowestLevel should default to Trace");
    CheckEquals(ASWLog::Level::Error, config.Backtrace.DumpAtLevel, "Backtrace.DumpAtLevel should default to Error");

    // Assert: File
    CheckTrue(config.File.FolderPath == std::filesystem::path("logs"), "File.FolderPath should default to logs");
    CheckTrue(config.File.FilePath == std::filesystem::path("aswlog.txt"), "File.FilePath should default to aswlog.txt");
    CheckFalse(config.File.AutoOpenClosePerWrite, "File.AutoOpenClosePerWrite should default to false");
    CheckEquals(ASWLog::FlushMode::EveryWrite, config.File.Flush, "File.Flush should default to EveryWrite");
    CheckTrue(config.File.FlushInterval == std::chrono::milliseconds(1000), "File.FlushInterval should default to 1000 ms");
    CheckEquals(ASWLog::Level::Error, config.File.FlushImmediatelyAtLevel, "File.FlushImmediatelyAtLevel should default to Error");
    CheckEquals(ASWLog::Level::Off, config.File.SyncToDiskAtLevel, "File.SyncToDiskAtLevel should default to Off");
    CheckEquals(5, config.File.OpenRetryCount, "File.OpenRetryCount should default to 5");
    CheckTrue(config.File.OpenRetryDelay == std::chrono::milliseconds(50), "File.OpenRetryDelay should default to 50 ms");
    CheckTrue(config.File.CircuitBreakerResetDelay == std::chrono::milliseconds(500),
        "File.CircuitBreakerResetDelay should default to 500 ms");
    CheckFalse(config.File.EnableRotation, "File.EnableRotation should default to false");
    CheckTrue(config.File.MaxFileSizeBytes == static_cast<std::uintmax_t>(10 * 1024 * 1024), "File.MaxFileSizeBytes should default to 10 MB");
    CheckTrue(config.File.RotationRetryDelay == std::chrono::milliseconds(500),
        "File.RotationRetryDelay should default to 500 ms");
    CheckFalse(config.File.EnableDailyRolling, "File.EnableDailyRolling should default to false");
    CheckTrue(config.File.RetentionMaxAge == std::chrono::hours(0), "File.RetentionMaxAge should default to 0 (disabled)");
    CheckNull(config.File.OnBackupCreated, "File.OnBackupCreated should default to unset");
    CheckEquals(0, config.File.MaxBackupFiles, "File.MaxBackupFiles should default to 0 (unlimited)");
    CheckEquals(0, config.File.MaxBackupTotalBytes, "File.MaxBackupTotalBytes should default to 0 (unlimited)");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ResolveFolder_CustomFolder()
{
    // Arrange
    const auto expectedDir = std::filesystem::path("custom") / "logs";

    ASWLog::TASWFileConfig config;
    config.FolderPath = std::filesystem::path("custom/logs");
    config.FilePath = std::filesystem::path("app.log");

    // Act
    const auto resolvedDir = config.ResolveFolder();

    // Assert
    CheckTrue(resolvedDir == expectedDir, "ResolveFolder should use the configured logs folder");

#if defined(_WIN32)
    // Backslash is a separator only on Windows; on POSIX it's an ordinary file name character.
    config.FolderPath = std::filesystem::path("custom\\logs");
    CheckTrue(config.ResolveFolder() == expectedDir,
        "ResolveFolder should accept a backslash-separated logs folder on Windows");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ResolvePath_AbsolutePath()
{
    // Arrange
    const auto absolutePath = std::filesystem::absolute(std::filesystem::path("custom.log")).lexically_normal();

    ASWLog::TASWFileConfig config;
    config.FolderPath = std::filesystem::path("logs");
    config.FilePath = absolutePath;

    // Act
    const auto resolvedPath = config.ResolvePath();

    // Assert
    CheckTrue(resolvedPath == absolutePath, "Absolute log file paths should be preserved");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ResolvePath_Defaults()
{
    // Arrange
    const auto expectedPath = (std::filesystem::path("logs") / std::filesystem::path("aswlog.txt")).lexically_normal();

    ASWLog::TASWFileConfig config;

    // Act
    const auto resolvedPath = config.ResolvePath();

    // Assert
    CheckTrue(resolvedPath == expectedPath, "Resolved path should default to logs/aswlog.txt");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_Config)
