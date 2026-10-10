/* **************************************************************************
Test_ASWLog_Version.cpp
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
#include "Test_ASWLog_Version.h"
//---------------------------------------------------------------------------
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
    CheckEquals(std::string(ASWLOG_VERSION_STRING), std::string(version),
        "GetVersionStr() should return ASWLOG_VERSION_STRING");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Version::Test_PreRelease_IsValidSemVer()
{
    // Arrange: a pre-release is empty (a release) or dot-separated identifiers, e.g. "dev.1". An identifier is
    // [0-9A-Za-z-], not empty, and has no leading zero if it is all digits (SemVer 2.0.0).
    const std::string identifier = "(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)";

    // Act
    const std::string preRelease = ASWLog::VersionPreRelease;

    // Assert
    CheckMatches(preRelease, "(?:" + identifier + "(?:\\." + identifier + ")*)?",
        "ASWLOG_VERSION_PRERELEASE should be empty or SemVer pre-release identifiers");
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
    CheckEquals(expected, version,
        "ASWLOG_VERSION_STRING should be MAJOR.MINOR.PATCH, plus -PRERELEASE if there is one");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_Version)
