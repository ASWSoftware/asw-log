/* **************************************************************************
Test_ASWLog_WindowsEventLog.cpp
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
#include "Test_ASWLog_WindowsEventLog.h"
//---------------------------------------------------------------------------
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_CrashHandler.h"
#include "ASWLog_Utils.h"
#include "ASWLog_WindowsEventLog.h"
//---------------------------------------------------------------------------

namespace
{

// A value whose formatting is counted in EventLogFormatCount, to check whether a *Fmt call formats
struct TEventLogCountedValue
{
};

int EventLogFormatCount = 0;

} // namespace

template<>
struct std::formatter<TEventLogCountedValue>
{
    constexpr std::format_parse_context::iterator parse(std::format_parse_context& context)
    {
        return context.begin();
    }

    std::format_context::iterator format(const TEventLogCountedValue& /*value*/, std::format_context& context) const
    {
        ++EventLogFormatCount;
        return std::format_to(context.out(), "counted");
    }
};

namespace ASWUnitTests
{

namespace
{

using EventType = ASWLog::TASWWindowsEventLog::EventType;

// A config with no startup or shutdown lines and only the level in each line, e.g. "[INFO]: message"
ASWLog::TASWLogConfig MakeEventLogConfig()
{
    ASWLog::TASWLogConfig config;
    config.InitialMinimumLevel = ASWLog::Level::Info;
    config.Line.ShowTimestamp = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Shutdown.WriteLine = false;
    config.Startup.WriteTimeInfo = false;
    config.Startup.WriteOSInfo = false;
    config.Startup.WriteDriveInfo = false;
    config.Startup.WriteSystemMemoryInfo = false;
    config.Startup.WriteApplicationInfo = false;
    config.Startup.WriteMemoryUsage = false;
    return config;
}

// Counts the lines it formats
class TCountingFormatter final : public ASWLog::IASWLogFormatter
{
public:
    mutable int FormatCount = 0;

    std::string Format(const ASWLog::TASWLogRecord& record, const ASWLog::TASWLogConfig& /*config*/) const override
    {
        ++FormatCount;
        return std::string(record.Message);
    }
};

// An Event Log logger that keeps the sources it would open and the events it would report, on every platform (where
// there is no Event Log, the logger's own ShouldLog() and PrepareWriteUnlocked() would drop every entry)
class TCapturingEventLog final : public ASWLog::TASWWindowsEventLog
{
public:
    struct TEvent
    {
        EventType Type = EventType::Information;
        std::uint16_t EventId = 0;
        std::string Text;
    };

    std::vector<std::string> Sources;
    std::vector<TEvent> Events;

protected:
    bool OpenEventSourceUnlocked(const std::string& source) override
    {
        Sources.push_back(source);
        return true;
    }

    bool PrepareWriteUnlocked(std::chrono::system_clock::time_point /*now*/) override
    {
        return true;
    }

    void ReportEventUnlocked(EventType type, std::uint16_t eventId, std::string_view message) override
    {
        Events.push_back({ type, eventId, std::string(message) });
    }

public:
    // The overrides above are used until the shutdown line
    ~TCapturingEventLog() override
    {
        Finalize();
    }

    bool ShouldLog(ASWLog::Level level) const noexcept override
    {
        return ASWLog::TASWTextLogBase::ShouldLog(level);
    }

    bool ShouldLog(const ASWLog::TASWLogRecord& record) const noexcept override
    {
        return ASWLog::TASWTextLogBase::ShouldLog(record);
    }

    void WriteCrashLine(std::string_view line) noexcept
    {
        WriteCrashLineDirect(line);
    }
};

#if defined(_WIN32)
// An Event Log logger that reports each entry as an event too long for the Event Log, which refuses it
class TOversizedEventLog final : public ASWLog::TASWWindowsEventLog
{
protected:
    void WriteLineUnlocked(ASWLog::Level /*level*/, std::string_view /*line*/, bool /*endsLine*/) override
    {
        ReportEventUnlocked(EventType::Warning, 1003, std::string(40000, 'x'));
    }

public:
    // The override above is used until the shutdown line
    ~TOversizedEventLog() override
    {
        Finalize();
    }
};

// An event read back from the Application log
struct TReadEvent
{
    WORD Type = 0;
    DWORD EventId = 0;
    std::wstring Text;
};

// The newest event of 'source' in the Application log whose text contains 'token', or nothing if none came within two
// seconds (the newest 1000 events are searched)
std::optional<TReadEvent> FindEvent(const std::wstring& source, const std::wstring& token)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

    do
    {
        const HANDLE log = OpenEventLogW(nullptr, L"Application");
        if (log == nullptr)
            return std::nullopt;

        std::vector<BYTE> buffer(64 * 1024);
        int recordCount = 0;
        DWORD readSize = 0;
        DWORD neededSize = 0;

        while (recordCount < 1000)
        {
            if (!ReadEventLogW(log, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 0, buffer.data(), static_cast<DWORD>(buffer.size()), &readSize, &neededSize))
            {
                if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                    break;

                buffer.resize(neededSize);
                continue;
            }

            for (DWORD offset = 0; offset < readSize; ++recordCount)
            {
                const auto* record = reinterpret_cast<const EVENTLOGRECORD*>(buffer.data() + offset);
                const std::wstring recordSource(reinterpret_cast<const wchar_t*>(record + 1));
                const std::wstring text = record->NumStrings > 0 ? std::wstring(reinterpret_cast<const wchar_t*>(buffer.data() + offset + record->StringOffset)) : std::wstring();

                if (recordSource == source && text.find(token) != std::wstring::npos)
                {
                    CloseEventLog(log);
                    return TReadEvent{ record->EventType, record->EventID, text };
                }

                offset += record->Length;
            }
        }

        CloseEventLog(log);
    } while (std::chrono::steady_clock::now() < deadline);

    return std::nullopt;
}

// True if the process runs with administrator rights (elevated)
bool IsElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool isElevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated != 0;
    CloseHandle(token);

    return isElevated;
}

// The EventMessageFile value of 'source''s registry key (empty if it has none), or nothing if it has no key
std::optional<std::wstring> ReadMessageFile(const std::wstring& source)
{
    const std::wstring key = L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\" + source;
    HKEY sourceKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_QUERY_VALUE, &sourceKey) != ERROR_SUCCESS)
        return std::nullopt;

    wchar_t value[MAX_PATH]{};
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(sourceKey, nullptr, L"EventMessageFile", RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, value, &size);
    RegCloseKey(sourceKey);

    return status == ERROR_SUCCESS ? std::wstring(value) : std::wstring();
}

// 'text' (ASCII) as a wide string
std::wstring Widen(std::string_view text)
{
    return std::wstring(text.begin(), text.end());
}
#endif

} // namespace

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_WindowsEventLog
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_WindowsEventLog::TTest_ASWLog_WindowsEventLog()
    : inherited("ASWLog_WindowsEventLog_Tests")
{
    RegisterTest(&TTest_ASWLog_WindowsEventLog::Test_GetInstance_ReturnsTheSameLogger, "GetInstance_ReturnsTheSameLogger");
    RegisterTest(&TTest_ASWLog_WindowsEventLog::Test_HandleCrash_ReportsTheFormattedCrashLineOnly, "HandleCrash_ReportsTheFormattedCrashLineOnly");
    RegisterTest(&TTest_ASWLog_WindowsEventLog::Test_Open_OpensTheConfiguredSource, "Open_OpensTheConfiguredSource");
    RegisterTest(&TTest_ASWLog_WindowsEventLog::Test_RegisterSource_NeedsAdministratorRights, "RegisterSource_NeedsAdministratorRights");
    RegisterTest(&TTest_ASWLog_WindowsEventLog::Test_Report_CutsTooLongMessages, "Report_CutsTooLongMessages");
    RegisterTest(&TTest_ASWLog_WindowsEventLog::Test_Report_MapsEachLevelToATypeAndEventId, "Report_MapsEachLevelToATypeAndEventId");
    RegisterTest(&TTest_ASWLog_WindowsEventLog::Test_Report_StripsOnlyTheLineEnding, "Report_StripsOnlyTheLineEnding");
    RegisterTest(&TTest_ASWLog_WindowsEventLog::Test_ShouldLog_FollowsThePlatform, "ShouldLog_FollowsThePlatform");
    RegisterTest(&TTest_ASWLog_WindowsEventLog::Test_Windows_ReportsAFailedOpen, "Windows_ReportsAFailedOpen");
    RegisterTest(&TTest_ASWLog_WindowsEventLog::Test_Windows_ReportsARealEvent, "Windows_ReportsARealEvent");
    RegisterTest(&TTest_ASWLog_WindowsEventLog::Test_Windows_ReportsARefusedEvent, "Windows_ReportsARefusedEvent");
}
//---------------------------------------------------------------------------
TTest_ASWLog_WindowsEventLog::~TTest_ASWLog_WindowsEventLog()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::SetUp_Test(ITestCase& /*testCase*/)
{
    EventLogFormatCount = 0;
}
//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::Test_GetInstance_ReturnsTheSameLogger()
{
    // Act
    auto& first = ASWLog::TASWWindowsEventLog::GetInstance();
    auto& second = ASWLog::TASWWindowsEventLog::GetInstance();

    // Assert
    CheckSame(&first, &second, "GetInstance() should always return the same logger");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::Test_HandleCrash_ReportsTheFormattedCrashLineOnly()
{
    // Arrange
    TCapturingEventLog log;
    CheckTrue(log.Initialize(MakeEventLogConfig()), "Initialize should succeed");

    // Act: a crash in normal context, then the direct write a crash with a busy lock would use
    ASWLog::HandleCrash("test crash");
    log.WriteCrashLine("[CRITICAL]: Crash: direct\n");

    // Assert
    AssertEquals(1, log.Events.size(), "Only the formatted crash line should be reported");
    CheckEquals(EventType::Error, log.Events[0].Type, "The crash line should be an Error event");
    CheckEquals(1005, log.Events[0].EventId, "The crash line should have Critical's event ID");
    CheckEquals(std::string("[CRITICAL]: Crash: test crash"), log.Events[0].Text, "The crash line should be reported without its line ending");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::Test_Open_OpensTheConfiguredSource()
{
    // Arrange
    TCapturingEventLog log;
    const auto config = MakeEventLogConfig();
    auto appConfig = config;
    appConfig.EventLog.Source = "MyApp";
    const std::string executableStem = ASWLog::PathToUTF8String(ASWLog::GetExecutablePath().stem());

    // Act
    const bool initialized = log.Initialize(config);
    const bool reconfigured = log.Reconfigure(appConfig);
    const bool closed = log.Close();
    const bool opened = log.Open();

    // Assert
    CheckTrue(initialized && reconfigured && closed && opened, "Initialize, Reconfigure, Close and Open should succeed");
    CheckEmpty(config.EventLog.Source, "The default source should be empty (the executable's file name)");
    AssertEquals(3, log.Sources.size(), "Initialize, Reconfigure and Open should each open the source");
    CheckNotEmpty(executableStem, "The executable's file name should be known");
    CheckEquals(executableStem, log.Sources[0], "Without a source, the executable's file name without its extension should be used");
    CheckEquals(std::string("MyApp"), log.Sources[1], "Reconfigure should open the new source");
    CheckEquals(std::string("MyApp"), log.Sources[2], "Open after Close should open the current config's source again");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::Test_RegisterSource_NeedsAdministratorRights()
{
#if defined(_WIN32)
    // Arrange: a source of this test's own; elevated (e.g. on a CI runner) it is registered and removed again
    const std::wstring source = L"ASWLogTests_Registration";
    const bool isElevated = IsElevated();
    if (isElevated)
        ASWLog::TASWWindowsEventLog::UnregisterSource("ASWLogTests_Registration");

    // Act
    const bool registered = ASWLog::TASWWindowsEventLog::RegisterSource("ASWLogTests_Registration");
    const auto messageFile = ReadMessageFile(source);
    const bool unregistered = ASWLog::TASWWindowsEventLog::UnregisterSource("ASWLogTests_Registration");
    const auto messageFileAfter = ReadMessageFile(source);
    const bool registeredNested = ASWLog::TASWWindowsEventLog::RegisterSource("ASWLogTests_Registration\\Nested");
    const bool unregisteredNested = ASWLog::TASWWindowsEventLog::UnregisterSource("ASWLogTests_Registration\\Nested");

    // Assert
    CheckEquals(isElevated, registered, "RegisterSource() should succeed exactly when the process is elevated");
    if (isElevated)
    {
        CheckTrue(messageFile == std::wstring(L"%SystemRoot%\\Microsoft.NET\\Framework\\v4.0.30319\\EventLogMessages.dll"), "The source's message file should be EventLogMessages.dll");
        CheckTrue(unregistered, "UnregisterSource() should succeed when elevated");
    }
    else
    {
        CheckTrue(unregistered, "UnregisterSource() of a source that isn't registered should succeed");
    }
    CheckFalse(messageFile.has_value() && !isElevated, "Without administrator rights, no registry key should be made");
    CheckFalse(messageFileAfter.has_value(), "After UnregisterSource(), the source should have no registry key");
    CheckFalse(registeredNested, "A source holding a '\\' should not be registered");
    CheckFalse(unregisteredNested, "A source holding a '\\' names no source to unregister");
#else
    // Act & Assert
    CheckFalse(ASWLog::TASWWindowsEventLog::RegisterSource("ASWLogTests_Registration"), "Without an Event Log, RegisterSource() should fail");
    CheckFalse(ASWLog::TASWWindowsEventLog::UnregisterSource("ASWLogTests_Registration"), "Without an Event Log, UnregisterSource() should fail");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::Test_Report_CutsTooLongMessages()
{
    // Arrange: "[INFO]: " is 8 bytes. The longer line has 2-byte characters from byte MaxSize - 9 on, so the cut, which
    // keeps MaxSize - 6 bytes for the " [cut]" marker, falls inside one and backs off to the character before it.
    constexpr std::size_t MaxSize = ASWLog::TASWWindowsEventLog::MaxMessageSize;
    TCapturingEventLog log;
    CheckTrue(log.Initialize(MakeEventLogConfig()), "Initialize should succeed");
    const std::string fitting(MaxSize - 8, 'x');
    std::string tooLong(MaxSize - 8 - 9, 'y');
    for (int i = 0; i < 20; ++i)
        tooLong += "\xC3\xA9";

    // Act
    log.LogInfo(fitting);
    log.LogInfo(tooLong);

    // Assert
    CheckEquals(31839, MaxSize, "The limit should be the Event Log's 31,839 characters");
    AssertEquals(2, log.Events.size(), "Each entry should be one event");
    CheckEquals("[INFO]: " + fitting, log.Events[0].Text, "A message that fits should be reported whole");
    CheckLessThanOrEqual(log.Events[1].Text.size(), MaxSize, "A longer one should be cut to fit");
    CheckEquals(MaxSize - 1, log.Events[1].Text.size(), "The cut should back off to the start of a 2-byte character");
    CheckStartsWith(log.Events[1].Text, "[INFO]: yyy", "The start of the message should be kept");
    CheckEndsWith(log.Events[1].Text, "\xC3\xA9 [cut]", "The cut should end with a whole character and the marker");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::Test_Report_MapsEachLevelToATypeAndEventId()
{
    // Arrange
    TCapturingEventLog log;
    auto config = MakeEventLogConfig();
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    auto tableConfig = config;
    tableConfig.EventLog.EventIds = { 10, 20, 30, 40, 50, 65535 };
    CheckTrue(log.Initialize(config), "Initialize should succeed");

    // Act
    log.LogTrace("trace");
    log.LogDebug("debug");
    log.LogInfo("info");
    log.LogWarn("warn");
    log.LogError("error");
    log.LogCritical("critical");
    log.LogRaw(ASWLog::Level::Warn, "raw");
    CheckTrue(log.Reconfigure(tableConfig), "Reconfigure should succeed");
    log.LogInfo("info");
    log.LogCritical("critical");

    // Assert
    AssertEquals(9, log.Events.size(), "Each entry should be one event");
    const EventType types[] = { EventType::Information, EventType::Information, EventType::Information, EventType::Warning, EventType::Error, EventType::Error, EventType::Warning, EventType::Information, EventType::Error };
    const int eventIds[] = { 1000, 1001, 1002, 1003, 1004, 1005, 1003, 30, 65535 };
    for (std::size_t i = 0; i < log.Events.size(); ++i)
    {
        CheckEquals(types[i], log.Events[i].Type, std::format("Event {} ({}) should have its level's type", i, log.Events[i].Text));
        CheckEquals(eventIds[i], log.Events[i].EventId, std::format("Event {} ({}) should have its level's event ID", i, log.Events[i].Text));
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::Test_Report_StripsOnlyTheLineEnding()
{
    // Arrange
    TCapturingEventLog log;
    auto crlfConfig = MakeEventLogConfig();
    crlfConfig.Line.Ending = ASWLog::LineEnding::CRLF;
    CheckTrue(log.Initialize(MakeEventLogConfig()), "Initialize should succeed");

    // Act
    log.LogInfo("hello");
    log.LogInfo("ends with a break\n");
    log.LogRaw(ASWLog::Level::Info, "raw text\n");
    CheckTrue(log.Reconfigure(crlfConfig), "Reconfigure should succeed");
    log.LogInfo("crlf");
    log.LogInfo("ends with LF\n");

    // Assert
    AssertEquals(5, log.Events.size(), "Each entry should be one event");
    CheckEquals(std::string("[INFO]: hello"), log.Events[0].Text, "The LF ending should be stripped");
    CheckEquals(std::string("[INFO]: ends with a break\n"), log.Events[1].Text, "Only the line ending should be stripped, not the message's own break");
    CheckEquals(std::string("raw text\n"), log.Events[2].Text, "A raw entry should be reported as is");
    CheckEquals(std::string("[INFO]: crlf"), log.Events[3].Text, "The CRLF ending should be stripped");
    CheckEquals(std::string("[INFO]: ends with LF\n"), log.Events[4].Text, "With CRLF endings, the message's own LF should stay");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::Test_ShouldLog_FollowsThePlatform()
{
#if defined(_WIN32)
    // Arrange: no startup or shutdown lines, and only checks, so nothing reaches the Event Log
    ASWLog::TASWWindowsEventLog log;
    ASWLog::TASWLogRecord infoRecord;
    infoRecord.LogLevel = ASWLog::Level::Info;

    // Act
    const bool initialized = log.Initialize(MakeEventLogConfig());
    log.LogDebugFmt("{}", TEventLogCountedValue{});

    // Assert
    CheckTrue(initialized, "Initialize should succeed on every platform");
    CheckEquals(0, EventLogFormatCount, "An entry below the minimum level should not be formatted");
    CheckTrue(log.ShouldLog(ASWLog::Level::Info), "On Windows, ShouldLog() should follow the minimum level (Info)");
    CheckFalse(log.ShouldLog(ASWLog::Level::Debug), "On Windows, ShouldLog() should follow the minimum level (Debug)");
    CheckTrue(log.ShouldLog(infoRecord), "On Windows, ShouldLog(record) should follow the minimum level");
#else
    // Arrange: a startup and a shutdown line too, each of which would be formatted
    auto config = MakeEventLogConfig();
    const auto formatter = std::make_shared<const TCountingFormatter>();
    config.Line.Formatter = formatter;
    config.Startup.Banner = "banner";
    config.Shutdown.WriteLine = true;
    ASWLog::TASWLogRecord criticalRecord;
    criticalRecord.LogLevel = ASWLog::Level::Critical;
    bool initialized = false;
    bool shouldLog = true;
    bool shouldLogRecord = true;

    // Act
    {
        ASWLog::TASWWindowsEventLog log;
        initialized = log.Initialize(config);
        shouldLog = log.ShouldLog(ASWLog::Level::Critical);
        shouldLogRecord = log.ShouldLog(criticalRecord);
        log.LogCriticalFmt("{}", TEventLogCountedValue{});
        log.LogCritical("not formatted");
    }

    // Assert
    CheckTrue(initialized, "Initialize should succeed on every platform");
    CheckFalse(shouldLog, "Without an Event Log, ShouldLog() should be false");
    CheckFalse(shouldLogRecord, "Without an Event Log, ShouldLog(record) should be false");
    CheckEquals(0, EventLogFormatCount, "Without an Event Log, no *Fmt arguments should be formatted");
    CheckEquals(0, formatter->FormatCount, "Without an Event Log, no line (startup, entry or shutdown) should be formatted");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::Test_Windows_ReportsAFailedOpen()
{
#if defined(_WIN32)
    // Arrange: only the system (LSA) may report to the Security log, so other processes can't open it as a source
    ASWLog::TASWWindowsEventLog log;
    auto config = MakeEventLogConfig();
    config.EventLog.Source = "Security";
    std::vector<ASWLog::TASWLogError> errors;
    config.OnError = [&errors](const ASWLog::TASWLogError& error) {
            errors.push_back(error);
        };

    // Act
    const bool initialized = log.Initialize(config);
    if (initialized)
        Skip("This process may report to the Security log");

    // Assert
    AssertEquals(1, errors.size(), "The failed open should be reported");
    CheckEquals(ASWLog::ErrorKind::OpenFailed, errors[0].Kind, "It should be an OpenFailed error");
    CheckContains(errors[0].Message, "'Security'", "It should name the source");
    CheckEquals(ERROR_ACCESS_DENIED, errors[0].Code.value(), "It should carry the system's error");
    CheckFalse(log.IsOpen(), "The logger should not be open");
#else
    Skip("Only Windows has an Event Log");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::Test_Windows_ReportsARealEvent()
{
#if defined(_WIN32)
    // Arrange: one Warning event of a test source in the Application log, found again by a token of this run
    ASWLog::TASWWindowsEventLog log;
    auto config = MakeEventLogConfig();
    config.EventLog.Source = "ASWLogTests";
    config.EventLog.EventIds[static_cast<std::size_t>(ASWLog::Level::Warn)] = 4321;
    const std::string token = std::format("token {}", std::chrono::steady_clock::now().time_since_epoch().count());
    CheckTrue(log.Initialize(config), "Initialize should succeed");

    // Act
    log.LogWarn("ASWLog unit test event, " + token);
    const auto event = FindEvent(L"ASWLogTests", Widen(token));

    // Assert
    AssertTrue(event.has_value(), "The event should be in the Application log");
    CheckEquals(EVENTLOG_WARNING_TYPE, event->Type, "It should be a Warning event");
    CheckEquals(4321, event->EventId & 0xFFFF, "It should have Warn's event ID from the config");
    CheckTrue(event->Text == Widen("[WARN]: ASWLog unit test event, " + token), "Its text should be the line without its ending");
#else
    Skip("Only Windows has an Event Log");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_WindowsEventLog::Test_Windows_ReportsARefusedEvent()
{
#if defined(_WIN32)
    // Arrange: a real source, and an event the Event Log refuses (ERROR_INVALID_PARAMETER), so nothing is written
    TOversizedEventLog log;
    auto config = MakeEventLogConfig();
    config.EventLog.Source = "ASWLogTests";
    std::vector<ASWLog::TASWLogError> errors;
    config.OnError = [&errors](const ASWLog::TASWLogError& error) {
            errors.push_back(error);
        };
    CheckTrue(log.Initialize(config), "Initialize should succeed");

    // Act
    log.LogWarn("refused");

    // Assert
    AssertEquals(1, errors.size(), "The refused event should be reported");
    CheckEquals(ASWLog::ErrorKind::WriteFailed, errors[0].Kind, "It should be a WriteFailed error");
    CheckEquals(ERROR_INVALID_PARAMETER, errors[0].Code.value(), "It should carry the system's error");
#else
    Skip("Only Windows has an Event Log");
#endif
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_WindowsEventLog)
