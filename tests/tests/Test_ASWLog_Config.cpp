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
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_CategoryLog.h"
#include "ASWLog_Config.h"
#include "ASWLog_Formatter.h"
#include "ASWLog_MemoryLog.h"
#include "ASWLog_NullLog.h"
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------
#include "UT_Helper_Environment.h"
#include "UT_Helper_StdErr.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

// The tests' own prefix, so that a developer's ASWLOG_* variables can't change their results
constexpr std::string_view TestPrefix = "ASWLOGTEST_CONFIG_";

// Clears the category levels a test applied (they are process-wide) when it ends
struct TCategoryLevelsReset
{
    TCategoryLevelsReset() = default;
    TCategoryLevelsReset(const TCategoryLevelsReset&) = delete;
    TCategoryLevelsReset& operator=(const TCategoryLevelsReset&) = delete;

    ~TCategoryLevelsReset()
    {
        ASWLog::TASWCategoryLog::ApplyLevels("");
    }
};

// The test variable named 'key'
std::string TestVariable(std::string_view key)
{
    return std::string(TestPrefix) + std::string(key);
}

} // namespace

//---------------------------------------------------------------------------
TTest_ASWLog_Config::TTest_ASWLog_Config()
    : inherited("ASWLog_Config_Tests")
{
    RegisterTest(&TTest_ASWLog_Config::Test_ApplyEnvironment_AppliesPatternAfterFormat, "ApplyEnvironment_AppliesPatternAfterFormat");
    RegisterTest(&TTest_ASWLog_Config::Test_ApplyEnvironment_AppliesTheCategoryLevels, "ApplyEnvironment_AppliesTheCategoryLevels");
    RegisterTest(&TTest_ASWLog_Config::Test_ApplyEnvironment_IgnoresUnsetAndEmptyVariables, "ApplyEnvironment_IgnoresUnsetAndEmptyVariables");
    RegisterTest(&TTest_ASWLog_Config::Test_ApplyEnvironment_ReadsTheDefaultPrefix, "ApplyEnvironment_ReadsTheDefaultPrefix");
    RegisterTest(&TTest_ASWLog_Config::Test_ApplyEnvironment_ReadsUTF8Values, "ApplyEnvironment_ReadsUTF8Values");
    RegisterTest(&TTest_ASWLog_Config::Test_ApplyEnvironment_ReportsInvalidValues, "ApplyEnvironment_ReportsInvalidValues");
    RegisterTest(&TTest_ASWLog_Config::Test_ApplyEnvironment_SeedsTheLevelAtInitialize, "ApplyEnvironment_SeedsTheLevelAtInitialize");
    RegisterTest(&TTest_ASWLog_Config::Test_ApplyEnvironment_SetsEachKey, "ApplyEnvironment_SetsEachKey");
    RegisterTest(&TTest_ASWLog_Config::Test_ApplySetting_ReportsAnUnknownKey, "ApplySetting_ReportsAnUnknownKey");
    RegisterTest(&TTest_ASWLog_Config::Test_ApplySetting_SetsOneSetting, "ApplySetting_SetsOneSetting");
    RegisterTest(&TTest_ASWLog_Config::Test_ApplySetting_WritesToStdErrWithoutHandler, "ApplySetting_WritesToStdErrWithoutHandler");
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
void TTest_ASWLog_Config::Test_ApplyEnvironment_AppliesPatternAfterFormat()
{
    // Arrange
    TScopedEnvironmentVariable format(TestVariable("FORMAT"), "JSON");
    TScopedEnvironmentVariable pattern(TestVariable("PATTERN"), "{level} {message}");

    ASWLog::TASWLogConfig config;

    // Act
    const bool isValid = config.ApplyEnvironment(TestPrefix);

    // Assert
    CheckTrue(isValid, "Both values should be valid");
    const auto* patternFormatter = dynamic_cast<const ASWLog::TASWPatternFormatter*>(config.Line.Formatter.get());
    CheckNotNull(patternFormatter, "PATTERN should win over FORMAT");
    if (patternFormatter != nullptr)
        CheckEquals(std::string("{level} {message}"), std::string(patternFormatter->GetPattern()), "The pattern should be the variable's");

    // Act: FORMAT=Text alone goes back to the built-in layout
    {
        TScopedEnvironmentVariable noPattern(TestVariable("PATTERN"), std::nullopt);
        TScopedEnvironmentVariable textFormat(TestVariable("FORMAT"), "text");
        CheckTrue(config.ApplyEnvironment(TestPrefix), "FORMAT=text should be valid");
    }

    // Assert
    CheckNull(config.Line.Formatter, "FORMAT=Text should clear the formatter (the built-in layout)");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ApplyEnvironment_AppliesTheCategoryLevels()
{
    // Arrange
    TCategoryLevelsReset resetLevels;
    ASWLog::TASWNullLog wrapped;
    ASWLog::TASWCategoryLog netLog("ConfigTestNet", wrapped);
    TScopedEnvironmentVariable categories(TestVariable("CATEGORIES"), "ConfigTestNet=Debug");

    std::vector<ASWLog::TASWLogError> errors;
    ASWLog::TASWLogConfig config;
    config.OnError = [&errors](const ASWLog::TASWLogError& error)
        {
            errors.push_back(error);
        };

    // Act
    const bool isValid = config.ApplyEnvironment(TestPrefix);

    // Assert
    CheckTrue(isValid, "The spec should be valid");
    CheckEquals(ASWLog::Level::Debug, netLog.GetMinimumLevel(), "CATEGORIES should set the category's level");

    // Act: an invalid spec
    const bool isInvalidValid = config.ApplySetting("CATEGORIES", "ConfigTestNet=Trace,Db=Loud");

    // Assert
    CheckFalse(isInvalidValid, "A spec with an unknown level should be invalid");
    CheckEquals(ASWLog::Level::Debug, netLog.GetMinimumLevel(), "An invalid spec should change no category's level");
    CheckEquals(1, errors.size(), "The invalid spec should be reported once");
    if (errors.size() == 1)
    {
        CheckEquals(ASWLog::ErrorKind::InvalidSetting, errors[0].Kind, "The report should be InvalidSetting");
        CheckEquals(std::string("CATEGORIES='ConfigTestNet=Trace,Db=Loud': 'Db=Loud': 'Loud' isn't a level (Trace, Debug, Info, Warn, "
            "Error, Critical or Off)"), errors[0].Message, "The report should name the setting, the value and the bad item");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ApplyEnvironment_IgnoresUnsetAndEmptyVariables()
{
    // Arrange: LEVEL empty, FLUSH only spaces, the rest unset
    TScopedEnvironmentVariable level(TestVariable("LEVEL"), "");
    TScopedEnvironmentVariable flush(TestVariable("FLUSH"), "  \t ");

    int errorCount = 0;
    ASWLog::TASWLogConfig config;
    config.OnError = [&errorCount](const ASWLog::TASWLogError&)
        {
            ++errorCount;
        };
    config.InitialMinimumLevel = ASWLog::Level::Warn;
    const auto formatter = std::make_shared<const ASWLog::TASWJSONFormatter>();
    config.Line.Formatter = formatter;

    // Act
    const bool isValid = config.ApplyEnvironment(TestPrefix);

    // Assert
    CheckTrue(isValid, "Unset and empty variables should not count as invalid");
    CheckEquals(0, errorCount, "Unset and empty variables should not be reported");
    CheckEquals(ASWLog::Level::Warn, config.InitialMinimumLevel, "An empty LEVEL should leave the level");
    CheckEquals(ASWLog::FlushMode::EveryWrite, config.File.Flush, "A FLUSH of spaces should leave the flush mode");
    CheckTrue(config.File.FolderPath == std::filesystem::path("logs"), "An unset FOLDER should leave the folder");
    CheckFalse(config.Async.Enabled, "An unset ASYNC should leave Async.Enabled");
    CheckEquals(0, config.Backtrace.Capacity, "An unset BACKTRACE should leave the capacity");
    CheckSame(formatter.get(), config.Line.Formatter.get(), "Unset FORMAT and PATTERN should leave the formatter");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ApplyEnvironment_ReadsTheDefaultPrefix()
{
    // Arrange: a developer's own ASWLOG_CATEGORIES must not change the process's category levels
    TScopedEnvironmentVariable categories("ASWLOG_CATEGORIES", std::nullopt);
    TScopedEnvironmentVariable precision("ASWLOG_TIME_PRECISION", "ns");

    ASWLog::TASWLogConfig config;
    config.OnError = [](const ASWLog::TASWLogError&)
        {
        }; // A developer's other ASWLOG_* variables may be invalid
    ASWLog::TASWLogConfig otherConfig;

    // Act
    config.ApplyEnvironment();
    otherConfig.ApplyEnvironment(TestPrefix);

    // Assert
    CheckEquals(ASWLog::TimePrecision::Nanoseconds, config.Line.TimestampPrecision, "The default prefix should be ASWLOG_");
    CheckEquals(ASWLog::TimePrecision::Milliseconds, otherConfig.Line.TimestampPrecision,
        "Another prefix should not read the ASWLOG_ variables");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ApplyEnvironment_ReadsUTF8Values()
{
    // Arrange: a lambda and a CJK character, in UTF-8
    TScopedEnvironmentVariable folder(TestVariable("FOLDER"), "logs\xCE\xBB");
    TScopedEnvironmentVariable file(TestVariable("FILE"), "\xE6\x97\xA5.txt");

    ASWLog::TASWLogConfig config;

    // Act
    const bool isValid = config.ApplyEnvironment(TestPrefix);

    // Assert
    CheckTrue(isValid, "The paths should be valid");
    CheckEquals(std::string("logs\xCE\xBB"), ASWLog::PathToUTF8String(config.File.FolderPath), "FOLDER should be read as UTF-8");
    CheckEquals(std::string("\xE6\x97\xA5.txt"), ASWLog::PathToUTF8String(config.File.FilePath), "FILE should be read as UTF-8");
#if defined(_WIN32)
    CheckTrue(config.File.FolderPath.wstring() == L"logs\x03BB", "FOLDER should name the lambda, not ANSI code page characters");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ApplyEnvironment_ReportsInvalidValues()
{
    // Arrange: every key but FOLDER invalid
    TScopedEnvironmentVariable level(TestVariable("LEVEL"), "Verbose");
    TScopedEnvironmentVariable categories(TestVariable("CATEGORIES"), "Net");
    TScopedEnvironmentVariable folder(TestVariable("FOLDER"), "diagnostics");
    TScopedEnvironmentVariable flush(TestVariable("FLUSH"), "Sometimes");
    TScopedEnvironmentVariable syncAtLevel(TestVariable("SYNC_AT_LEVEL"), "Loud");
    TScopedEnvironmentVariable async(TestVariable("ASYNC"), "maybe");
    TScopedEnvironmentVariable backtrace(TestVariable("BACKTRACE"), "-5");
    TScopedEnvironmentVariable timeZone(TestVariable("TIME_ZONE"), "Mars");
    TScopedEnvironmentVariable timePrecision(TestVariable("TIME_PRECISION"), "ps");
    TScopedEnvironmentVariable multiline(TestVariable("MULTILINE"), "Fold");
    TScopedEnvironmentVariable showFunction(TestVariable("SHOW_FUNCTION"), "2");
    TScopedEnvironmentVariable showSource(TestVariable("SHOW_SOURCE"), "yesno");
    TScopedEnvironmentVariable format(TestVariable("FORMAT"), "XML");
    TScopedEnvironmentVariable pattern(TestVariable("PATTERN"), "{nope}");

    std::vector<ASWLog::TASWLogError> errors;
    ASWLog::TASWLogConfig config;
    config.OnError = [&errors](const ASWLog::TASWLogError& error)
        {
            errors.push_back(error);
        };
    config.InitialMinimumLevel = ASWLog::Level::Warn;
    config.Backtrace.Capacity = 7;
    config.Line.ShowFunctionName = true;
    const auto formatter = std::make_shared<const ASWLog::TASWJSONFormatter>();
    config.Line.Formatter = formatter;

    // Act
    const bool isValid = config.ApplyEnvironment(TestPrefix);

    // Assert: the invalid values changed nothing, the valid one was applied
    CheckFalse(isValid, "Invalid values should make ApplyEnvironment() return false");
    CheckEquals(ASWLog::Level::Warn, config.InitialMinimumLevel, "An invalid LEVEL should leave the level");
    CheckEquals(ASWLog::FlushMode::EveryWrite, config.File.Flush, "An invalid FLUSH should leave the flush mode");
    CheckEquals(ASWLog::Level::Off, config.File.SyncToDiskAtLevel, "An invalid SYNC_AT_LEVEL should leave the level");
    CheckFalse(config.Async.Enabled, "An invalid ASYNC should leave Async.Enabled");
    CheckEquals(7, config.Backtrace.Capacity, "An invalid BACKTRACE should leave the capacity");
    CheckEquals(ASWLog::TimeZone::UTC, config.Line.TimestampZone, "An invalid TIME_ZONE should leave the zone");
    CheckEquals(ASWLog::TimePrecision::Milliseconds, config.Line.TimestampPrecision, "An invalid TIME_PRECISION should leave the precision");
    CheckEquals(ASWLog::MultilineMode::Preserve, config.Line.Multiline, "An invalid MULTILINE should leave the mode");
    CheckTrue(config.Line.ShowFunctionName, "An invalid SHOW_FUNCTION should leave the setting");
    CheckFalse(config.Line.ShowSourceLine, "An invalid SHOW_SOURCE should leave the setting");
    CheckSame(formatter.get(), config.Line.Formatter.get(), "An invalid FORMAT and PATTERN should leave the formatter");
    CheckTrue(config.File.FolderPath == std::filesystem::path("diagnostics"), "A valid value should be applied anyway");

    // Assert: one report per invalid value, in the order of the keys
    const std::vector<std::string> expectedStarts{
        "ASWLOGTEST_CONFIG_LEVEL='Verbose': expected Trace, Debug, Info, Warn, Error, Critical or Off",
        "ASWLOGTEST_CONFIG_CATEGORIES='Net': 'Net' has no '='", "ASWLOGTEST_CONFIG_FLUSH='Sometimes': expected",
        "ASWLOGTEST_CONFIG_SYNC_AT_LEVEL='Loud': expected", "ASWLOGTEST_CONFIG_ASYNC='maybe': expected",
        "ASWLOGTEST_CONFIG_BACKTRACE='-5': expected", "ASWLOGTEST_CONFIG_TIME_ZONE='Mars': expected",
        "ASWLOGTEST_CONFIG_TIME_PRECISION='ps': expected", "ASWLOGTEST_CONFIG_MULTILINE='Fold': expected",
        "ASWLOGTEST_CONFIG_SHOW_FUNCTION='2': expected", "ASWLOGTEST_CONFIG_SHOW_SOURCE='yesno': expected",
        "ASWLOGTEST_CONFIG_FORMAT='XML': expected Text or JSON", "ASWLOGTEST_CONFIG_PATTERN='{nope}': "
    };

    CheckEquals(expectedStarts.size(), errors.size(), "Each invalid value should be reported once");
    for (std::size_t index = 0; index < errors.size() && index < expectedStarts.size(); ++index)
    {
        CheckEquals(ASWLog::ErrorKind::InvalidSetting, errors[index].Kind, "Each report should be InvalidSetting");
        CheckStartsWith(errors[index].Message, expectedStarts[index], "Each report should name the variable and its value");
    }

    if (errors.size() == expectedStarts.size())
        CheckContains(errors.back().Message, "nope", "The PATTERN report should give the pattern's problem");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ApplyEnvironment_SeedsTheLevelAtInitialize()
{
    // Arrange
    TScopedEnvironmentVariable level(TestVariable("LEVEL"), "Debug");

    ASWLog::TASWLogConfig config;
    config.Startup.WriteApplicationInfo = false;
    config.Startup.WriteDriveInfo = false;
    config.Startup.WriteMemoryUsage = false;
    config.Startup.WriteOSInfo = false;
    config.Startup.WriteSystemMemoryInfo = false;
    config.Startup.WriteTimeInfo = false;
    config.Shutdown.WriteLine = false;
    ASWLog::TASWMemoryLog log;

    // Act
    config.ApplyEnvironment(TestPrefix);
    log.Initialize(config);
    log.LogDebug("debug entry");
    log.LogTrace("trace entry");
    const auto lines = log.GetLines();

    // Assert
    CheckEquals(ASWLog::Level::Debug, log.GetMinimumLevel(), "The logger should start at the variable's level");
    CheckEquals(1, lines.size(), "Only the Debug entry should be written");
    if (lines.size() == 1)
        CheckContains(lines[0], "debug entry", "The Debug entry should be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ApplyEnvironment_SetsEachKey()
{
    // Arrange: mixed case and spaces around the values
    TScopedEnvironmentVariable level(TestVariable("LEVEL"), " debug ");
    TScopedEnvironmentVariable folder(TestVariable("FOLDER"), "diag/logs");
    TScopedEnvironmentVariable file(TestVariable("FILE"), "app-diag.txt");
    TScopedEnvironmentVariable flush(TestVariable("FLUSH"), "Periodic");
    TScopedEnvironmentVariable syncAtLevel(TestVariable("SYNC_AT_LEVEL"), "warn");
    TScopedEnvironmentVariable async(TestVariable("ASYNC"), "on");
    TScopedEnvironmentVariable backtrace(TestVariable("BACKTRACE"), "250");
    TScopedEnvironmentVariable timeZone(TestVariable("TIME_ZONE"), "local");
    TScopedEnvironmentVariable timePrecision(TestVariable("TIME_PRECISION"), "us");
    TScopedEnvironmentVariable multiline(TestVariable("MULTILINE"), "Escape");
    TScopedEnvironmentVariable showFunction(TestVariable("SHOW_FUNCTION"), "YES");
    TScopedEnvironmentVariable showSource(TestVariable("SHOW_SOURCE"), "true");
    TScopedEnvironmentVariable format(TestVariable("FORMAT"), "json");

    int errorCount = 0;
    ASWLog::TASWLogConfig config;
    config.OnError = [&errorCount](const ASWLog::TASWLogError&)
        {
            ++errorCount;
        };

    // Act
    const bool isValid = config.ApplyEnvironment(TestPrefix);

    // Assert
    CheckTrue(isValid, "Every value should be valid");
    CheckEquals(0, errorCount, "Nothing should be reported");
    CheckEquals(ASWLog::Level::Debug, config.InitialMinimumLevel, "LEVEL should set InitialMinimumLevel");
    CheckTrue(config.File.FolderPath == std::filesystem::path("diag/logs"), "FOLDER should set File.FolderPath");
    CheckTrue(config.File.FilePath == std::filesystem::path("app-diag.txt"), "FILE should set File.FilePath");
    CheckEquals(ASWLog::FlushMode::Periodic, config.File.Flush, "FLUSH should set File.Flush");
    CheckEquals(ASWLog::Level::Warn, config.File.SyncToDiskAtLevel, "SYNC_AT_LEVEL should set File.SyncToDiskAtLevel");
    CheckTrue(config.Async.Enabled, "ASYNC should set Async.Enabled");
    CheckEquals(250, config.Backtrace.Capacity, "BACKTRACE should set Backtrace.Capacity");
    CheckEquals(ASWLog::TimeZone::Local, config.Line.TimestampZone, "TIME_ZONE should set Line.TimestampZone");
    CheckEquals(ASWLog::TimePrecision::Microseconds, config.Line.TimestampPrecision, "TIME_PRECISION should set Line.TimestampPrecision");
    CheckEquals(ASWLog::MultilineMode::Escape, config.Line.Multiline, "MULTILINE should set Line.Multiline");
    CheckTrue(config.Line.ShowFunctionName, "SHOW_FUNCTION should set Line.ShowFunctionName");
    CheckTrue(config.Line.ShowSourceLine, "SHOW_SOURCE should set Line.ShowSourceLine");
    CheckNotNull(dynamic_cast<const ASWLog::TASWJSONFormatter*>(config.Line.Formatter.get()), "FORMAT=json should set a JSON formatter");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ApplySetting_ReportsAnUnknownKey()
{
    // Arrange
    std::vector<ASWLog::TASWLogError> errors;
    ASWLog::TASWLogConfig config;
    config.OnError = [&errors](const ASWLog::TASWLogError& error)
        {
            errors.push_back(error);
        };

    // Act
    const bool isTypoValid = config.ApplySetting("LEVL", "Debug");
    const bool isPrefixedValid = config.ApplySetting("ASWLOG_LEVEL", "Debug");
    const bool isEmptyValid = config.ApplySetting("LEVL", "");

    // Assert
    CheckFalse(isTypoValid, "An unknown key should be invalid");
    CheckFalse(isPrefixedValid, "A key with a prefix should be unknown");
    CheckFalse(isEmptyValid, "An unknown key should be invalid with an empty value too");
    CheckEquals(ASWLog::Level::Info, config.InitialMinimumLevel, "An unknown key should change nothing");
    CheckEquals(3, errors.size(), "Each unknown key should be reported");
    if (errors.size() == 3)
    {
        CheckEquals(ASWLog::ErrorKind::InvalidSetting, errors[0].Kind, "The report should be InvalidSetting");
        CheckEquals(std::string("'LEVL' isn't a setting"), errors[0].Message, "The report should name the key");
        CheckEquals(std::string("'ASWLOG_LEVEL' isn't a setting"), errors[1].Message, "The report should name the key as given");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ApplySetting_SetsOneSetting()
{
    // Arrange
    std::vector<ASWLog::TASWLogError> errors;
    ASWLog::TASWLogConfig config;
    config.OnError = [&errors](const ASWLog::TASWLogError& error)
        {
            errors.push_back(error);
        };

    // Act & Assert: the key ignores case and spaces; an empty value changes nothing
    CheckTrue(config.ApplySetting(" level ", "Trace"), "A key should ignore case and spaces");
    CheckEquals(ASWLog::Level::Trace, config.InitialMinimumLevel, "LEVEL should be set");
    CheckTrue(config.ApplySetting("Level", "  "), "An empty value should be valid");
    CheckEquals(ASWLog::Level::Trace, config.InitialMinimumLevel, "An empty value should change nothing");

    // Act & Assert: each false value, each true value
    for (const std::string_view value : { "0", "False", "NO", "off" })
    {
        config.Line.ShowSourceLine = true;
        CheckTrue(config.ApplySetting("SHOW_SOURCE", value), "A false value should be valid: " + std::string(value));
        CheckFalse(config.Line.ShowSourceLine, "A false value should clear the setting: " + std::string(value));
    }

    for (const std::string_view value : { "1", "TRUE", "Yes", "ON" })
    {
        config.Line.ShowSourceLine = false;
        CheckTrue(config.ApplySetting("SHOW_SOURCE", value), "A true value should be valid: " + std::string(value));
        CheckTrue(config.Line.ShowSourceLine, "A true value should set the setting: " + std::string(value));
    }

    // Act & Assert: BACKTRACE takes decimal digits only, up to the largest std::size_t
    CheckTrue(config.ApplySetting("BACKTRACE", "0"), "0 should be a valid BACKTRACE");
    CheckEquals(0, config.Backtrace.Capacity, "BACKTRACE=0 should turn the backtrace off");
    config.Backtrace.Capacity = 9;
    for (const std::string_view value : { "+3", "-1", "12x", "1.5", "0x10", "99999999999999999999999" })
    {
        CheckFalse(config.ApplySetting("BACKTRACE", value), "An invalid BACKTRACE should be reported: " + std::string(value));
        CheckEquals(9, config.Backtrace.Capacity, "An invalid BACKTRACE should change nothing: " + std::string(value));
    }

    CheckEquals(6, errors.size(), "Only the invalid BACKTRACE values should be reported");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Config::Test_ApplySetting_WritesToStdErrWithoutHandler()
{
    // Arrange
    const auto stdErrFile = std::filesystem::temp_directory_path() / "aswlog_config_stderr.txt";
    ASWLog::TASWLogConfig config; // No OnError

    // Act
    bool isRedirected = false;
    bool isValid = true;
    {
        TStdErrRedirect redirect(stdErrFile);
        isRedirected = redirect.IsActive();
        if (isRedirected)
            isValid = config.ApplySetting("LEVEL", "Verbose");
    }

    if (!isRedirected)
    {
        std::error_code removeError;
        std::filesystem::remove(stdErrFile, removeError);
        Skip("stderr has no file descriptor to redirect (e.g. a GUI application)");
    }

    std::string written;
    {
        std::ifstream stream(stdErrFile, std::ios::binary);
        written.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }
    std::error_code removeError;
    std::filesystem::remove(stdErrFile, removeError);
    std::erase(written, '\r'); // In case stderr is in text mode on Windows

    // Assert
    CheckFalse(isValid, "The value should be invalid");
    CheckEquals(std::string("ASWLog: INVALID_SETTING: LEVEL='Verbose': expected Trace, Debug, Info, Warn, Error, Critical or Off\n"),
        written, "Without OnError, an invalid value should be written to stderr as one line");
}
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
    CheckNull(config.OnBeforeWrite, "OnBeforeWrite should default to unset");
    CheckEquals(1000, config.Memory.MaxLines, "Memory.MaxLines should default to 1000");
    CheckEquals(0, config.Memory.MaxBytes, "Memory.MaxBytes should default to 0 (no limit)");
    CheckFalse(config.Async.Enabled, "Async.Enabled should default to false");
    CheckEquals(8192, config.Async.QueueCapacity, "Async.QueueCapacity should default to 8192");
    CheckEquals(ASWLog::AsyncOverflowPolicy::Block, config.Async.OverflowPolicy, "Async.OverflowPolicy should default to Block");
    CheckEquals(ASWLog::Level::Error, config.Async.WaitAtLevel, "Async.WaitAtLevel should default to Error");
    CheckTrue(config.ErrorReportInterval == std::chrono::minutes(1), "ErrorReportInterval should default to 1 minute");

    // Assert: Line
    CheckNull(config.Line.Formatter, "Line.Formatter should default to unset (the built-in layout)");
    CheckEquals(ASWLog::LineEnding::LF, config.Line.Ending, "Line.Ending should default to LF");
    CheckEquals(ASWLog::MultilineMode::Preserve, config.Line.Multiline, "Line.Multiline should default to Preserve");
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
