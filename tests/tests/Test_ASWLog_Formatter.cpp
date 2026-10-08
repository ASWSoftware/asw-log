/* **************************************************************************
Test_ASWLog_Formatter.cpp
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
#include "Test_ASWLog_Formatter.h"
//---------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_CategoryLog.h"
#include "ASWLog_FileLog.h"
#include "ASWLog_Formatter.h"
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------
#include "UT_Helper_DateTime.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

const auto GroupBaseTempDir = std::filesystem::temp_directory_path() / "aswlog_formatter_tests";
const auto TestTempDir = GroupBaseTempDir / "test";

// 2026-09-21T14:13:20.123Z
const auto FixedTime = std::chrono::system_clock::time_point(std::chrono::seconds(1790000000)) + std::chrono::milliseconds(123);

// A copy of the record a formatter received (a record's views are only valid during the call)
struct TCapturedRecord
{
    std::chrono::system_clock::time_point Timestamp;
    ASWLog::Level LogLevel = ASWLog::Level::Info;
    std::string Message;
    std::uint_least32_t Line = 0;
    std::uint32_t ProcessId = 0;
    std::uint32_t ThreadId = 0;
};

// Captures each record it formats into a list owned by the test, and formats it as "LEVEL|message"
class TCapturingFormatter final : public ASWLog::IASWLogFormatter
{
private:
    std::vector<TCapturedRecord>* m_Records;

public:
    explicit TCapturingFormatter(std::vector<TCapturedRecord>& records)
        : m_Records(&records)
    {
    }

    std::string Format(const ASWLog::TASWLogRecord& record, const ASWLog::TASWLogConfig& /*config*/) const override
    {
        m_Records->push_back({ record.Timestamp, record.LogLevel, std::string(record.Message), record.Location.line(),
                               record.ProcessId, record.ThreadId });
        return std::string(ASWLog::Level_ToString(record.LogLevel)).append("|").append(record.Message);
    }
};

// Counts its calls, which may come from several loggers at once
class TCountingFormatter final : public ASWLog::IASWLogFormatter
{
public:
    mutable std::atomic<int> CallCount{ 0 };

    std::string Format(const ASWLog::TASWLogRecord& record, const ASWLog::TASWLogConfig& /*config*/) const override
    {
        ++CallCount;
        return std::string("counted|").append(record.Message);
    }
};

class TFixedClockFileLog final : public ASWLog::TASWFileLog
{
protected:
    std::chrono::system_clock::time_point NowUTC() const noexcept override
    {
        return FixedTime;
    }
};

// A config with no startup or shutdown lines and no line fields
ASWLog::TASWLogConfig MakeConfigWithoutFields(const std::filesystem::path& file)
{
    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = file;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.File.OpenRetryCount = 1;
    config.Shutdown.WriteLine = false;
    config.Startup.WriteTimeInfo = false;
    config.Startup.WriteOSInfo = false;
    config.Startup.WriteDriveInfo = false;
    config.Startup.WriteSystemMemoryInfo = false;
    config.Startup.WriteApplicationInfo = false;
    config.Startup.WriteMemoryUsage = false;
    return config;
}

ASWLog::TASWLogRecord MakeRecord(std::string_view message, std::source_location location = std::source_location::current())
{
    ASWLog::TASWLogRecord record;
    record.Timestamp = FixedTime;
    record.LogLevel = ASWLog::Level::Warn;
    record.Message = message;
    record.Location = location;
    record.ProcessId = 1234;
    record.ThreadId = 5678;
    return record;
}

std::string ReadFileText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};

    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

} // namespace

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_Formatter
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_Formatter::TTest_ASWLog_Formatter()
    : inherited("ASWLog_Formatter_Tests")
{
    RegisterTest(&TTest_ASWLog_Formatter::Test_Format_MatchesFormatLine, "Format_MatchesFormatLine");
    RegisterTest(&TTest_ASWLog_Formatter::Test_FormatLine_AllFieldsInOrder, "FormatLine_AllFieldsInOrder");
    RegisterTest(&TTest_ASWLog_Formatter::Test_FormatLine_CategoryOnlyWhenSetAndShown, "FormatLine_CategoryOnlyWhenSetAndShown");
    RegisterTest(&TTest_ASWLog_Formatter::Test_FormatLine_LargestIdsAndEachLevel, "FormatLine_LargestIdsAndEachLevel");
    RegisterTest(&TTest_ASWLog_Formatter::Test_FormatLine_MemoryFields, "FormatLine_MemoryFields");
    RegisterTest(&TTest_ASWLog_Formatter::Test_FormatLine_NoFields, "FormatLine_NoFields");
    RegisterTest(&TTest_ASWLog_Formatter::Test_FormatLine_TimestampFollowsZoneAndPrecision, "FormatLine_TimestampFollowsZoneAndPrecision");
    RegisterTest(&TTest_ASWLog_Formatter::Test_Formatter_ReceivesRecordFromLoggingThread, "Formatter_ReceivesRecordFromLoggingThread");
    RegisterTest(&TTest_ASWLog_Formatter::Test_Formatter_SharedByTwoLoggers, "Formatter_SharedByTwoLoggers");
    RegisterTest(&TTest_ASWLog_Formatter::Test_PatternFormatter_AffixesOnlyAroundAValue, "PatternFormatter_AffixesOnlyAroundAValue");
    RegisterTest(&TTest_ASWLog_Formatter::Test_PatternFormatter_BracesAndPlainText, "PatternFormatter_BracesAndPlainText");
    RegisterTest(&TTest_ASWLog_Formatter::Test_PatternFormatter_DefaultLayoutMatchesTextFormatter, "PatternFormatter_DefaultLayoutMatchesTextFormatter");
    RegisterTest(&TTest_ASWLog_Formatter::Test_PatternFormatter_EachPlaceholder, "PatternFormatter_EachPlaceholder");
    RegisterTest(&TTest_ASWLog_Formatter::Test_PatternFormatter_FileLoggerWritesItsLines, "PatternFormatter_FileLoggerWritesItsLines");
    RegisterTest(&TTest_ASWLog_Formatter::Test_PatternFormatter_InvalidPatternThrows, "PatternFormatter_InvalidPatternThrows");
    RegisterTest(&TTest_ASWLog_Formatter::Test_PatternFormatter_MemoryFields, "PatternFormatter_MemoryFields");
    RegisterTest(&TTest_ASWLog_Formatter::Test_PatternFormatter_WidthPadsShortValues, "PatternFormatter_WidthPadsShortValues");
}
//---------------------------------------------------------------------------
TTest_ASWLog_Formatter::~TTest_ASWLog_Formatter()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::SetUp_Group()
{
    Log("Setting up temp group folder: " + GroupBaseTempDir.string());
    std::filesystem::create_directories(GroupBaseTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::SetUp_Test(ITestCase& testCase)
{
    Log("  Setting up temp folder for " + testCase.GetName() + ": " + TestTempDir.string());
    std::filesystem::create_directories(TestTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::TearDown_Group()
{
    Log("Cleaning up temp group folder:" + GroupBaseTempDir.string());
    std::filesystem::remove_all(GroupBaseTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::TearDown_Test(ITestCase& testCase)
{
    Log("  Cleaning up temp folder for " + testCase.GetName() + ": " + TestTempDir.string());
    std::filesystem::remove_all(TestTempDir);
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_Format_MatchesFormatLine()
{
    // Arrange
    const ASWLog::TASWTextFormatter textFormatter;
    const ASWLog::IASWLogFormatter& formatter = textFormatter;
    const ASWLog::TASWLogConfig config;
    const auto record = MakeRecord("same layout");

    // Act
    const auto formatted = formatter.Format(record, config);

    // Assert
    CheckEquals(ASWLog::TASWTextFormatter::FormatLine(record, config), formatted,
        "A TASWTextFormatter instance should produce the built-in layout");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_FormatLine_AllFieldsInOrder()
{
    // Arrange
    ASWLog::TASWLogConfig config;
    config.Line.ShowTimestamp = true;
    config.Line.ShowLevel = true;
    config.Line.ShowProcessId = true;
    config.Line.ShowThreadId = true;
    config.Line.ShowFunctionName = true;
    config.Line.ShowSourceLine = true;
    const auto location = std::source_location::current();
    auto record = MakeRecord("all fields", location);
    record.Category = "Net";

    // Act
    const auto line = ASWLog::TASWTextFormatter::FormatLine(record, config);

    // Assert
    const auto expected = std::format("[2026-09-21T14:13:20.123Z][WARN][Net][P:1234][T:5678][{}][{}:{}]: all fields",
        location.function_name(), std::filesystem::path(location.file_name()).filename().string(), location.line());
    CheckEquals(expected, line, "Each field should be written from the record, in the documented order");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_FormatLine_CategoryOnlyWhenSetAndShown()
{
    // Arrange: the level and the category only
    auto config = MakeConfigWithoutFields("unused.log");
    config.Line.ShowLevel = true;
    auto categorized = MakeRecord("categorized");
    categorized.Category = "Net.Http";
    const auto uncategorized = MakeRecord("uncategorized");

    // Act
    const auto categorizedLine = ASWLog::TASWTextFormatter::FormatLine(categorized, config);
    const auto uncategorizedLine = ASWLog::TASWTextFormatter::FormatLine(uncategorized, config);
    config.Line.ShowCategory = false;
    const auto hiddenLine = ASWLog::TASWTextFormatter::FormatLine(categorized, config);

    // Assert
    CheckEquals(std::string("[WARN][Net.Http]: categorized"), categorizedLine, "The category should follow the level");
    CheckEquals(std::string("[WARN]: uncategorized"), uncategorizedLine, "An entry without a category should show no field for it");
    CheckEquals(std::string("[WARN]: categorized"), hiddenLine, "ShowCategory false should hide the category");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_FormatLine_LargestIdsAndEachLevel()
{
    // Arrange
    auto config = MakeConfigWithoutFields("unused.log");
    config.Line.ShowLevel = true;
    config.Line.ShowProcessId = true;
    config.Line.ShowThreadId = true;
    auto record = MakeRecord("ids");
    record.ProcessId = std::numeric_limits<std::uint32_t>::max();
    record.ThreadId = 0;

    for (const auto level : { ASWLog::Level::Trace, ASWLog::Level::Debug, ASWLog::Level::Info, ASWLog::Level::Warn,
                              ASWLog::Level::Error, ASWLog::Level::Critical })
    {
        record.LogLevel = level;

        // Act
        const auto line = ASWLog::TASWTextFormatter::FormatLine(record, config);

        // Assert
        const auto expected = std::format("[{}][P:4294967295][T:0]: ids", ASWLog::Level_ToString(level));
        CheckEquals(expected, line, "The level's name and every digit of the ids should be written");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_FormatLine_MemoryFields()
{
    // Arrange
    auto config = MakeConfigWithoutFields("unused.log");
    config.Line.ShowWorkingSet = true;
    config.Line.ShowPeakWorkingSet = true;

    // Act
    const auto line = ASWLog::TASWTextFormatter::FormatLine(MakeRecord("memory"), config);

    // Assert
    CheckMatches(line, R"(\[WS:[0-9]+\]\[PWS:[0-9]+\]: memory)",
        "The working set, then the peak working set, each a decimal number, should come before the message");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_FormatLine_NoFields()
{
    // Arrange
    const auto config = MakeConfigWithoutFields("unused.log");

    // Act
    const auto line = ASWLog::TASWTextFormatter::FormatLine(MakeRecord("just the message"), config);

    // Assert
    CheckEquals(std::string(": just the message"), line, "With every field off, only the separator and message remain");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_FormatLine_TimestampFollowsZoneAndPrecision()
{
    // Arrange: FixedTime is 2026-09-21T14:13:20.123Z, during US Eastern daylight time
    auto config = MakeConfigWithoutFields("unused.log");
    config.Line.ShowTimestamp = true;
    config.Line.TimestampZone = ASWLog::TimeZone::Local;
    config.Line.TimestampPrecision = ASWLog::TimePrecision::Microseconds;
    const TScopedTimeZone timeZone("EST5EDT");

    // Act
    const auto line = ASWLog::TASWTextFormatter::FormatLine(MakeRecord("local"), config);

    // Assert
    CheckEquals(std::string("[2026-09-21T10:13:20.123000-04:00]: local"), line, "The timestamp should be local time with microseconds and the offset");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_Formatter_ReceivesRecordFromLoggingThread()
{
    // Arrange
    std::vector<TCapturedRecord> records;
    auto config = MakeConfigWithoutFields("record.log");
    config.InitialMinimumLevel = ASWLog::Level::Debug;
    config.Line.Formatter = std::make_shared<TCapturingFormatter>(records);
    TFixedClockFileLog log;
    CheckTrue(log.Initialize(config), "Initialize should succeed");

    // Act
    std::uint32_t workerThreadId = 0;
    std::uint_least32_t loggedLine = 0;
    std::thread worker([&] {
        workerThreadId = ASWLog::GetCurrentOSThreadId();
        loggedLine = std::source_location::current().line() + 1;
        log.LogDebug("from worker");
            });
    worker.join();
    log.Close();

    // Assert
    CheckEquals(1, records.size(), "The formatter should get one record");

    if (records.size() == 1)
    {
        const auto& record = records[0];
        CheckTrue(record.Timestamp == FixedTime, "The record's time should come from the logger's NowUTC()");
        CheckEquals(ASWLog::Level::Debug, record.LogLevel, "The record should have the entry's level");
        CheckEquals(std::string("from worker"), record.Message, "The record should have the message");
        CheckEquals(loggedLine, record.Line, "The record should have the caller's source location");
        CheckEquals(ASWLog::GetCurrentOSProcessId(), record.ProcessId, "The record should have the process id");
        CheckEquals(workerThreadId, record.ThreadId, "The record should have the id of the thread that logged");
    }

    CheckEquals(std::string("DEBUG|from worker\n"), ReadFileText(TestTempDir / "record.log"),
        "The file should get the formatter's line plus the line ending");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_Formatter_SharedByTwoLoggers()
{
    // Arrange
    const auto formatter = std::make_shared<TCountingFormatter>();
    auto configA = MakeConfigWithoutFields("shared_a.log");
    auto configB = MakeConfigWithoutFields("shared_b.log");
    configA.Line.Formatter = formatter;
    configB.Line.Formatter = formatter;
    ASWLog::TASWFileLog logA;
    ASWLog::TASWFileLog logB;
    CheckTrue(logA.Initialize(configA), "Logger A should initialize");
    CheckTrue(logB.Initialize(configB), "Logger B should initialize");

    // Act
    std::thread workerA([&] {
        for (int i = 0; i < 100; ++i)
            logA.LogInfo("a");
            });
    std::thread workerB([&] {
        for (int i = 0; i < 100; ++i)
            logB.LogInfo("b");
            });
    workerA.join();
    workerB.join();
    logA.Close();
    logB.Close();

    const auto contentsA = ReadFileText(TestTempDir / "shared_a.log");
    const auto contentsB = ReadFileText(TestTempDir / "shared_b.log");

    // Assert
    CheckEquals(200, formatter->CallCount.load(), "Both loggers should call the shared formatter for each entry");
    CheckTrue(contentsA.starts_with("counted|a\n") && contentsA.size() == 100 * std::string("counted|a\n").size(),
        "Logger A's file should have its 100 lines in the shared format");
    CheckTrue(contentsB.starts_with("counted|b\n") && contentsB.size() == 100 * std::string("counted|b\n").size(),
        "Logger B's file should have its 100 lines in the shared format");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_PatternFormatter_AffixesOnlyAroundAValue()
{
    // Arrange: one record with a category and a function, one without (a default source location has empty names)
    auto withValues = MakeRecord("msg");
    withValues.Category = "Net";
    auto withoutValues = MakeRecord("msg");
    withoutValues.Location = std::source_location();
    const ASWLog::TASWLogConfig config;

    const auto formatBoth = [&](std::string_view pattern) {
            const ASWLog::TASWPatternFormatter formatter(pattern);
            return std::format("{}|{}", formatter.Format(withValues, config), formatter.Format(withoutValues, config));
        };

    // Act & Assert
    CheckEquals(std::string("[Net] msg|msg"), formatBoth("{[category] }{message}"), "Unquoted punctuation and spaces should be written only with a value");
    CheckEquals(std::string("cat=Net msg|msg"), formatBoth("{'cat=' category ' '}{message}"), "Quoted text should be written only with a value");
    CheckEquals(std::string("it's Net|"), formatBoth("{'it''s ' category}"), "'' should write a quote inside quoted text");
    CheckEquals(std::string("[cat:Net] msg|msg"), formatBoth("{['cat:'category] }{message}"), "Quoted and unquoted text should mix");
    CheckEquals(std::string("Net | msg|msg"), formatBoth("{category ' | '}{message}"), "Spaces next to quoted text should only separate it");
    CheckEquals(std::string(" | Net msg|msg"), formatBoth("{ | category }{message}"), "Unquoted spaces away from quotes should be written");
    CheckEquals(std::format("({}) msg|msg", withValues.Location.function_name()), formatBoth("{(function) }{message}"),
        "A field without a value, like an empty function name, should write nothing at all");
    CheckEquals(std::string("[Net]msg|[]msg"), formatBoth("[{category}]{message}"), "Text outside the braces should always be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_PatternFormatter_BracesAndPlainText()
{
    // Arrange
    const ASWLog::TASWLogConfig config;
    const auto record = MakeRecord("msg");

    // Act
    const auto escaped = ASWLog::TASWPatternFormatter("{{{level}}} }} {{").Format(record, config);
    const auto plain = ASWLog::TASWPatternFormatter("plain text").Format(record, config);
    const auto empty = ASWLog::TASWPatternFormatter("").Format(record, config);

    // Assert
    CheckEquals(std::string("{WARN} } {"), escaped, "{{ and }} should write a brace");
    CheckEquals(std::string("plain text"), plain, "A pattern without placeholders should write its text");
    CheckEmpty(empty, "An empty pattern should write an empty line");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_PatternFormatter_DefaultLayoutMatchesTextFormatter()
{
    // Arrange: the patterns spelling the built-in layout, with the default fields and with every field but memory use
    const ASWLog::TASWPatternFormatter defaultFields("[{time}][{level}]{[category]}[P:{pid}][T:{tid}]: {message}");
    const ASWLog::TASWPatternFormatter allFields("[{time}][{level}]{[category]}[P:{pid}][T:{tid}][{function}][{file}:{line}]: {message}");

    ASWLog::TASWLogConfig defaultConfig;
    ASWLog::TASWLogConfig localConfig;
    localConfig.Line.TimestampZone = ASWLog::TimeZone::Local;
    localConfig.Line.TimestampPrecision = ASWLog::TimePrecision::Microseconds;
    ASWLog::TASWLogConfig allConfig;
    allConfig.Line.ShowFunctionName = true;
    allConfig.Line.ShowSourceLine = true;

    std::vector<ASWLog::TASWLogRecord> records;

    for (int level = 0; level < static_cast<int>(ASWLog::LevelCount); ++level)
    {
        auto record = MakeRecord("entry");
        record.LogLevel = static_cast<ASWLog::Level>(level);
        records.push_back(record);
    }

    auto categorized = MakeRecord("categorized");
    categorized.Category = "Net.Http";
    records.push_back(categorized);
    auto largestIds = MakeRecord("largest ids");
    largestIds.ProcessId = 4294967295;
    largestIds.ThreadId = 0;
    records.push_back(largestIds);

    // Act & Assert
    for (const auto& record : records)
    {
        const auto what = std::format(" ({} {})", ASWLog::Level_ToString(record.LogLevel), record.Message);
        CheckEquals(ASWLog::TASWTextFormatter::FormatLine(record, defaultConfig), defaultFields.Format(record, defaultConfig),
            "The default layout's pattern should write the same line" + what);
        CheckEquals(ASWLog::TASWTextFormatter::FormatLine(record, localConfig), defaultFields.Format(record, localConfig),
            "{time} should follow the configured zone and precision, as the default layout does" + what);
        CheckEquals(ASWLog::TASWTextFormatter::FormatLine(record, allConfig), allFields.Format(record, allConfig),
            "The pattern with the function and source line should write the same line" + what);
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_PatternFormatter_EachPlaceholder()
{
    // Arrange
    const std::string pattern = "{time}|{level}|{category}|{pid}|{tid}|{function}|{file}|{line}|{message}";
    const ASWLog::TASWPatternFormatter formatter(pattern);
    const auto location = std::source_location::current();
    auto record = MakeRecord("the message", location);
    record.Category = "Net";

    // Act
    const auto line = formatter.Format(record, ASWLog::TASWLogConfig());

    // Assert
    const auto expected = std::format("2026-09-21T14:13:20.123Z|WARN|Net|1234|5678|{}|{}|{}|the message", location.function_name(),
        std::filesystem::path(location.file_name()).filename().string(), location.line());
    CheckEquals(expected, line, "Each placeholder should write its field");
    CheckEquals(pattern, std::string(formatter.GetPattern()), "GetPattern should return the pattern");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_PatternFormatter_FileLoggerWritesItsLines()
{
    // Arrange: CRLF, so the line ending shows that the logger still adds it
    auto config = MakeConfigWithoutFields("pattern.log");
    config.Line.Ending = ASWLog::LineEnding::CRLF;
    config.Line.ShowLevel = true; // Ignored by the pattern
    config.Line.Formatter = std::make_shared<const ASWLog::TASWPatternFormatter>("{level:5} {[category] }{message}");
    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    ASWLog::TASWCategoryLog netLog("Net", logger);

    // Act
    netLog.LogWarn("from_net");
    logger.LogInfo("plain");
    logger.LogRaw(ASWLog::Level::Info, "raw\n");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(std::string("WARN  [Net] from_net\r\nINFO  plain\r\nraw\n"), ReadFileText(TestTempDir / "pattern.log"),
        "Each line should follow the pattern, then the line ending; a raw entry is written as is");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_PatternFormatter_InvalidPatternThrows()
{
    // Arrange: each pattern, and the start of the error message it should give
    struct TCase
    {
        std::string Pattern;
        std::string Error;
    };

    const std::vector<TCase> cases{
        { "{time} {lvl}", "Unknown placeholder name 'lvl' at index 8 of the line pattern \"{time} {lvl}\"" },
        { "{time", "Unclosed '{' at index 0" },
        { "[{level]", "Unclosed '{' at index 1" },
        { "a } b", "Unmatched '}' at index 2" },
        { "{{level}", "Unmatched '}' at index 7" },
        { "{'x category}", "Unclosed quote at index 1" },
        { "{}", "Missing placeholder name at index 0" },
        { "{ [ ] }", "Missing placeholder name at index 0" },
        { "{category x}", "Letters around a placeholder name must be in quotes at index 10" },
        { "{level level}", "Letters around a placeholder name must be in quotes at index 7" },
        { "{2 level}", "'2' around a placeholder name must be in quotes at index 1" },
        { "{a{level}", "Unknown placeholder name 'a' at index 1" },
        { "{[{level}", "'{' around a placeholder name must be in quotes at index 2" },
        { "{:level}", "':' around a placeholder name must be in quotes at index 1" },
        { "{level:}", "A width must be 1 to 1000 at index 7" },
        { "{level:0}", "A width must be 1 to 1000 at index 7" },
        { "{level:1001}", "A width must be 1 to 1000 at index 7" },
        { "{level:99999999999999999999}", "A width must be 1 to 1000 at index 7" }
    };

    // Act & Assert
    for (const auto& testCase : cases)
    {
        CheckThrows<std::invalid_argument>([&] {
                [[maybe_unused]] const ASWLog::TASWPatternFormatter formatter(testCase.Pattern);
            }, "The pattern " + testCase.Pattern + " should be rejected", testCase.Error);
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_PatternFormatter_MemoryFields()
{
    // Arrange
    const ASWLog::TASWPatternFormatter formatter("{ws} {pws}: {message}");

    // Act
    const auto line = formatter.Format(MakeRecord("memory"), ASWLog::TASWLogConfig());

    // Assert
    CheckMatches(line, "[1-9][0-9]* [1-9][0-9]*: memory", "The working set and peak working set should be read, as decimal numbers");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Formatter::Test_PatternFormatter_WidthPadsShortValues()
{
    // Arrange
    const ASWLog::TASWLogConfig config;
    const ASWLog::TASWPatternFormatter padded("{level:5}|");
    const ASWLog::TASWPatternFormatter withAffixes("{[level:6]}|");
    const ASWLog::TASWPatternFormatter emptyField("{category:8}|");
    auto info = MakeRecord("msg");
    info.LogLevel = ASWLog::Level::Info;
    auto error = MakeRecord("msg");
    error.LogLevel = ASWLog::Level::Error;
    auto critical = MakeRecord("msg");
    critical.LogLevel = ASWLog::Level::Critical;

    // Act & Assert
    CheckEquals(std::string("INFO |"), padded.Format(info, config), "A shorter value should be padded on the right");
    CheckEquals(std::string("ERROR|"), padded.Format(error, config), "A value of the width should be written as is");
    CheckEquals(std::string("CRITICAL|"), padded.Format(critical, config), "A longer value should not be cut");
    CheckEquals(std::string("[INFO  ]|"), withAffixes.Format(info, config), "The affixes should go around the padded value");
    CheckEquals(std::string("|"), emptyField.Format(info, config), "An empty field should not be padded");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_Formatter)
