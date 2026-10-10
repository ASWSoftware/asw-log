/* **************************************************************************
Test_ASWLog_Unicode.cpp
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
#include "Test_ASWLog_Unicode.h"
//---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_Interface.h"
#include "ASWLog_MemoryLog.h"
#include "ASWLog_Unicode.h"
#include "ASWLog_Version.h"
//---------------------------------------------------------------------------

namespace
{

// A value whose wide formatting is counted in WideFormatCount, to check when the wide *Fmt methods format
struct TWideCountedValue
{
};

int WideFormatCount = 0;

} // namespace

template<>
struct std::formatter<TWideCountedValue, wchar_t>
{
    constexpr std::wformat_parse_context::iterator parse(std::wformat_parse_context& context)
    {
        return context.begin();
    }

    std::wformat_context::iterator format(const TWideCountedValue& /*value*/, std::wformat_context& context) const
    {
        ++WideFormatCount;
        return std::format_to(context.out(), L"counted");
    }
};

namespace ASWUnitTests
{

namespace
{

// An entry as TCountingLog received it
struct TReceivedEntry
{
    ASWLog::Level LogLevel = ASWLog::Level::Info;
    std::string Message;
    bool Raw = false;
    bool Forced = false;
    std::size_t FieldCount = 0;
    std::uint_least32_t Line = 0;
};

// A minimal logger that counts the Write() calls (the entries a logging method passed on, used or not) and keeps a
// copy of each entry it would use, filtering like TASWLogBase: enabled, not Level::Off, and the minimum level unless
// forced
class TCountingLog final : public ASWLog::IASWLog
{
private:
    bool IsUsed(const ASWLog::TASWLogRecord& record) const noexcept
    {
        return Enabled && record.LogLevel != ASWLog::Level::Off && (record.Forced || record.LogLevel >= MinimumLevel);
    }

public:
    ASWLog::Level MinimumLevel = ASWLog::Level::Info;
    bool Enabled = true;
    int WriteCount = 0;
    std::vector<TReceivedEntry> Entries;

    std::string_view GetVersionStr() const noexcept override
    {
        return ASWLog::Version;
    }

    std::string GetFullVersionStr() const override
    {
        return "TCountingLog";
    }

    std::shared_ptr<const ASWLog::TASWLogConfig> GetConfig() const noexcept override
    {
        return nullptr;
    }

    bool Initialize(const ASWLog::TASWLogConfig& /*config*/) noexcept override
    {
        return true;
    }

    bool Reconfigure(const ASWLog::TASWLogConfig& /*config*/) noexcept override
    {
        return true;
    }

    bool Open() noexcept override
    {
        return true;
    }

    bool Close() noexcept override
    {
        return true;
    }

    bool Flush() noexcept override
    {
        return true;
    }

    bool IsOpen() const noexcept override
    {
        return true;
    }

    void DumpBacktrace() noexcept override
    {
    }

    bool IsEnabled() const noexcept override
    {
        return Enabled;
    }

    void SetEnabled(bool enabled) noexcept override
    {
        Enabled = enabled;
    }

    ASWLog::Level GetMinimumLevel() const noexcept override
    {
        return MinimumLevel;
    }

    void SetMinimumLevel(ASWLog::Level level) noexcept override
    {
        MinimumLevel = level;
    }

    bool ShouldLog(ASWLog::Level level) const noexcept override
    {
        ASWLog::TASWLogRecord record;
        record.LogLevel = level;

        return IsUsed(record);
    }

    bool ShouldLog(const ASWLog::TASWLogRecord& record) const noexcept override
    {
        return IsUsed(record);
    }

