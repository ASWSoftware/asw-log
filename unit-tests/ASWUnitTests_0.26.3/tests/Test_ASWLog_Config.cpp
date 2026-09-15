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
#include <filesystem>
//---------------------------------------------------------------------------
#include "ASWLog_Config.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

//---------------------------------------------------------------------------
TTest_ASWLog_Config::TTest_ASWLog_Config()
    : inherited("ASWLog_Config_Tests")
{
    RegisterTest(&TTest_ASWLog_Config::Test_ResolveLogFileDir_CustomFolder, "ResolveLogFileDir_CustomFolder");
    RegisterTest(&TTest_ASWLog_Config::Test_ResolveLogFilePath_AbsolutePath, "ResolveLogFilePath_AbsolutePath");
    RegisterTest(&TTest_ASWLog_Config::Test_ResolveLogFilePath_Defaults, "ResolveLogFilePath_Defaults");
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
void TTest_ASWLog_Config::Test_ResolveLogFileDir_CustomFolder()
{
    // Arrange
    const auto expectedDir = std::filesystem::path("custom/logs");

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = std::filesystem::path("custom\\logs");
    config.LogFilePath = std::filesystem::path("app.log");

    // Act
    const auto resolvedDir = config.ResolveLogFileDir();

    // Assert
    CheckTrue(resolvedDir == expectedDir, __func__, __LINE__, "ResolveLogFileDir should use the configured logs folder");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ResolveLogFilePath_AbsolutePath()
{
    // Arrange
    const auto absolutePath = std::filesystem::absolute(std::filesystem::path("custom.log")).lexically_normal();

    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = std::filesystem::path("logs");
    config.LogFilePath = absolutePath;

    // Act
    const auto resolvedPath = config.ResolveLogFilePath();

    // Assert
    CheckTrue(resolvedPath == absolutePath, __func__, __LINE__, "Absolute log file paths should be preserved");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ResolveLogFilePath_Defaults()
{
    // Arrange
    const auto expectedPath = (std::filesystem::path("logs") / std::filesystem::path("aswlog.txt")).lexically_normal();

    ASWLog::TASWLogConfig config;

    // Act
    const auto resolvedPath = config.ResolveLogFilePath();

    // Assert
    CheckTrue(resolvedPath == expectedPath, __func__, __LINE__, "Resolved path should default to logs/aswlog.txt");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests
