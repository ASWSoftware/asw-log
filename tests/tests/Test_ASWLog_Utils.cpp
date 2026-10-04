/* **************************************************************************
Test_ASWLog_Utils.cpp
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
#include "Test_ASWLog_Utils.h"
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

// While alive, sets the C runtime's local time zone through the TZ environment variable (POSIX format, e.g. "EST5EDT",
// which the Windows C runtime also reads), then restores the previous value.
class TScopedTimeZone
{
private:
    bool m_HadValue = false;
    std::string m_PreviousValue;

    // Sets TZ to 'value', or removes it if 'value' is null, and makes the C runtime read it again
    static void Apply(const char* value)
    {
#if defined(_WIN32)
        _putenv_s("TZ", value != nullptr ? value : ""); // An empty value removes the variable
        _tzset();

        // Once the Windows C runtime has read a system time zone without daylight saving time (e.g. UTC on CI machines,
        // or Arizona), it keeps that zone's daylight saving bias of 0 even when TZ then names a zone that has daylight
        // saving time, so summer would be flagged as daylight saving time without moving the clock. TZ can't give the
        // bias, so set the usual hour.
        int hasDaylightSavingTime = 0;
        if (value != nullptr && _get_daylight(&hasDaylightSavingTime) == 0 && hasDaylightSavingTime != 0)
        {
            // __dstbias() is deprecated in favor of _get_dstbias(), which can't set it.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            *__dstbias() = -3600;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        }
#else
        if (value != nullptr)
            setenv("TZ", value, 1);
        else
            unsetenv("TZ");
        tzset();
#endif
    }

public:
    explicit TScopedTimeZone(const char* timeZone)
    {
#if defined(_WIN32)
        // _putenv_s also updates the process environment, which GetEnvironmentVariableA reads (avoids getenv(), which
        // MSVC warns about)
        char buffer[256]{};
        const DWORD length = GetEnvironmentVariableA("TZ", buffer, sizeof(buffer));
        m_HadValue = length > 0 && length < sizeof(buffer);
        if (m_HadValue)
            m_PreviousValue.assign(buffer, length);
#else
        const char* previous = std::getenv("TZ");
        m_HadValue = previous != nullptr;
        if (m_HadValue)
            m_PreviousValue = previous;
#endif
        Apply(timeZone);
    }

    ~TScopedTimeZone()
    {
        Apply(m_HadValue ? m_PreviousValue.c_str() : nullptr);
    }

    TScopedTimeZone(const TScopedTimeZone&) = delete;
    TScopedTimeZone& operator=(const TScopedTimeZone&) = delete;
};

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
TTest_ASWLog_Utils::TTest_ASWLog_Utils()
    : inherited("ASWLog_Utils_Tests")
{
    RegisterTest(&TTest_ASWLog_Utils::Test_GenerateLogFileName_ContainsExpectedFields, "GenerateLogFileName_ContainsExpectedFields");
    RegisterTest(&TTest_ASWLog_Utils::Test_GenerateLogFileName_PrefixAndPostfixAreOptional, "GenerateLogFileName_PrefixAndPostfixAreOptional");
    RegisterTest(&TTest_ASWLog_Utils::Test_GetCurrentOSProcessId_MatchesOS, "GetCurrentOSProcessId_MatchesOS");
    RegisterTest(&TTest_ASWLog_Utils::Test_GetCurrentOSThreadId_IdentifiesCallingThread, "GetCurrentOSThreadId_IdentifiesCallingThread");
    RegisterTest(&TTest_ASWLog_Utils::Test_GetOSInfoString_ContainsEdition, "GetOSInfoString_ContainsEdition");
    RegisterTest(&TTest_ASWLog_Utils::Test_GetTimeInfoString_ReportsCurrentOffset, "GetTimeInfoString_ReportsCurrentOffset");
    RegisterTest(&TTest_ASWLog_Utils::Test_GetWindowsEditionName_ProductTypes, "GetWindowsEditionName_ProductTypes");
    RegisterTest(&TTest_ASWLog_Utils::Test_IsRootFolder_DetectsRootFolders, "IsRootFolder_DetectsRootFolders");
    RegisterTest(&TTest_ASWLog_Utils::Test_IsRootFolder_ResolvesRelativePaths, "IsRootFolder_ResolvesRelativePaths");
    RegisterTest(&TTest_ASWLog_Utils::Test_MatchesWildcard_Patterns, "MatchesWildcard_Patterns");
    RegisterTest(&TTest_ASWLog_Utils::Test_RenameWithoutReplacing_KeepsExistingTarget, "RenameWithoutReplacing_KeepsExistingTarget");
    RegisterTest(&TTest_ASWLog_Utils::Test_Time_GetUTCOffsetMinutes_FollowsDaylightSavingTime, "Time_GetUTCOffsetMinutes_FollowsDaylightSavingTime");
    RegisterTest(&TTest_ASWLog_Utils::Test_Time_ToDateString, "Time_ToDateString");
    RegisterTest(&TTest_ASWLog_Utils::Test_Time_ToISO8601String, "Time_ToISO8601String");
    RegisterTest(&TTest_ASWLog_Utils::Test_Time_ToLocalISO8601String_IncludesOffset, "Time_ToLocalISO8601String_IncludesOffset");
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
    AssertTrue(!logName.empty(), "Generated log file name should not be empty");

    CheckContains(logName, "_PID", "Generated file name should include process id");
    CheckContains(logName, "_TID", "Generated file name should include thread id");
    CheckContains(logName, "ExampleLog.txt", "Generated file name should include the custom postfix");

    const auto idsPart = "_PID" + std::to_string(ASWLog::GetCurrentOSProcessId()) + "_TID" + std::to_string(ASWLog::GetCurrentOSThreadId()) + "_";
    CheckContains(logName, idsPart, "Generated file name should use the OS process and thread ids");
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
    CheckStartsWith(withBoth, "MyApp_", "A non-empty prefix should appear at the very start of the name");
    CheckContains(withBoth, "ExampleLog.txt", "The postfix should still be included alongside a prefix");
    CheckNotContains(withBoth, "__", "Prefix and postfix should not introduce a doubled separator");

    CheckStartsWith(prefixOnly, "MyApp_", "The prefix should appear even when the postfix is empty");
    CheckNotEndsWith(prefixOnly, "_", "An empty postfix should not leave a trailing separator");

    CheckNotStartsWith(postfixOnly, "_", "An empty prefix should not leave a leading separator");
    CheckContains(postfixOnly, "ExampleLog.txt", "The postfix should still be included when the prefix is empty");

    CheckFalse(neither.empty(), "The name should still contain the timestamp/PID/TID segments when both are empty");
    CheckNotStartsWith(neither, "_", "An empty prefix should not leave a leading separator when the postfix is also empty");
    CheckNotEndsWith(neither, "_", "An empty postfix should not leave a trailing separator when the prefix is also empty");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_GetCurrentOSProcessId_MatchesOS()
{
    // Act
    const auto processId = ASWLog::GetCurrentOSProcessId();

    // Assert
#if defined(_WIN32)
    CheckEquals(static_cast<std::uint32_t>(GetCurrentProcessId()), processId, "The id should be the Windows process id");
#else
    // "/proc/self" is a link named for this process's id
    std::error_code errorCode;
    const auto selfLink = std::filesystem::read_symlink("/proc/self", errorCode);
    CheckEquals(std::to_string(processId), selfLink.string(), "The id should be the one /proc/self names");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_GetCurrentOSThreadId_IdentifiesCallingThread()
{
    // Act
    const auto mainThreadId = ASWLog::GetCurrentOSThreadId();
    const auto mainThreadIdAgain = ASWLog::GetCurrentOSThreadId();

    std::uint32_t workerThreadId = 0;
    std::thread worker([&] {
        workerThreadId = ASWLog::GetCurrentOSThreadId();
            });
    worker.join();

    // Assert
    CheckEquals(mainThreadId, mainThreadIdAgain, "The id should stay the same on one thread");
    CheckTrue(mainThreadId != workerThreadId, "Two threads should have different ids");

#if defined(_WIN32)
    // The id should name a live thread of this process
    const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, mainThreadId);
    CheckTrue(thread != nullptr, "The id should open a thread");
    if (thread != nullptr)
    {
        CheckEquals(static_cast<std::uint32_t>(GetCurrentProcessId()), static_cast<std::uint32_t>(GetProcessIdOfThread(thread)), "The thread should belong to this process");
        CloseHandle(thread);
    }
#else
    // Each of this process's threads has a /proc/self/task/<id> folder
    std::error_code errorCode;
    CheckTrue(std::filesystem::is_directory("/proc/self/task/" + std::to_string(mainThreadId), errorCode), "The id should name a thread of this process");
#endif
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

    CheckTrue(!osInfo.empty(), "OS info string should not be empty");
    CheckTrue(hasEdition, "Windows OS info should include the edition name (Home, Pro, Enterprise, etc.)");
#else
    CheckTrue(!osInfo.empty(), "OS info string should not be empty");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_GetTimeInfoString_ReportsCurrentOffset()
{
    // Arrange: US Eastern time, which is -300 minutes from UTC, or -240 while daylight saving time is in effect. The
    // C runtime's tm_isdst says which applies now.
    const TScopedTimeZone timeZone("EST5EDT");
    const auto timeValue = std::time(nullptr);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &timeValue);
#else
    localtime_r(&timeValue, &localTime);
#endif
    const bool isDaylightSavingTime = localTime.tm_isdst > 0;
    const std::string expectedOffset = isDaylightSavingTime ? "-240" : "-300";
    const std::string expectedLocalSuffix = isDaylightSavingTime ? "-04:00" : "-05:00";

    // Act
    const auto timeInfo = ASWLog::GetTimeInfoString();

    // Assert
    const std::string offsetLabel = "offset_minutes=";
    const auto offsetPos = timeInfo.find(offsetLabel);
    CheckTrue(offsetPos != std::string::npos, "The time info should include offset_minutes");
    if (offsetPos != std::string::npos)
        CheckEquals(expectedOffset, timeInfo.substr(offsetPos + offsetLabel.size()), "offset_minutes should include daylight saving time when it is in effect");
    CheckContains(timeInfo, expectedLocalSuffix + ", " + offsetLabel, "The local time should end with the zone's current offset");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_GetWindowsEditionName_ProductTypes()
{
#if defined(_WIN32)
    // Act & Assert
    CheckEquals(std::string("Home"), ASWLog::GetWindowsEditionName(PRODUCT_CORE, false),
        "Windows 10/11 Home should be named");
    CheckEquals(std::string("Pro"), ASWLog::GetWindowsEditionName(PRODUCT_PROFESSIONAL, false),
        "Pro should be named");
    CheckEquals(std::string("Server Standard"), ASWLog::GetWindowsEditionName(PRODUCT_STANDARD_SERVER, true),
        "Server Standard should be named");
    CheckEquals(std::string("Server Datacenter"), ASWLog::GetWindowsEditionName(PRODUCT_DATACENTER_SERVER, true),
        "Server Datacenter (e.g. GitHub's Windows runners) should be named");
    CheckEquals(std::string("Server (product type 0xABCD)"), ASWLog::GetWindowsEditionName(0xABCD, true),
        "An unlisted server product type should still say Server");
    CheckEquals(std::string("Unknown (product type 0xABCD)"), ASWLog::GetWindowsEditionName(0xABCD, false),
        "An unlisted workstation product type should be Unknown");
#else
    Skip("GetWindowsEditionName() is Windows-only");
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
    CheckTrue(ASWLog::IsRootFolder(root), "The root of the temp folder's path should be a root folder");
    CheckTrue(ASWLog::IsRootFolder(root / "."), "The root followed by \".\" should be a root folder");
    CheckTrue(ASWLog::IsRootFolder(firstFolder / ".."), "A folder's \"..\" at the top level should be a root folder");
    CheckFalse(ASWLog::IsRootFolder(firstFolder), "A top-level folder should not be a root folder");
    CheckFalse(ASWLog::IsRootFolder(tempDir), "The temp folder should not be a root folder");
    CheckTrue(ASWLog::IsRootFolder(std::filesystem::path()), "An empty path can't be checked, so it should count as a root folder");

#if defined(_WIN32)
    const auto driveLetter = tempDir.root_name().string().substr(0, 1);
    CheckTrue(ASWLog::IsRootFolder(driveLetter + ":/"), "A drive root written with '/' should be a root folder");

    // The drive's administrative share (e.g. "\\localhost\C$\"), when this machine allows it. Forward slashes, since
    // Windows accepts them in UNC paths too.
    const std::filesystem::path share = "//localhost/" + driveLetter + "$/";
    std::error_code errorCode;
    if (std::filesystem::is_directory(share / tempDir.relative_path(), errorCode))
    {
        CheckTrue(ASWLog::IsRootFolder(share), "A network share's root should be a root folder");
        CheckFalse(ASWLog::IsRootFolder(share / tempDir.relative_path()), "A folder in a network share should not be a root folder");
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
    CheckFalse(relativeRoot.empty(), "The relative path to the root should be found");
    CheckTrue(ASWLog::IsRootFolder(relativeRoot), "A relative path that leads to a root should be a root folder");
    if (currentPath != currentPath.root_path())
        CheckFalse(ASWLog::IsRootFolder("."), "\".\" should not be a root folder when the current folder isn't one");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_MatchesWildcard_Patterns()
{
    // Arrange
    const std::string fileName = "app_123.log";
    const std::string fileName2 = "notes.txt";

    // Act & Assert
    CheckTrue(ASWLog::MatchesWildcard(fileName, "*.log"), "Wildcard should match a suffix");
    CheckTrue(ASWLog::MatchesWildcard(fileName, "app_*.log"), "Wildcard should match mid-string patterns");
    CheckTrue(ASWLog::MatchesWildcard(fileName2, "n?tes.*"), "Question mark wildcard should match a single character");
    CheckFalse(ASWLog::MatchesWildcard(fileName, "*.txt"), "Wildcard should reject non-matching files");
    CheckTrue(ASWLog::MatchesWildcard("no_extension", "*"), "A lone '*' should match any name, with or without a dot");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_RenameWithoutReplacing_KeepsExistingTarget()
{
    // Arrange
    const auto folder = std::filesystem::temp_directory_path() / "aswlog_utils_rename_test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);

    const auto source = folder / "source.log";
    const auto existingTarget = folder / "existing.bak";
    const auto freeTarget = folder / "free.bak";
    {
        std::ofstream sourceStream(source);
        sourceStream << "source_content";
        std::ofstream targetStream(existingTarget);
        targetStream << "existing_content";
    }

    // Act
    const auto existingResult = ASWLog::RenameWithoutReplacing(source, existingTarget);
    const bool sourceKept = std::filesystem::exists(source);
    const auto existingContent = ReadText(existingTarget);
    const auto freeResult = ASWLog::RenameWithoutReplacing(source, freeTarget);
    const auto missingResult = ASWLog::RenameWithoutReplacing(folder / "missing.log", folder / "other.bak");

    // Assert
    CheckTrue(existingResult == std::errc::file_exists, "Renaming onto an existing file should fail with file_exists");
    CheckEquals(std::string("existing_content"), existingContent, "The existing file should not be replaced");
    CheckTrue(sourceKept, "The source should stay when the rename is refused");
    CheckFalse(static_cast<bool>(freeResult), "Renaming to a free name should succeed");
    CheckEquals(std::string("source_content"), ReadText(freeTarget), "The renamed file should keep its content");
    CheckFalse(std::filesystem::exists(source), "The source name should be gone after the rename");
    CheckTrue(static_cast<bool>(missingResult) && missingResult != std::errc::file_exists, "Renaming a missing file should fail with another error");

    std::error_code errorCode;
    std::filesystem::remove_all(folder, errorCode);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_Time_GetUTCOffsetMinutes_FollowsDaylightSavingTime()
{
    // Arrange: noon UTC on a summer and a winter day
    using namespace std::chrono_literals;
    const auto summer = std::chrono::sys_days{ 2026y / 7 / 1 } + 12h;
    const auto winter = std::chrono::sys_days{ 2026y / 1 / 15 } + 12h;

    // Act & Assert
    {
        const TScopedTimeZone timeZone("EST5EDT");
        CheckEquals(-240, ASWLog::Time::GetUTCOffsetMinutes(summer), "US Eastern daylight time should be 4 hours behind UTC");
        CheckEquals(-300, ASWLog::Time::GetUTCOffsetMinutes(winter), "US Eastern standard time should be 5 hours behind UTC");
    }
    {
        const TScopedTimeZone timeZone("IST-5:30");
        CheckEquals(330, ASWLog::Time::GetUTCOffsetMinutes(summer), "India Standard Time should be 5 hours 30 minutes ahead of UTC");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_Time_ToDateString()
{
    // Arrange
    const auto now = std::chrono::system_clock::now();

    // Act
    const std::string date = ASWLog::Time::ToDateString(now);

    // Assert
    CheckGreaterThanOrEqual(date.size(), 10, "Date string should have a YYYY-MM-DD format");
    CheckContains(date, "-", "Date string should include date separators");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_Time_ToISO8601String()
{
    // Arrange
    const auto now = std::chrono::system_clock::now();

    // Act
    const std::string iso = ASWLog::Time::ToISO8601String(now);

    // Assert
    CheckContains(iso, "T", "ISO string should include the T separator");
    CheckTrue(iso.find('Z') == iso.size() - 1, "ISO string should end with Z");
    CheckGreaterThanOrEqual(iso.size(), 24, "ISO string should include date and time");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_Time_ToLocalISO8601String_IncludesOffset()
{
    // Arrange: just after noon UTC on a summer and a winter day
    using namespace std::chrono_literals;
    const auto summer = std::chrono::sys_days{ 2026y / 7 / 1 } + 12h + 123ms;
    const auto winter = std::chrono::sys_days{ 2026y / 1 / 15 } + 12h + 7ms;

    // Act & Assert
    {
        const TScopedTimeZone timeZone("EST5EDT");
        CheckEquals(std::string("2026-07-01T08:00:00.123-04:00"), ASWLog::Time::ToLocalISO8601String(summer), "Summer should show US Eastern daylight time with milliseconds and its offset");
        CheckEquals(std::string("2026-01-15T07:00:00.007-05:00"), ASWLog::Time::ToLocalISO8601String(winter), "Winter should show US Eastern standard time with zero-padded milliseconds and its offset");
    }
    {
        const TScopedTimeZone timeZone("IST-5:30");
        CheckEquals(std::string("2026-07-01T17:30:00.123+05:30"), ASWLog::Time::ToLocalISO8601String(summer), "A zone ahead of UTC should show a '+' offset with its minutes");
    }
    {
        const TScopedTimeZone timeZone("UTC0");
        CheckEquals(std::string("2026-07-01T12:00:00.123+00:00"), ASWLog::Time::ToLocalISO8601String(summer), "UTC should show a +00:00 offset");
        CheckEquals(std::string("2026-07-01T12:00:00.000+00:00"), ASWLog::Time::ToLocalISO8601String(std::chrono::sys_days{ 2026y / 7 / 1 } + 12h), "A whole second should show .000");
    }
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_Utils)
