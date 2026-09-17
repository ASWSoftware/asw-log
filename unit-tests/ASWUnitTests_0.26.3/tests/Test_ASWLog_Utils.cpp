/* **************************************************************************
Test_ASWLog_Utils.cpp
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
#include "Test_ASWLog_Utils.h"
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------
#include <chrono>
#include <string>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

//---------------------------------------------------------------------------
TTest_ASWLog_Utils::TTest_ASWLog_Utils()
    : inherited("ASWLog_Utils_Tests")
{
    RegisterTest(&TTest_ASWLog_Utils::Test_GenerateLogFileName_ContainsExpectedFields, "GenerateLogFileName_ContainsExpectedFields");
    RegisterTest(&TTest_ASWLog_Utils::Test_GetOSInfoString_ContainsEdition, "GetOSInfoString_ContainsEdition");
    RegisterTest(&TTest_ASWLog_Utils::Test_MatchesWildcard_Patterns, "MatchesWildcard_Patterns");
    RegisterTest(&TTest_ASWLog_Utils::Test_Time_ToDateString, "Time_ToDateString");
    RegisterTest(&TTest_ASWLog_Utils::Test_Time_ToISO8601String, "Time_ToISO8601String");
}
//---------------------------------------------------------------------------
TTest_ASWLog_Utils::~TTest_ASWLog_Utils()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_GenerateLogFileName_ContainsExpectedFields()
{
    // Arrange
    const std::string logName = ASWLog::GenerateLogFileName("ExampleLog.txt");

    // Act & Assert
    AssertTrue(!logName.empty(), __func__, __LINE__, "Generated log file name should not be empty");

    CheckTrue(logName.find("_PID") != std::string::npos, __func__, __LINE__, "Generated file name should include process id");
    CheckTrue(logName.find("_TID") != std::string::npos, __func__, __LINE__, "Generated file name should include thread id");
    CheckTrue(logName.find("ExampleLog.txt") != std::string::npos, __func__, __LINE__, "Generated file name should include the custom postfix");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_GetOSInfoString_ContainsEdition()
{
    // Arrange
    const std::string osInfo = ASWLog::GetOSInfoString();

    // Act & Assert
#if defined(_WIN32)
    const bool hasEdition = osInfo.find("Home") != std::string::npos ||
        osInfo.find("Pro") != std::string::npos ||
        osInfo.find("Enterprise") != std::string::npos ||
        osInfo.find("Education") != std::string::npos ||
        osInfo.find("Business") != std::string::npos ||
        osInfo.find("Server") != std::string::npos ||
        osInfo.find("Ultimate") != std::string::npos;

    CheckTrue(!osInfo.empty(), __func__, __LINE__, "OS info string should not be empty");
    CheckTrue(hasEdition, __func__, __LINE__, "Windows OS info should include the edition name (Home, Pro, Enterprise, etc.)");
#else
    CheckTrue(!osInfo.empty(), __func__, __LINE__, "OS info string should not be empty");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_MatchesWildcard_Patterns()
{
    // Arrange
    const std::string fileName = "app_123.log";
    const std::string fileName2 = "notes.txt";

    // Act & Assert
    CheckTrue(ASWLog::MatchesWildcard(fileName, "*.log"), __func__, __LINE__, "Wildcard should match a suffix");
    CheckTrue(ASWLog::MatchesWildcard(fileName, "app_*.log"), __func__, __LINE__, "Wildcard should match mid-string patterns");
    CheckTrue(ASWLog::MatchesWildcard(fileName2, "n?tes.*"), __func__, __LINE__, "Question mark wildcard should match a single character");
    CheckFalse(ASWLog::MatchesWildcard(fileName, "*.txt"), __func__, __LINE__, "Wildcard should reject non-matching files");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_Time_ToDateString()
{
    // Arrange
    const auto now = std::chrono::system_clock::now();

    // Act
    const std::string date = ASWLog::Time::ToDateString(now);

    // Assert
    CheckTrue(date.size() >= 10, __func__, __LINE__, "Date string should have a YYYY-MM-DD format");
    CheckTrue(date.find('-') != std::string::npos, __func__, __LINE__, "Date string should include date separators");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_Time_ToISO8601String()
{
    // Arrange
    const auto now = std::chrono::system_clock::now();

    // Act
    const std::string iso = ASWLog::Time::ToISO8601String(now);

    // Assert
    CheckTrue(iso.find('T') != std::string::npos, __func__, __LINE__, "ISO string should include the T separator");
    CheckTrue(iso.find('Z') == iso.size() - 1, __func__, __LINE__, "ISO string should end with Z");
    CheckTrue(iso.size() >= 24, __func__, __LINE__, "ISO string should include date and time");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests
