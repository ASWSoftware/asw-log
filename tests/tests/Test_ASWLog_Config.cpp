/* **************************************************************************
Test_ASWLog_Config.cpp
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
    CheckTrue(config.InitialMinimumLevel == ASWLog::Level::Info, __func__, __LINE__, "InitialMinimumLevel should default to Info");
    CheckTrue(config.OnLogEntry == nullptr, __func__, __LINE__, "OnLogEntry should default to unset");
    CheckTrue(config.OnLogEntryMinimumLevel == ASWLog::Level::Error, __func__, __LINE__, "OnLogEntryMinimumLevel should default to Error");
    CheckTrue(config.OnError == nullptr, __func__, __LINE__, "OnError should default to unset (reports go to stderr)");
    CheckTrue(config.ErrorReportInterval == std::chrono::minutes(1), __func__, __LINE__, "ErrorReportInterval should default to 1 minute");

    // Assert: Line
    CheckTrue(config.Line.Formatter == nullptr, __func__, __LINE__, "Line.Formatter should default to unset (the built-in layout)");
    CheckTrue(config.Line.Ending == ASWLog::LineEnding::LF, __func__, __LINE__, "Line.Ending should default to LF");
    CheckTrue(config.Line.ShowTimestamp, __func__, __LINE__, "Line.ShowTimestamp should default to true");
    CheckTrue(config.Line.ShowLevel, __func__, __LINE__, "Line.ShowLevel should default to true");
    CheckTrue(config.Line.ShowProcessId, __func__, __LINE__, "Line.ShowProcessId should default to true");
    CheckTrue(config.Line.ShowThreadId, __func__, __LINE__, "Line.ShowThreadId should default to true");
    CheckFalse(config.Line.ShowWorkingSet, __func__, __LINE__, "Line.ShowWorkingSet should default to false");
    CheckFalse(config.Line.ShowPeakWorkingSet, __func__, __LINE__, "Line.ShowPeakWorkingSet should default to false");
    CheckFalse(config.Line.ShowFunctionName, __func__, __LINE__, "Line.ShowFunctionName should default to false");
    CheckFalse(config.Line.ShowSourceLine, __func__, __LINE__, "Line.ShowSourceLine should default to false");

    // Assert: Startup
    CheckTrue(config.Startup.Banner.empty(), __func__, __LINE__, "Startup.Banner should default to empty");
    CheckTrue(config.Startup.WriteApplicationInfo, __func__, __LINE__, "Startup.WriteApplicationInfo should default to true");
    CheckFalse(config.Startup.WriteCommandLine, __func__, __LINE__, "Startup.WriteCommandLine should default to false");
    CheckTrue(config.Startup.WriteDriveInfo, __func__, __LINE__, "Startup.WriteDriveInfo should default to true");
    CheckTrue(config.Startup.WriteMemoryUsage, __func__, __LINE__, "Startup.WriteMemoryUsage should default to true");
    CheckTrue(config.Startup.WriteOSInfo, __func__, __LINE__, "Startup.WriteOSInfo should default to true");
    CheckTrue(config.Startup.WriteSystemMemoryInfo, __func__, __LINE__, "Startup.WriteSystemMemoryInfo should default to true");
    CheckTrue(config.Startup.WriteTimeInfo, __func__, __LINE__, "Startup.WriteTimeInfo should default to true");

    // Assert: Shutdown
    CheckTrue(config.Shutdown.WriteLine, __func__, __LINE__, "Shutdown.WriteLine should default to true");
    CheckTrue(config.Shutdown.Banner.empty(), __func__, __LINE__, "Shutdown.Banner should default to empty");

    // Assert: File
    CheckTrue(config.File.FolderPath == std::filesystem::path("logs"), __func__, __LINE__, "File.FolderPath should default to logs");
    CheckTrue(config.File.FilePath == std::filesystem::path("aswlog.txt"), __func__, __LINE__, "File.FilePath should default to aswlog.txt");
    CheckFalse(config.File.AutoOpenClosePerWrite, __func__, __LINE__, "File.AutoOpenClosePerWrite should default to false");
    CheckTrue(config.File.Flush == ASWLog::FlushMode::EveryWrite, __func__, __LINE__, "File.Flush should default to EveryWrite");
    CheckTrue(config.File.FlushInterval == std::chrono::milliseconds(1000), __func__, __LINE__, "File.FlushInterval should default to 1000 ms");
    CheckEquals(5, config.File.OpenRetryCount, __func__, __LINE__, "File.OpenRetryCount should default to 5");
    CheckTrue(config.File.OpenRetryDelay == std::chrono::milliseconds(50), __func__, __LINE__, "File.OpenRetryDelay should default to 50 ms");
    CheckTrue(config.File.CircuitBreakerResetDelay == std::chrono::milliseconds(500), __func__, __LINE__,
        "File.CircuitBreakerResetDelay should default to 500 ms");
    CheckFalse(config.File.EnableRotation, __func__, __LINE__, "File.EnableRotation should default to false");
    CheckTrue(config.File.MaxFileSizeBytes == static_cast<std::uintmax_t>(10 * 1024 * 1024), __func__, __LINE__, "File.MaxFileSizeBytes should default to 10 MB");
    CheckTrue(config.File.RotationRetryDelay == std::chrono::milliseconds(500), __func__, __LINE__,
        "File.RotationRetryDelay should default to 500 ms");
    CheckFalse(config.File.EnableDailyRolling, __func__, __LINE__, "File.EnableDailyRolling should default to false");
    CheckTrue(config.File.RetentionMaxAge == std::chrono::hours(0), __func__, __LINE__, "File.RetentionMaxAge should default to 0 (disabled)");
    CheckEquals(static_cast<std::size_t>(0), config.File.MaxBackupFiles, __func__, __LINE__, "File.MaxBackupFiles should default to 0 (unlimited)");
    CheckEquals(static_cast<std::uintmax_t>(0), config.File.MaxBackupTotalBytes, __func__, __LINE__, "File.MaxBackupTotalBytes should default to 0 (unlimited)");
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
    CheckTrue(resolvedDir == expectedDir, __func__, __LINE__, "ResolveFolder should use the configured logs folder");

#if defined(_WIN32)
    // Backslash is a separator only on Windows; on POSIX it's an ordinary file name character.
    config.FolderPath = std::filesystem::path("custom\\logs");
    CheckTrue(config.ResolveFolder() == expectedDir, __func__, __LINE__,
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
    CheckTrue(resolvedPath == absolutePath, __func__, __LINE__, "Absolute log file paths should be preserved");
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
    CheckTrue(resolvedPath == expectedPath, __func__, __LINE__, "Resolved path should default to logs/aswlog.txt");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_Config)
