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
#include <format>
#include <fstream>
#include <iterator>
#include <ratio>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "UT_Helper_DateTime.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

// True if system_clock ticks in less than 1 us (100 ns with MSVC and MinGW on Windows, 1 ns with libstdc++ on Linux),
// false for 1 us (libc++, e.g. RAD Studio)
constexpr bool HasSubmicrosecondClock = std::ratio_less_v<std::chrono::system_clock::period, std::micro>;

//---------------------------------------------------------------------------

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

// The bytes of 'text' in hex, e.g. "E2 82", for a check's message
std::string ToHex(std::string_view text)
{
    std::string hex;
    for (const char character : text)
        hex += std::format("{}{:02X}", hex.empty() ? "" : " ", static_cast<unsigned char>(character));

    return hex;
}

// 'text' as JSON::AppendString() writes it
std::string ToJSONString(std::string_view text)
{
    std::string output;
    ASWLog::JSON::AppendString(output, text);

    return output;
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
    RegisterTest(&TTest_ASWLog_Utils::Test_JSON_AppendString_EscapesQuotesBackslashAndControls, "JSON_AppendString_EscapesQuotesBackslashAndControls");
    RegisterTest(&TTest_ASWLog_Utils::Test_JSON_AppendString_FindsSpecialCharactersAnywhereInALongText, "JSON_AppendString_FindsSpecialCharactersAnywhereInALongText");
    RegisterTest(&TTest_ASWLog_Utils::Test_JSON_AppendString_KeepsValidUTF8, "JSON_AppendString_KeepsValidUTF8");
    RegisterTest(&TTest_ASWLog_Utils::Test_JSON_AppendString_ReplacesInvalidUTF8, "JSON_AppendString_ReplacesInvalidUTF8");
    RegisterTest(&TTest_ASWLog_Utils::Test_MatchesWildcard_Patterns, "MatchesWildcard_Patterns");
    RegisterTest(&TTest_ASWLog_Utils::Test_RenameWithoutReplacing_KeepsExistingTarget, "RenameWithoutReplacing_KeepsExistingTarget");
    RegisterTest(&TTest_ASWLog_Utils::Test_Time_GetUTCOffsetMinutes_FollowsDaylightSavingTime, "Time_GetUTCOffsetMinutes_FollowsDaylightSavingTime");
    RegisterTest(&TTest_ASWLog_Utils::Test_Time_ToDateString, "Time_ToDateString");
    RegisterTest(&TTest_ASWLog_Utils::Test_Time_ToDateString_FollowsZone, "Time_ToDateString_FollowsZone");
    RegisterTest(&TTest_ASWLog_Utils::Test_Time_ToISO8601String, "Time_ToISO8601String");
    RegisterTest(&TTest_ASWLog_Utils::Test_Time_ToLocalISO8601String_IncludesOffset, "Time_ToLocalISO8601String_IncludesOffset");
    RegisterTest(&TTest_ASWLog_Utils::Test_Time_WriteISO8601_CalendarEdges, "Time_WriteISO8601_CalendarEdges");
    RegisterTest(&TTest_ASWLog_Utils::Test_Time_WriteISO8601_LocalTimeWithEachPrecision, "Time_WriteISO8601_LocalTimeWithEachPrecision");
    RegisterTest(&TTest_ASWLog_Utils::Test_Time_WriteISO8601_UTCWithEachPrecision, "Time_WriteISO8601_UTCWithEachPrecision");
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
    AssertNotEmpty(logName, "Generated log file name should not be empty");

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

    CheckNotEmpty(neither, "The name should still contain the timestamp/PID/TID segments when both are empty");
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
    CheckNotNull(thread, "The id should open a thread");
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

    CheckNotEmpty(osInfo, "OS info string should not be empty");
    CheckTrue(hasEdition, "Windows OS info should include the edition name (Home, Pro, Enterprise, etc.)");
#else
    CheckNotEmpty(osInfo, "OS info string should not be empty");
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
    CheckNotEmpty(relativeRoot, "The relative path to the root should be found");
    CheckTrue(ASWLog::IsRootFolder(relativeRoot), "A relative path that leads to a root should be a root folder");
    if (currentPath != currentPath.root_path())
        CheckFalse(ASWLog::IsRootFolder("."), "\".\" should not be a root folder when the current folder isn't one");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_JSON_AppendString_EscapesQuotesBackslashAndControls()
{
    // Arrange: each control character's escape, the short form where JSON has one
    std::string appended = "key:";

    // Act
    ASWLog::JSON::AppendString(appended, "value");

    // Assert
    CheckEquals(std::string("key:\"value\""), appended, "The string should be appended, in quotes");
    CheckEquals(std::string("\"\""), ToJSONString(""), "An empty text should be an empty string");
    CheckEquals(std::string(R"("say \"hi\" \\ there")"), ToJSONString(R"(say "hi" \ there)"), "Quotes and backslashes should be escaped");
    CheckEquals(std::string(R"("a / b ~)" "\x7F" R"(")"), ToJSONString("a / b ~\x7F"), "'/' and DEL should be written as they are");
    CheckEquals(std::string(R"("line1\nline2\r\n\tend")"), ToJSONString("line1\nline2\r\n\tend"), "Line breaks should be escaped");

    for (int code = 0; code < 0x20; ++code)
    {
        std::string expected;
        switch (code)
        {
            case 0x08: expected = "\\b"; break;
            case 0x09: expected = "\\t"; break;
            case 0x0A: expected = "\\n"; break;
            case 0x0C: expected = "\\f"; break;
            case 0x0D: expected = "\\r"; break;
            default: expected = std::format("\\u{:04x}", code); break;
        }

        CheckEquals("\"" + expected + "\"", ToJSONString(std::string(1, static_cast<char>(code))),
            std::format("Control character 0x{:02X} should be escaped", code));
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_JSON_AppendString_FindsSpecialCharactersAnywhereInALongText()
{
    // Arrange: plain text is scanned 8 bytes at a time, so each special character goes at every position of the first
    // two 8-byte words of a longer text, with what it should become
    struct TCase
    {
        std::string Text;
        std::string Expected;
    };

    const std::vector<TCase> specials{
        { "\"", "\\\"" }, { "\\", "\\\\" }, { std::string(1, '\0'), "\\u0000" }, { "\n", "\\n" }, { "\x1F", "\\u001f" },
        { " ", " " }, { "\x7F", "\x7F" }, { "\xCE\xBB", "\xCE\xBB" }, { "\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80" },
        { "\x80", "\xEF\xBF\xBD" }, { "\xFF", "\xEF\xBF\xBD" }, { "\xE2\x82", "\xEF\xBF\xBD" }
    };

    // Act & Assert
    for (const auto& special : specials)
    {
        for (std::size_t position = 0; position < 16; ++position)
        {
            const std::string before(position, 'a');
            const std::string after(24 - position, 'z');
            CheckEquals("\"" + before + special.Expected + after + "\"", ToJSONString(before + special.Text + after),
                std::format("{} at index {} should be found", ToHex(special.Text), position));
        }
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_JSON_AppendString_KeepsValidUTF8()
{
    // Arrange: the first and last character of each UTF-8 length, the characters around the surrogates, and a few
    // common ones
    const std::vector<std::string> texts{
        "\xC2\x80", "\xDF\xBF", "\xE0\xA0\x80", "\xED\x9F\xBF", "\xEE\x80\x80", "\xEF\xBF\xBF", "\xF0\x90\x80\x80",
        "\xF4\x8F\xBF\xBF", "\xCE\xBB", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "a\xCE\xBB" "b\xE2\x82\xAC" "c"
    };

    // Act & Assert
    for (const auto& text : texts)
        CheckEquals("\"" + text + "\"", ToJSONString(text), "Valid UTF-8 should be written as it is: " + ToHex(text));
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_JSON_AppendString_ReplacesInvalidUTF8()
{
    // Arrange: each invalid text and what it should become, U+FFFD (R) for each longest start of a valid sequence, or
    // else each byte
    const std::string R = "\xEF\xBF\xBD";
    struct TCase
    {
        std::string Text;
        std::string Expected;
    };

    const std::vector<TCase> cases{
        { "\x80", R }, // A continuation byte without a lead byte
        { "a\xBF" "b", "a" + R + "b" },
        { "\xC0\xAF", R + R }, // Overlong forms: C0 and C1 never start a valid sequence
        { "\xC1\xBF", R + R },
        { "\xE0\x80\xAF", R + R + R }, // Overlong: E0 needs A0-BF next
        { "\xF0\x80\x80\x80", R + R + R + R }, // Overlong: F0 needs 90-BF next
        { "\xED\xA0\x80", R + R + R }, // A surrogate (U+D800)
        { "\xF4\x90\x80\x80", R + R + R + R }, // Above U+10FFFF
        { "\xF5\x80", R + R }, // F5-FF never start a valid sequence
        { "\xFF", R },
        { "\xC2", R }, // Cut off at the end
        { "\xE2\x82", R }, // A cut off sequence is one U+FFFD
        { "\xE2\x82" "X", R + "X" },
        { "\xF0\x9F\x98", R },
        { "\xF0\x9F\x98" "a", R + "a" },
        { "\xC2\"", R + "\\\"" }, // The character after a cut off sequence is written as usual
        { "\xCE\xBB\xCE", "\xCE\xBB" + R }
    };

    // Act & Assert
    for (const auto& testCase : cases)
    {
        CheckEquals("\"" + testCase.Expected + "\"", ToJSONString(testCase.Text),
            "Invalid UTF-8 should be replaced with U+FFFD: " + ToHex(testCase.Text));
    }
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
void TTest_ASWLog_Utils::Test_Time_ToDateString_FollowsZone()
{
    // Arrange: 02:00 UTC is still the evening before in US Eastern time
    using namespace std::chrono_literals;
    const auto time = std::chrono::sys_days{ 2026y / 7 / 1 } + 2h;
    const TScopedTimeZone timeZone("EST5EDT");

    // Act
    const auto utcDate = ASWLog::Time::ToDateString(time);
    const auto localDate = ASWLog::Time::ToDateString(time, ASWLog::TimeZone::Local);

    // Assert
    CheckEquals(std::string("2026-07-01"), utcDate, "The UTC date should be the default");
    CheckEquals(std::string("2026-06-30"), localDate, "The local date should be the day before, 4 hours behind UTC");
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
void TTest_ASWLog_Utils::Test_Time_WriteISO8601_CalendarEdges()
{
    // Arrange
    using namespace std::chrono_literals;
    using std::chrono::sys_days;

    struct TCase
    {
        std::chrono::system_clock::time_point Time;
        std::string Expected;
        std::string What;
    };

    const TCase cases[] = {
        { sys_days{ 1970y / 1 / 1 }, "1970-01-01T00:00:00.000Z", "The epoch" },
        { sys_days{ 2026y / 1 / 15 } + 7h + 7ms, "2026-01-15T07:00:00.007Z", "Short fields should be zero-padded" },
        { sys_days{ 1999y / 12 / 31 } + 23h + 59min + 59s + 999ms, "1999-12-31T23:59:59.999Z", "The last millisecond of a year" },
        { sys_days{ 2024y / 2 / 29 } + 12h + 34min + 56s + 500ms, "2024-02-29T12:34:56.500Z", "A leap day" },
        { sys_days{ 2000y / 2 / 29 }, "2000-02-29T00:00:00.000Z", "A leap day in a century year divisible by 400" },
        { sys_days{ 2100y / 2 / 28 } + 24h, "2100-03-01T00:00:00.000Z", "The day after February 28 in a century year that isn't a leap year" },
        { sys_days{ 2026y / 7 / 1 } + 12h + 123999us, "2026-07-01T12:00:00.123Z", "Microseconds should be cut off, not rounded" },
        { sys_days{ 1970y / 1 / 1 } - 1ms, "1969-12-31T23:59:59.999Z", "A time before 1970 should fall on the millisecond it is in" },
    };

    for (const auto& testCase : cases)
    {
        // Act
        char buffer[ASWLog::Time::ISO8601BufferSize];
        const auto size = ASWLog::Time::WriteISO8601(buffer, testCase.Time);

        // Assert: the string forms are the same text
        CheckEquals(testCase.Expected, std::string(buffer, size), testCase.What);
        CheckEquals(testCase.Expected, ASWLog::Time::ToISO8601String(testCase.Time), testCase.What + " (ToISO8601String)");
        CheckEquals(testCase.Expected.substr(0, 10), ASWLog::Time::ToDateString(testCase.Time), testCase.What + " (ToDateString)");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_Time_WriteISO8601_LocalTimeWithEachPrecision()
{
    // Arrange: a winter noon UTC, with a fraction whose 7th digit only a clock finer than 1 us keeps
    using namespace std::chrono_literals;
    const auto time = std::chrono::sys_days{ 2026y / 1 / 15 } + 12h + std::chrono::duration_cast<std::chrono::system_clock::duration>(7000400ns);
    const std::string nanosecondDigits = HasSubmicrosecondClock ? "007000400" : "007000000";

    const auto write = [&](ASWLog::TimePrecision precision) {
            char buffer[ASWLog::Time::ISO8601BufferSize];
            return std::string(buffer, ASWLog::Time::WriteISO8601(buffer, time, ASWLog::TimeZone::Local, precision));
        };

    // Act & Assert
    {
        const TScopedTimeZone timeZone("EST5EDT");
        CheckEquals(std::string("2026-01-15T07:00:00.007-05:00"), write(ASWLog::TimePrecision::Milliseconds), "Milliseconds should come before the offset");
        CheckEquals(std::string("2026-01-15T07:00:00.007000-05:00"), write(ASWLog::TimePrecision::Microseconds), "Microseconds should come before the offset");
        CheckEquals("2026-01-15T07:00:00." + nanosecondDigits + "-05:00", write(ASWLog::TimePrecision::Nanoseconds), "Nanoseconds should come before the offset");
        CheckEquals(std::string("2026-01-15T07:00:00.007000-05:00"), ASWLog::Time::ToISO8601String(time, ASWLog::TimeZone::Local, ASWLog::TimePrecision::Microseconds), "ToISO8601String should pass the zone and precision on");
    }
    {
        const TScopedTimeZone timeZone("IST-5:30");
        CheckEquals("2026-01-15T17:30:00." + nanosecondDigits + "+05:30", write(ASWLog::TimePrecision::Nanoseconds), "A zone ahead of UTC should show a '+' offset with its minutes");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Utils::Test_Time_WriteISO8601_UTCWithEachPrecision()
{
    // Arrange: a fraction with a digit in each place, the 7th only kept by a clock finer than 1 us
    using namespace std::chrono_literals;
    const auto time = std::chrono::sys_days{ 2026y / 7 / 1 } + 12h + std::chrono::duration_cast<std::chrono::system_clock::duration>(123456700ns);

    struct TCase
    {
        ASWLog::TimePrecision Precision;
        std::string Expected;
        std::string What;
    };

    const TCase cases[] = {
        { ASWLog::TimePrecision::Milliseconds, "2026-07-01T12:00:00.123Z", "Milliseconds should show 3 digits" },
        { ASWLog::TimePrecision::Microseconds, "2026-07-01T12:00:00.123456Z", "Microseconds should show 6 digits, cut off rather than rounded" },
        { ASWLog::TimePrecision::Nanoseconds, HasSubmicrosecondClock ? "2026-07-01T12:00:00.123456700Z" : "2026-07-01T12:00:00.123456000Z",
          "Nanoseconds should show 9 digits, as far as the clock holds them" },
    };

    for (const auto& testCase : cases)
    {
        // Act
        char buffer[ASWLog::Time::ISO8601BufferSize];
        const auto size = ASWLog::Time::WriteISO8601(buffer, time, ASWLog::TimeZone::UTC, testCase.Precision);

        // Assert
        CheckEquals(testCase.Expected, std::string(buffer, size), testCase.What);
        CheckEquals(testCase.Expected, ASWLog::Time::ToISO8601String(time, ASWLog::TimeZone::UTC, testCase.Precision), testCase.What + " (ToISO8601String)");
    }
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_Utils)
