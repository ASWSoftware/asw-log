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
#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

//---------------------------------------------------------------------------
TTest_ASWLog_Utils::TTest_ASWLog_Utils()
    : inherited("ASWLog_Utils_Tests")
{
    RegisterTest(&TTest_ASWLog_Utils::Test_GenerateLogFileName_ContainsExpectedFields, "GenerateLogFileName_ContainsExpectedFields");
    RegisterTest(&TTest_ASWLog_Utils::Test_GenerateLogFileName_PrefixAndPostfixAreOptional, "GenerateLogFileName_PrefixAndPostfixAreOptional");
    RegisterTest(&TTest_ASWLog_Utils::Test_GetOSInfoString_ContainsEdition, "GetOSInfoString_ContainsEdition");
    RegisterTest(&TTest_ASWLog_Utils::Test_GetWindowsEditionName_ProductTypes, "GetWindowsEditionName_ProductTypes");
    RegisterTest(&TTest_ASWLog_Utils::Test_IsRootFolder_DetectsRootFolders, "IsRootFolder_DetectsRootFolders");
    RegisterTest(&TTest_ASWLog_Utils::Test_IsRootFolder_ResolvesRelativePaths, "IsRootFolder_ResolvesRelativePaths");
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
    const std::string logName = ASWLog::GenerateLogFileName("", "ExampleLog.txt");

    // Act & Assert
    AssertTrue(!logName.empty(), __func__, __LINE__, "Generated log file name should not be empty");

    CheckTrue(logName.find("_PID") != std::string::npos, __func__, __LINE__, "Generated file name should include process id");
    CheckTrue(logName.find("_TID") != std::string::npos, __func__, __LINE__, "Generated file name should include thread id");
    CheckTrue(logName.find("ExampleLog.txt") != std::string::npos, __func__, __LINE__, "Generated file name should include the custom postfix");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_GenerateLogFileName_PrefixAndPostfixAreOptional()
{
    // Arrange & Act
    const std::string withBoth = ASWLog::GenerateLogFileName("MyApp", "ExampleLog.txt");
    const std::string prefixOnly = ASWLog::GenerateLogFileName("MyApp", "");
    const std::string postfixOnly = ASWLog::GenerateLogFileName("", "ExampleLog.txt");
    const std::string neither = ASWLog::GenerateLogFileName("", "");

    // Assert
    CheckTrue(withBoth.starts_with("MyApp_"), __func__, __LINE__, "A non-empty prefix should appear at the very start of the name");
    CheckTrue(withBoth.find("ExampleLog.txt") != std::string::npos, __func__, __LINE__, "The postfix should still be included alongside a prefix");
    CheckTrue(withBoth.find("__") == std::string::npos, __func__, __LINE__, "Prefix and postfix should not introduce a doubled separator");

    CheckTrue(prefixOnly.starts_with("MyApp_"), __func__, __LINE__, "The prefix should appear even when the postfix is empty");
    CheckFalse(prefixOnly.ends_with("_"), __func__, __LINE__, "An empty postfix should not leave a trailing separator");

    CheckFalse(postfixOnly.starts_with("_"), __func__, __LINE__, "An empty prefix should not leave a leading separator");
    CheckTrue(postfixOnly.find("ExampleLog.txt") != std::string::npos, __func__, __LINE__, "The postfix should still be included when the prefix is empty");

    CheckFalse(neither.empty(), __func__, __LINE__, "The name should still contain the timestamp/PID/TID segments when both are empty");
    CheckFalse(neither.starts_with("_"), __func__, __LINE__, "An empty prefix should not leave a leading separator when the postfix is also empty");
    CheckFalse(neither.ends_with("_"), __func__, __LINE__, "An empty postfix should not leave a trailing separator when the prefix is also empty");
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
void TTest_ASWLog_Utils::Test_GetWindowsEditionName_ProductTypes()
{
#if defined(_WIN32)
    // Act & Assert
    CheckEquals(std::string("Home"), ASWLog::GetWindowsEditionName(PRODUCT_CORE, false), __func__, __LINE__,
        "Windows 10/11 Home should be named");
    CheckEquals(std::string("Pro"), ASWLog::GetWindowsEditionName(PRODUCT_PROFESSIONAL, false), __func__, __LINE__,
        "Pro should be named");
    CheckEquals(std::string("Server Standard"), ASWLog::GetWindowsEditionName(PRODUCT_STANDARD_SERVER, true),
        __func__, __LINE__, "Server Standard should be named");
    CheckEquals(std::string("Server Datacenter"), ASWLog::GetWindowsEditionName(PRODUCT_DATACENTER_SERVER, true),
        __func__, __LINE__, "Server Datacenter (e.g. GitHub's Windows runners) should be named");
    CheckEquals(std::string("Server (product type 0xABCD)"), ASWLog::GetWindowsEditionName(0xABCD, true), __func__,
        __LINE__, "An unlisted server product type should still say Server");
    CheckEquals(std::string("Unknown (product type 0xABCD)"), ASWLog::GetWindowsEditionName(0xABCD, false), __func__,
        __LINE__, "An unlisted workstation product type should be Unknown");
#else
    Skip(__func__, __LINE__, "GetWindowsEditionName() is Windows-only");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_IsRootFolder_DetectsRootFolders()
{
    // Arrange
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto root = tempDir.root_path();
    const auto firstFolder = root / *tempDir.relative_path().begin(); // e.g. "C:\Users" or "/tmp"

    // Act & Assert
    CheckTrue(ASWLog::IsRootFolder(root), __func__, __LINE__, "The root of the temp folder's path should be a root folder");
    CheckTrue(ASWLog::IsRootFolder(root / "."), __func__, __LINE__, "The root followed by \".\" should be a root folder");
    CheckTrue(ASWLog::IsRootFolder(firstFolder / ".."), __func__, __LINE__, "A folder's \"..\" at the top level should be a root folder");
    CheckFalse(ASWLog::IsRootFolder(firstFolder), __func__, __LINE__, "A top-level folder should not be a root folder");
    CheckFalse(ASWLog::IsRootFolder(tempDir), __func__, __LINE__, "The temp folder should not be a root folder");
    CheckTrue(ASWLog::IsRootFolder(std::filesystem::path()), __func__, __LINE__, "An empty path can't be checked, so it should count as a root folder");

#if defined(_WIN32)
    const auto driveLetter = tempDir.root_name().string().substr(0, 1);
    CheckTrue(ASWLog::IsRootFolder(driveLetter + ":/"), __func__, __LINE__, "A drive root written with '/' should be a root folder");

    // The drive's administrative share (e.g. "\\localhost\C$\"), when this machine allows it. Forward slashes, since
    // Windows accepts them in UNC paths too.
    const std::filesystem::path share = "//localhost/" + driveLetter + "$/";
    std::error_code errorCode;
    if (std::filesystem::is_directory(share / tempDir.relative_path(), errorCode))
    {
        CheckTrue(ASWLog::IsRootFolder(share), __func__, __LINE__, "A network share's root should be a root folder");
        CheckFalse(ASWLog::IsRootFolder(share / tempDir.relative_path()), __func__, __LINE__, "A folder in a network share should not be a root folder");
    }
    else
    {
        Log("  " + share.string() + " isn't available, so the network share checks were skipped");
    }
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_IsRootFolder_ResolvesRelativePaths()
{
    // Arrange: a relative path from the current folder up to its root, e.g. "..\..\.."
    const auto currentPath = std::filesystem::current_path();
    const auto relativeRoot = std::filesystem::relative(currentPath.root_path(), currentPath);

    // Act & Assert
    CheckFalse(relativeRoot.empty(), __func__, __LINE__, "The relative path to the root should be found");
    CheckTrue(ASWLog::IsRootFolder(relativeRoot), __func__, __LINE__, "A relative path that leads to a root should be a root folder");
    if (currentPath != currentPath.root_path())
        CheckFalse(ASWLog::IsRootFolder("."), __func__, __LINE__, "\".\" should not be a root folder when the current folder isn't one");
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
    CheckTrue(ASWLog::MatchesWildcard("no_extension", "*"), __func__, __LINE__, "A lone '*' should match any name, with or without a dot");
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

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_Utils)
