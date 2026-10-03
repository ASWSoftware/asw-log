/* **************************************************************************
Test_ASWLog_Version.cpp
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
#include "Test_ASWLog_Version.h"
//---------------------------------------------------------------------------
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_ConsoleLog.h"
#include "ASWLog_Version.h"
//---------------------------------------------------------------------------

// The constants are defined from the macros, and both are usable at compile time
static_assert(ASWLog::VersionMajor == ASWLOG_VERSION_MAJOR);
static_assert(ASWLog::VersionMinor == ASWLOG_VERSION_MINOR);
static_assert(ASWLog::VersionPatch == ASWLOG_VERSION_PATCH);
static_assert(std::string_view(ASWLog::VersionPreRelease) == ASWLOG_VERSION_PRERELEASE);
static_assert(std::string_view(ASWLog::Version) == ASWLOG_VERSION_STRING);
#if ASWLOG_VERSION_MAJOR < 0 || ASWLOG_VERSION_MINOR < 0 || ASWLOG_VERSION_PATCH < 0
#error "The ASWLog version macros must be usable in #if"
#endif

namespace ASWUnitTests
{

namespace
{

// A SemVer pre-release identifier: not empty, only [0-9A-Za-z-], and no leading zero if it is all digits
bool IsValidPreReleaseIdentifier(std::string_view identifier)
{
    if (identifier.empty())
        return false;

    bool isNumeric = true;
    for (const char character : identifier)
    {
        const bool isDigit = character >= '0' && character <= '9';
        const bool isLetter = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        if (!isDigit && !isLetter && character != '-')
            return false;

        isNumeric = isNumeric && isDigit;
    }

    return !isNumeric || identifier.size() == 1 || identifier.front() != '0';
}

} // namespace

//---------------------------------------------------------------------------
TTest_ASWLog_Version::TTest_ASWLog_Version()
    : inherited("ASWLog_Version_Tests")
{
    RegisterTest(&TTest_ASWLog_Version::Test_GetVersionStr_ReturnsVersion, "GetVersionStr_ReturnsVersion");
    RegisterTest(&TTest_ASWLog_Version::Test_PreRelease_IsValidSemVer, "PreRelease_IsValidSemVer");
    RegisterTest(&TTest_ASWLog_Version::Test_VersionString_MatchesParts, "VersionString_MatchesParts");
}
//---------------------------------------------------------------------------
TTest_ASWLog_Version::~TTest_ASWLog_Version()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Version::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Version::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Version::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Version::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_Version::Test_GetVersionStr_ReturnsVersion()
{
    // Arrange
    ASWLog::TASWConsoleLog consoleLog;
    const ASWLog::IASWLog& logger = consoleLog;

    // Act
    const auto version = logger.GetVersionStr();

    // Assert
    CheckEquals(std::string(ASWLOG_VERSION_STRING), std::string(version), __func__, __LINE__,
        "GetVersionStr() should return ASWLOG_VERSION_STRING");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Version::Test_PreRelease_IsValidSemVer()
{
    // Arrange: a pre-release is empty (a release) or dot-separated identifiers, e.g. "dev.1"
    const std::string_view preRelease = ASWLog::VersionPreRelease;

    // Act
    bool isValid = true;
    for (std::size_t start = 0; !preRelease.empty() && start <= preRelease.size();)
    {
        const auto dot = preRelease.find('.', start);
        const auto end = dot == std::string_view::npos ? preRelease.size() : dot;
        isValid = isValid && IsValidPreReleaseIdentifier(preRelease.substr(start, end - start));
        start = end + 1;
    }

    // Assert
    CheckTrue(isValid, __func__, __LINE__,
        "ASWLOG_VERSION_PRERELEASE should be empty or SemVer pre-release identifiers: " + std::string(preRelease));
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Version::Test_VersionString_MatchesParts()
{
    // Arrange
    const std::string_view preRelease = ASWLog::VersionPreRelease;
    auto expected = std::format("{}.{}.{}", ASWLog::VersionMajor, ASWLog::VersionMinor, ASWLog::VersionPatch);
    if (!preRelease.empty())
        expected.append("-").append(preRelease);

    // Act
    const std::string version = ASWLog::Version;

    // Assert
    CheckEquals(expected, version, __func__, __LINE__,
        "ASWLOG_VERSION_STRING should be MAJOR.MINOR.PATCH, plus -PRERELEASE if there is one");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_Version)