    void Write(const ASWLog::TASWLogRecord& record) noexcept override
    {
        ++WriteCount;
        if (!IsUsed(record))
            return;

        TReceivedEntry entry;
        entry.LogLevel = record.LogLevel;
        entry.Message = std::string(record.Message);
        entry.Raw = record.Raw;
        entry.Forced = record.Forced;
        entry.FieldCount = record.GetOwnFields().size();
        entry.Line = record.Location.line();
        Entries.push_back(entry);
    }
};

// What a TReceivedEntry should hold
struct TExpectedEntry
{
    ASWLog::Level LogLevel;
    std::string Message;
    bool Raw;
    bool Forced;
    std::size_t FieldCount;
};

// A code point and its UTF-8 bytes
struct TCodePoint
{
    char32_t Value;
    std::string UTF8;
};

// How the entries 'log' received differ from 'expected', in order: empty if they don't, else the first difference
std::string DescribeEntryMismatch(const TCountingLog& log, const std::vector<TExpectedEntry>& expected)
{
    if (log.Entries.size() != expected.size())
        return std::format("{} entries instead of {}", log.Entries.size(), expected.size());

    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const auto& entry = log.Entries[index];
        const auto& item = expected[index];
        if (entry.LogLevel != item.LogLevel || entry.Message != item.Message || entry.Raw != item.Raw ||
            entry.Forced != item.Forced || entry.FieldCount != item.FieldCount)
        {
            return std::format("entry {}: {} '{}' raw={} forced={} fields={} instead of {} '{}' raw={} forced={} fields={}", index,
                ASWLog::Level_ToString(entry.LogLevel), entry.Message, entry.Raw, entry.Forced, entry.FieldCount,
                ASWLog::Level_ToString(item.LogLevel), item.Message, item.Raw, item.Forced, item.FieldCount);
        }
    }

    return {};
}

// A config with no startup or shutdown lines and only the level in each line, e.g. "[INFO]: message"
ASWLog::TASWLogConfig MakeQuietConfig()
{
    ASWLog::TASWLogConfig config;
    config.Line.ShowTimestamp = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Shutdown.WriteLine = false;
    config.Startup.WriteApplicationInfo = false;
    config.Startup.WriteDriveInfo = false;
    config.Startup.WriteMemoryUsage = false;
    config.Startup.WriteOSInfo = false;
    config.Startup.WriteSystemMemoryInfo = false;
    config.Startup.WriteTimeInfo = false;
    return config;
}

// The bytes of 'text' in hex, e.g. "E2 82", for a check's message
std::string ToHex(std::string_view text)
{
    std::string hex;
    for (const char character : text)
        hex += std::format("{}{:02X}", hex.empty() ? "" : " ", static_cast<unsigned char>(character));

    return hex;
}

// 'value' (a Unicode scalar value) as UTF-16, written out here rather than with the code the tests check
std::u16string ToUTF16(char32_t value)
{
    if (value < 0x10000)
        return std::u16string(1, static_cast<char16_t>(value));

    const char32_t offset = value - 0x10000;

    return std::u16string{ static_cast<char16_t>(0xD800 + (offset >> 10)), static_cast<char16_t>(0xDC00 + (offset & 0x3FF)) };
}

// 'value' as a wide string: UTF-16 where wchar_t has 16 bits, else the value itself
std::wstring ToWideString(char32_t value)
{
    if constexpr (sizeof(wchar_t) == sizeof(char16_t))
    {
        const auto utf16 = ToUTF16(value);

        return std::wstring(utf16.begin(), utf16.end());
    }
    else
    {
        return std::wstring(1, static_cast<wchar_t>(value));
    }
}

} // namespace

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_Unicode
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_Unicode::TTest_ASWLog_Unicode()
    : inherited("ASWLog_Unicode_Tests")
{
    RegisterTest(&TTest_ASWLog_Unicode::Test_Formatters_ConvertTheWrappedText, "Formatters_ConvertTheWrappedText");
    RegisterTest(&TTest_ASWLog_Unicode::Test_LogFmt_NarrowFormatTakesWideArguments, "LogFmt_NarrowFormatTakesWideArguments");
    RegisterTest(&TTest_ASWLog_Unicode::Test_LogFmt_WideFormatSkipsFilteredEntries, "LogFmt_WideFormatSkipsFilteredEntries");
    RegisterTest(&TTest_ASWLog_Unicode::Test_LogFmt_WideFormatWritesUTF8, "LogFmt_WideFormatWritesUTF8");
    RegisterTest(&TTest_ASWLog_Unicode::Test_LogWide_ConvertsOnlyEntriesThatAreUsed, "LogWide_ConvertsOnlyEntriesThatAreUsed");
    RegisterTest(&TTest_ASWLog_Unicode::Test_LogWide_EachMethodWritesUTF8, "LogWide_EachMethodWritesUTF8");
    RegisterTest(&TTest_ASWLog_Unicode::Test_LogWide_WritesThroughARealLogger, "LogWide_WritesThroughARealLogger");
    RegisterTest(&TTest_ASWLog_Unicode::Test_RoundTrip_KeepsEveryCharacter, "RoundTrip_KeepsEveryCharacter");
    RegisterTest(&TTest_ASWLog_Unicode::Test_UTF8ToUTF16_ReplacesInvalidSequences, "UTF8ToUTF16_ReplacesInvalidSequences");
    RegisterTest(&TTest_ASWLog_Unicode::Test_UTF8ToWide_FollowsTheWidthOfWchar, "UTF8ToWide_FollowsTheWidthOfWchar");
    RegisterTest(&TTest_ASWLog_Unicode::Test_WideText_TakesEachKindOfText, "WideText_TakesEachKindOfText");
    RegisterTest(&TTest_ASWLog_Unicode::Test_WideToUTF8_ReplacesWhatIsNotACharacter, "WideToUTF8_ReplacesWhatIsNotACharacter");
}
//---------------------------------------------------------------------------
TTest_ASWLog_Unicode::~TTest_ASWLog_Unicode()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::Test_Formatters_ConvertTheWrappedText()
{
    // Arrange
    const std::wstring wideEmoji = ToWideString(0x1F600);

    // Act
    const auto narrowWide = std::format("[{}]", ASWLog::Wide(L"a\x03BB"));
    const auto narrowUTF16 = std::format("[{:>5}]", ASWLog::Wide(u"ab"));
    const auto narrowUTF8 = std::format("[{:<4}]", ASWLog::UTF8("ab"));
    const auto wideUTF8 = std::format(L"[{}]", ASWLog::UTF8("a\xCE\xBB"));
    const auto wideUTF8Padded = std::format(L"[{:<4}]", ASWLog::UTF8("ab"));
    const auto wideUTF16 = std::format(L"[{}]", ASWLog::Wide(u"\xD83D\xDE00"));
    const auto wideWide = std::format(L"[{}]", ASWLog::Wide(L"x"));

    // Assert
    CheckEquals(std::string("[a\xCE\xBB]"), narrowWide, "Wide() should format wchar_t text as UTF-8");
    CheckEquals(std::string("[   ab]"), narrowUTF16, "Wide() should format char16_t text, with a width");
    CheckEquals(std::string("[ab  ]"), narrowUTF8, "UTF8() should format as it is in a narrow format");
    CheckTrue(wideUTF8 == L"[a\x03BB]", "UTF8() should format UTF-8 as wide text in a wide format");
    CheckTrue(wideUTF8Padded == L"[ab  ]", "UTF8() should take a width in a wide format");
    CheckTrue(wideUTF16 == L"[" + wideEmoji + L"]", "Wide() should format char16_t text in a wide format");
    CheckTrue(wideWide == L"[x]", "Wide() should format wchar_t text as it is in a wide format");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::Test_LogFmt_NarrowFormatTakesWideArguments()
{
    // Arrange
    TCountingLog log;

    // Act
    log.LogInfoFmt("{} and {}", ASWLog::Wide(L"\x03BB"), ASWLog::Wide(u"\xD83D\xDE00"));
    log.LogWarnFmt("[{:>3}]", ASWLog::Wide(std::wstring(L"a")));

    // Assert
    CheckEmpty(DescribeEntryMismatch(log, {
            { ASWLog::Level::Info, "\xCE\xBB and \xF0\x9F\x98\x80", false, false, 0 },
            { ASWLog::Level::Warn, "[  a]", false, false, 0 },
        }), "Each call should pass on one entry with its level, flags, fields and UTF-8 message");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::Test_LogFmt_WideFormatSkipsFilteredEntries()
{
    // Arrange
    TCountingLog log;
    log.MinimumLevel = ASWLog::Level::Warn;
    WideFormatCount = 0;

    // Act
    log.LogInfoFmt(L"{}", TWideCountedValue{});
    log.LogFmt(ASWLog::Level::Debug, L"{}", TWideCountedValue{});
    log.LogRawFmt(ASWLog::Level::Info, L"{}", TWideCountedValue{});
    log.LogDebugFmt({ { "key", 1 } }, L"{}", TWideCountedValue{});
    log.LogForceFmt(ASWLog::Level::Off, L"{}", TWideCountedValue{});
    const int filteredCount = WideFormatCount;

    log.LogWarnFmt(L"{}", TWideCountedValue{});
    log.LogForceFmt(ASWLog::Level::Debug, L"{}", TWideCountedValue{});
    log.LogForceRawFmt(ASWLog::Level::Trace, { { "key", 1 } }, L"{}", TWideCountedValue{});
    const int writtenCount = WideFormatCount;

    log.Enabled = false;
    log.LogForceFmt(ASWLog::Level::Error, L"{}", TWideCountedValue{});
    const int disabledCount = WideFormatCount;

    // Assert
    CheckEquals(0, filteredCount, "An entry that wouldn't be written should not be formatted");
    CheckEquals(3, writtenCount, "An entry that is written should be formatted once");
    CheckEquals(3, disabledCount, "A forced entry should not be formatted while the logger is disabled");
    CheckEquals(3, log.Entries.size(), "Only the used entries should be kept");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::Test_LogFmt_WideFormatWritesUTF8()
{
    // Arrange
    const std::wstring runtimeFormat = L"{} {}";
    TCountingLog log;
    log.MinimumLevel = ASWLog::Level::Trace;

    // Act: each method, arguments of each kind
    const auto line = std::source_location::current().line();
    log.LogFmt(ASWLog::Level::Warn, L"fmt {} {}", 1, L"\x03BB");
    log.LogRawFmt(ASWLog::Level::Info, L"raw {}", ASWLog::UTF8("\xE6\x97\xA5"));
    log.LogForceFmt(ASWLog::Level::Debug, L"force {}", ASWLog::Wide(u"\x03BB"));
    log.LogForceRawFmt(ASWLog::Level::Trace, L"forceraw {:.1f}", 2.25);
    log.LogTraceFmt(L"trace {}", 1);
    log.LogDebugFmt(L"debug {}", 2);
    log.LogInfoFmt(L"info {}", 3);
    log.LogWarnFmt(L"warn {}", 4);
    log.LogErrorFmt(L"error {}", 5);
    log.LogCriticalFmt(L"critical {}", 6);

    log.LogFmt(ASWLog::Level::Warn, { { "k", 1 } }, L"fmt {}", 1);
    log.LogRawFmt(ASWLog::Level::Info, { { "k", 1 } }, L"raw {}", 2);
    log.LogForceFmt(ASWLog::Level::Debug, { { "k", 1 } }, L"force {}", 3);
    log.LogForceRawFmt(ASWLog::Level::Trace, { { "k", 1 } }, L"forceraw {}", 4);
    log.LogTraceFmt({ { "k", 1 } }, L"trace {}", 5);
    log.LogDebugFmt({ { "k", 1 } }, L"debug {}", 6);
    log.LogInfoFmt({ { "k", 1 } }, L"info {}", 7);
    log.LogWarnFmt({ { "k", 1 }, { "j", 2 } }, L"warn {}", 8);
    log.LogErrorFmt({ { "k", 1 } }, L"error {}", 9);
    log.LogCriticalFmt({ { "k", 1 } }, L"critical {}", 10);

    log.LogInfoFmt(ASWLog::RuntimeFormat(runtimeFormat), 1, L"\x03BB");

    // Assert
    CheckEmpty(DescribeEntryMismatch(log, {
            { ASWLog::Level::Warn, "fmt 1 \xCE\xBB", false, false, 0 },
            { ASWLog::Level::Info, "raw \xE6\x97\xA5", true, false, 0 },
            { ASWLog::Level::Debug, "force \xCE\xBB", false, true, 0 },
            { ASWLog::Level::Trace, "forceraw 2.2", true, true, 0 },
            { ASWLog::Level::Trace, "trace 1", false, false, 0 },
            { ASWLog::Level::Debug, "debug 2", false, false, 0 },
            { ASWLog::Level::Info, "info 3", false, false, 0 },
            { ASWLog::Level::Warn, "warn 4", false, false, 0 },
            { ASWLog::Level::Error, "error 5", false, false, 0 },
            { ASWLog::Level::Critical, "critical 6", false, false, 0 },
            { ASWLog::Level::Warn, "fmt 1", false, false, 1 },
            { ASWLog::Level::Info, "raw 2", true, false, 1 },
            { ASWLog::Level::Debug, "force 3", false, true, 1 },
            { ASWLog::Level::Trace, "forceraw 4", true, true, 1 },
            { ASWLog::Level::Trace, "trace 5", false, false, 1 },
            { ASWLog::Level::Debug, "debug 6", false, false, 1 },
            { ASWLog::Level::Info, "info 7", false, false, 1 },
            { ASWLog::Level::Warn, "warn 8", false, false, 2 },
            { ASWLog::Level::Error, "error 9", false, false, 1 },
            { ASWLog::Level::Critical, "critical 10", false, false, 1 },
            { ASWLog::Level::Info, "1 \xCE\xBB", false, false, 0 },
        }), "Each call should pass on one entry with its level, flags, fields and UTF-8 message");

    if (!log.Entries.empty())
        CheckEquals(line + 1, log.Entries[0].Line, "The entry should have the caller's source line");

    // Act: a runtime format string with too few arguments
    log.LogInfoFmt(ASWLog::RuntimeFormat(runtimeFormat), 1);

    // Assert
    CheckEquals(22, log.Entries.size(), "The mismatched entry should still be written");
    if (log.Entries.size() == 22)
    {
        CheckStartsWith(log.Entries[21].Message, std::string("[ASWLog format error: "), "A mismatched runtime format should be described");
        CheckEndsWith(log.Entries[21].Message, std::string("] {} {}"), "The description should end with the format string");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::Test_LogWide_ConvertsOnlyEntriesThatAreUsed()
{
    // Arrange
    TCountingLog log;
    log.MinimumLevel = ASWLog::Level::Warn;

    // Act: entries that wouldn't be used, then entries that would
    log.LogInfo(L"info");
    log.LogDebug(u"debug", { { "key", 1 } });
    log.Log(ASWLog::Level::Info, L"info");
    log.LogRaw(ASWLog::Level::Trace, u"trace");
    log.LogForce(ASWLog::Level::Off, L"off");
    const int filteredWriteCount = log.WriteCount;

    log.LogWarn(L"warn");
    log.LogForce(ASWLog::Level::Debug, u"forced");
    const int usedWriteCount = log.WriteCount;

    log.Enabled = false;
    log.LogForceRaw(ASWLog::Level::Error, L"disabled");
    const int disabledWriteCount = log.WriteCount;

    // Assert
    CheckEquals(0, filteredWriteCount, "An entry that wouldn't be used should not be passed on (nor converted)");
    CheckEquals(2, usedWriteCount, "An entry that would be used should be passed on");
    CheckEquals(2, disabledWriteCount, "A forced entry should not be passed on while the logger is disabled");
    CheckEquals(2, log.Entries.size(), "The used entries should be kept");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::Test_LogWide_EachMethodWritesUTF8()
{
    // Arrange
    TCountingLog log;
    log.MinimumLevel = ASWLog::Level::Trace;

    // Act: each method, with wchar_t and char16_t text of each kind
    const auto line = std::source_location::current().line();
    log.Log(ASWLog::Level::Warn, L"log \x03BB");
    log.LogRaw(ASWLog::Level::Info, u"raw \x03BB");
    log.LogForce(ASWLog::Level::Debug, std::wstring(L"force"));
    log.LogForceRaw(ASWLog::Level::Trace, std::u16string(u"forceraw"));
    log.LogTrace(L"trace");
    log.LogDebug(u"debug");
    log.LogInfo(std::wstring_view(L"info"));
    log.LogWarn(std::u16string_view(u"warn"));
    log.LogError(std::wstring(L"error ") + ToWideString(0x1F600)); // A surrogate pair only where wchar_t has 16 bits
    log.LogCritical(u"critical \xD83D\xDE00");

    const auto fieldsLine = std::source_location::current().line();
    log.Log(ASWLog::Level::Warn, L"log", { { "k", 1 } });
    log.LogRaw(ASWLog::Level::Info, u"raw", { { "k", 1 } });
    log.LogForce(ASWLog::Level::Debug, L"force", { { "k", 1 } });
    log.LogForceRaw(ASWLog::Level::Trace, u"forceraw", { { "k", 1 } });
    log.LogTrace(L"trace", { { "k", 1 } });
    log.LogDebug(u"debug", { { "k", 1 } });
    log.LogInfo(L"info", { { "k", 1 }, { "j", 2 } });
    log.LogWarn(u"warn", { { "k", 1 } });
    log.LogError(L"error", { { "k", 1 } });
    log.LogCritical(u"critical", { { "k", 1 } });

    // Assert
    CheckEmpty(DescribeEntryMismatch(log, {
            { ASWLog::Level::Warn, "log \xCE\xBB", false, false, 0 },
            { ASWLog::Level::Info, "raw \xCE\xBB", true, false, 0 },
            { ASWLog::Level::Debug, "force", false, true, 0 },
            { ASWLog::Level::Trace, "forceraw", true, true, 0 },
            { ASWLog::Level::Trace, "trace", false, false, 0 },
            { ASWLog::Level::Debug, "debug", false, false, 0 },
            { ASWLog::Level::Info, "info", false, false, 0 },
            { ASWLog::Level::Warn, "warn", false, false, 0 },
            { ASWLog::Level::Error, "error \xF0\x9F\x98\x80", false, false, 0 },
            { ASWLog::Level::Critical, "critical \xF0\x9F\x98\x80", false, false, 0 },
            { ASWLog::Level::Warn, "log", false, false, 1 },
            { ASWLog::Level::Info, "raw", true, false, 1 },
            { ASWLog::Level::Debug, "force", false, true, 1 },
            { ASWLog::Level::Trace, "forceraw", true, true, 1 },
            { ASWLog::Level::Trace, "trace", false, false, 1 },
            { ASWLog::Level::Debug, "debug", false, false, 1 },
            { ASWLog::Level::Info, "info", false, false, 2 },
            { ASWLog::Level::Warn, "warn", false, false, 1 },
            { ASWLog::Level::Error, "error", false, false, 1 },
            { ASWLog::Level::Critical, "critical", false, false, 1 },
        }), "Each call should pass on one entry with its level, flags, fields and UTF-8 message");

    if (log.Entries.size() == 20)
    {
        CheckEquals(line + 1, log.Entries[0].Line, "An entry should have the caller's source line");
        CheckEquals(fieldsLine + 1, log.Entries[10].Line, "An entry with fields should have the caller's source line");
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::Test_LogWide_WritesThroughARealLogger()
{
    // Arrange
    ASWLog::TASWMemoryLog log;
    log.Initialize(MakeQuietConfig());

    // Act
    log.LogInfo(L"Opened \x03BB.txt");
    log.LogWarnFmt(L"{} of {}", 3, ASWLog::UTF8("\xE6\x97\xA5"));
    log.LogErrorFmt("{} failed", ASWLog::Wide(u"\x03BB"));
    log.LogDebug(L"filtered");
    const auto lines = log.GetLines();

    // Assert
    const std::vector<std::string> expected{ "[INFO]: Opened \xCE\xBB.txt", "[WARN]: 3 of \xE6\x97\xA5", "[ERROR]: \xCE\xBB failed" };
    CheckEquals(expected.size(), lines.size(), "Each entry at or above Info should be written");
    for (std::size_t index = 0; index < expected.size() && index < lines.size(); ++index)
        CheckEquals(expected[index], lines[index], "The line should hold the message in UTF-8");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::Test_RoundTrip_KeepsEveryCharacter()
{
    // Arrange: the first and last characters of each UTF-8 length, around the surrogates, and the largest
    const std::vector<TCodePoint> codePoints{
        { 0x0000, std::string(1, '\0') }, { 0x007F, "\x7F" }, { 0x0080, "\xC2\x80" }, { 0x07FF, "\xDF\xBF" },
        { 0x0800, "\xE0\xA0\x80" }, { 0xD7FF, "\xED\x9F\xBF" }, { 0xE000, "\xEE\x80\x80" }, { 0xFFFD, "\xEF\xBF\xBD" },
        { 0xFFFF, "\xEF\xBF\xBF" }, { 0x10000, "\xF0\x90\x80\x80" }, { 0x10FFFF, "\xF4\x8F\xBF\xBF" }
    };

    for (const auto& item : codePoints)
    {
        // Act
        const std::u16string utf16 = ToUTF16(item.Value);
        const std::wstring wide = ToWideString(item.Value);

        const auto fromUTF16 = ASWLog::WideToUTF8(utf16);
        const auto fromWide = ASWLog::WideToUTF8(wide);
        const auto toUTF16 = ASWLog::UTF8ToUTF16(item.UTF8);
        const auto toWide = ASWLog::UTF8ToWide(item.UTF8);

        // Assert
        const std::string what = ": " + ToHex(item.UTF8);
        CheckEquals(item.UTF8, fromUTF16, "UTF-16 should become UTF-8" + what);
        CheckEquals(item.UTF8, fromWide, "A wide string should become UTF-8" + what);
        CheckTrue(toUTF16 == utf16, "UTF-8 should become UTF-16" + what);
        CheckTrue(toWide == wide, "UTF-8 should become a wide string" + what);
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::Test_UTF8ToUTF16_ReplacesInvalidSequences()
{
    // Arrange: valid text, then invalid sequences, each replaced by one U+FFFD per longest start of a valid sequence
    struct TCase
    {
        std::string Text;
        std::u16string Expected;
    };

    const std::vector<TCase> cases{
        { "", u"" },
        { "a\xCE\xBB" "\xE6\x97\xA5" "\xF0\x9F\x98\x80", u"a\x03BB\x65E5\xD83D\xDE00" },
        { "x\xFFy", u"x\xFFFDy" },                                                       // Not a lead byte
        { "\x80x", u"\xFFFDx" },                                                         // A lone continuation byte
        { "\xC0\xAF", u"\xFFFD\xFFFD" },                                                 // Overlong 2-byte form
        { "\xE0\x80\x80", u"\xFFFD\xFFFD\xFFFD" },                                       // Overlong 3-byte form
        { "\xED\xA0\x80", u"\xFFFD\xFFFD\xFFFD" },                                       // A surrogate
        { "\xF4\x90\x80\x80", u"\xFFFD\xFFFD\xFFFD\xFFFD" },                             // Above U+10FFFF
        { "\xE6\x97" "x", u"\xFFFDx" },                                                  // Cut short by another character
        { "a\xF0\x9F\x98", u"a\xFFFD" },                                                 // Cut short by the end
    };

    for (const auto& item : cases)
    {
        // Act
        const auto result = ASWLog::UTF8ToUTF16(item.Text);

        // Assert
        CheckTrue(result == item.Expected, "UTF-8 should become UTF-16, invalid sequences U+FFFD: " + ToHex(item.Text));
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::Test_UTF8ToWide_FollowsTheWidthOfWchar()
{
    // Arrange
    const std::wstring expected = std::wstring(L"a") + ToWideString(0x1F600) + ToWideString(0xFFFD);

    // Act
    const auto result = ASWLog::UTF8ToWide("a\xF0\x9F\x98\x80\xFF");

    // Assert
    CheckTrue(result == expected, "UTF-8 should become UTF-16 where wchar_t has 16 bits, else UTF-32");
    CheckEquals(sizeof(wchar_t) == sizeof(char16_t) ? 4 : 3, result.size(), "A character above U+FFFF should be two "
        "wchar_t where it has 16 bits, else one");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::Test_WideText_TakesEachKindOfText()
{
    // Arrange
    const auto toUTF8 = [](ASWLog::TASWWideText text)
        {
            return text.ToUTF8();
        };
    const wchar_t* nullWide = nullptr;
    const char16_t* nullUTF16 = nullptr;
    const std::wstring wideString = L"wide \x03BB";
    const std::u16string utf16String = u"utf16 \x03BB";

    // Act & Assert: made implicitly from each kind of text
    CheckEquals(std::string("a\xCE\xBB"), toUTF8(L"a\x03BB"), "A wchar_t literal should be taken");
    CheckEquals(std::string("a\xCE\xBB"), toUTF8(u"a\x03BB"), "A char16_t literal should be taken");
    CheckEquals(std::string("wide \xCE\xBB"), toUTF8(wideString), "A std::wstring should be taken");
    CheckEquals(std::string("utf16 \xCE\xBB"), toUTF8(utf16String), "A std::u16string should be taken");
    CheckEquals(std::string("wide"), toUTF8(std::wstring_view(wideString).substr(0, 4)), "A std::wstring_view should be taken");
    CheckEquals(std::string("utf16"), toUTF8(std::u16string_view(utf16String).substr(0, 5)), "A std::u16string_view should be taken");
    CheckEquals(std::string(), toUTF8(nullWide), "A null wchar_t pointer should be empty text");
    CheckEquals(std::string(), toUTF8(nullUTF16), "A null char16_t pointer should be empty text");

    // Act & Assert: as a wide string
    CheckTrue(ASWLog::Wide(u"\xD83D\xDE00").ToWide() == ToWideString(0x1F600), "char16_t text should become a wide string");
    CheckTrue(ASWLog::Wide(L"x\x03BB").ToWide() == L"x\x03BB", "wchar_t text should stay as it is");
    const std::wstring unpaired = sizeof(wchar_t) == sizeof(char16_t) ? std::wstring(1, static_cast<wchar_t>(0xD83D)) :
            ToWideString(0xFFFD);
    CheckTrue(ASWLog::Wide(u"\xD83D").ToWide() == unpaired,
        "An unpaired surrogate should stay where wchar_t has 16 bits, and become U+FFFD where it has 32");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Unicode::Test_WideToUTF8_ReplacesWhatIsNotACharacter()
{
    // Arrange: UTF-16 with unpaired surrogates
    struct TCase
    {
        std::u16string Text;
        std::string Expected;
    };

    const std::vector<TCase> cases{
        { u"", "" },
        { u"a\x03BB\xD83D\xDE00", "a\xCE\xBB\xF0\x9F\x98\x80" },
        { u"x\xD83D", "x\xEF\xBF\xBD" },                                                  // A high surrogate at the end
        { u"\xDE00y", "\xEF\xBF\xBDy" },                                                  // A lone low surrogate
        { u"\xD83Dz", "\xEF\xBF\xBDz" },                                                  // A high surrogate, then no low one
        { u"\xD83D\xD83D\xDE00", "\xEF\xBF\xBD\xF0\x9F\x98\x80" },                        // Two high surrogates, then a low one
    };

    // Act & Assert: wide text is UTF-16 too where wchar_t has 16 bits
    for (const auto& item : cases)
    {
        CheckEquals(item.Expected, ASWLog::WideToUTF8(item.Text), "UTF-16 should become UTF-8: " + ToHex(item.Expected));
        if constexpr (sizeof(wchar_t) == sizeof(char16_t))
            CheckEquals(item.Expected, ASWLog::WideToUTF8(std::wstring(item.Text.begin(), item.Text.end())),
                "16-bit wide text should become UTF-8 as UTF-16 does: " + ToHex(item.Expected));
    }

    // Arrange: values that aren't characters, which a 32-bit wchar_t holds one each
    if constexpr (sizeof(wchar_t) != sizeof(char16_t))
    {
        const std::uint32_t surrogate = 0xD800;
        const std::uint32_t tooLarge = 0x110000;
        const std::wstring invalid{ static_cast<wchar_t>(surrogate), static_cast<wchar_t>(tooLarge), static_cast<wchar_t>(0x1F600) };

        // Act & Assert
        CheckEquals(std::string("\xEF\xBF\xBD\xEF\xBF\xBD\xF0\x9F\x98\x80"), ASWLog::WideToUTF8(invalid),
            "A surrogate or a value above U+10FFFF should become U+FFFD");
    }
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_Unicode)
