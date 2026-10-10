/* **************************************************************************
Test_ASWLog_SyslogLog.cpp
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
#include "Test_ASWLog_SyslogLog.h"
//---------------------------------------------------------------------------
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_CrashHandler.h"
#include "ASWLog_SyslogLog.h"
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace
{

// A value whose formatting is counted in SyslogLogFormatCount, to check whether a *Fmt call formats
struct TSyslogLogCountedValue
{
};

int SyslogLogFormatCount = 0;

} // namespace

template<>
struct std::formatter<TSyslogLogCountedValue>
{
    constexpr std::format_parse_context::iterator parse(std::format_parse_context& context)
    {
        return context.begin();
    }

    std::format_context::iterator format(const TSyslogLogCountedValue& /*value*/, std::format_context& context) const
    {
        ++SyslogLogFormatCount;
        return std::format_to(context.out(), "counted");
    }
};

namespace ASWUnitTests
{

namespace
{

// The syslog priorities of the LOG_USER facility (8) with each severity, as in <syslog.h>
constexpr int UserCritical = 8 | 2;
constexpr int UserError    = 8 | 3;
constexpr int UserWarning  = 8 | 4;
constexpr int UserInfo     = 8 | 6;
constexpr int UserDebug    = 8 | 7;

//---------------------------------------------------------------------------

// A config with no startup or shutdown lines and only the level in each line, e.g. "[INFO]: message"
ASWLog::TASWLogConfig MakeSyslogConfig()
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

// A syslog logger that keeps what it would pass to openlog() and syslog(), on every platform (on Windows, the
// logger's own ShouldLog() and PrepareWriteUnlocked() would drop every entry)
class TCapturingSyslogLog final : public ASWLog::TASWSyslogLog
{
public:
    struct TOpen
    {
        std::string Ident;
        ASWLog::SyslogFacility Facility = ASWLog::SyslogFacility::User;
    };

    struct TMessage
    {
        int Priority = 0;
        std::string Text;
    };

    std::vector<TOpen> Opens;
    std::vector<TMessage> Messages;

protected:
    void OpenSyslogUnlocked(const std::string& ident, ASWLog::SyslogFacility facility) override
    {
        Opens.push_back({ ident, facility });
    }

    bool PrepareWriteUnlocked(std::chrono::system_clock::time_point /*now*/) override
    {
        return true;
    }

    void SendToSyslogUnlocked(int priority, std::string_view message) override
    {
        Messages.push_back({ priority, std::string(message) });
    }

public:
    // The overrides above are used until the shutdown line
    ~TCapturingSyslogLog() override
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

} // namespace

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_SyslogLog
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_SyslogLog::TTest_ASWLog_SyslogLog()
    : inherited("ASWLog_SyslogLog_Tests")
{
    RegisterTest(&TTest_ASWLog_SyslogLog::Test_GetInstance_ReturnsTheSameLogger, "GetInstance_ReturnsTheSameLogger");
    RegisterTest(&TTest_ASWLog_SyslogLog::Test_HandleCrash_SendsTheFormattedCrashLineOnly, "HandleCrash_SendsTheFormattedCrashLineOnly");
    RegisterTest(&TTest_ASWLog_SyslogLog::Test_Initialize_OpensSyslogWithTheIdentAndFacility, "Initialize_OpensSyslogWithTheIdentAndFacility");
    RegisterTest(&TTest_ASWLog_SyslogLog::Test_Linux_SendsToTheRealSyslog, "Linux_SendsToTheRealSyslog");
    RegisterTest(&TTest_ASWLog_SyslogLog::Test_Send_FollowsTheMultilineMode, "Send_FollowsTheMultilineMode");
    RegisterTest(&TTest_ASWLog_SyslogLog::Test_Send_MapsEachLevelToAPriority, "Send_MapsEachLevelToAPriority");
    RegisterTest(&TTest_ASWLog_SyslogLog::Test_Send_StripsOnlyTheLineEnding, "Send_StripsOnlyTheLineEnding");
    RegisterTest(&TTest_ASWLog_SyslogLog::Test_ShouldLog_FollowsThePlatform, "ShouldLog_FollowsThePlatform");
}
//---------------------------------------------------------------------------
TTest_ASWLog_SyslogLog::~TTest_ASWLog_SyslogLog()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_SyslogLog::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_SyslogLog::SetUp_Test(ITestCase& /*testCase*/)
{
    SyslogLogFormatCount = 0;
}
//---------------------------------------------------------------------------
void TTest_ASWLog_SyslogLog::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_SyslogLog::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_SyslogLog::Test_GetInstance_ReturnsTheSameLogger()
{
    // Act
    auto& first = ASWLog::TASWSyslogLog::GetInstance();
    auto& second = ASWLog::TASWSyslogLog::GetInstance();

    // Assert
    CheckSame(&first, &second, "GetInstance() should always return the same logger");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_SyslogLog::Test_HandleCrash_SendsTheFormattedCrashLineOnly()
{
    // Arrange
    TCapturingSyslogLog log;
    CheckTrue(log.Initialize(MakeSyslogConfig()), "Initialize should succeed");

    // Act: a crash in normal context, then the direct write a POSIX signal handler (or a busy lock) would use
    ASWLog::HandleCrash("test crash");
    log.WriteCrashLine("[CRITICAL]: Crash: direct\n");

    // Assert
    AssertEquals(1, log.Messages.size(), "Only the formatted crash line should be sent");
    CheckEquals(UserCritical, log.Messages[0].Priority, "The crash line should be sent as LOG_CRIT");
    CheckEquals(std::string("[CRITICAL]: Crash: test crash"), log.Messages[0].Text, "The crash line should be sent without its line ending");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_SyslogLog::Test_Initialize_OpensSyslogWithTheIdentAndFacility()
{
    // Arrange
    TCapturingSyslogLog log;
    const auto config = MakeSyslogConfig();
    auto appConfig = config;
    appConfig.Syslog.Ident = "MyApp";
    appConfig.Syslog.Facility = ASWLog::SyslogFacility::Local3;
    const std::string executableName = ASWLog::PathToUTF8String(ASWLog::GetExecutablePath().filename());

    // Act
    const bool initialized = log.Initialize(config);
    const bool reconfigured = log.Reconfigure(appConfig);
    const bool closed = log.Close();
    const bool opened = log.Open();

    // Assert
    CheckTrue(initialized && reconfigured && closed && opened, "Initialize, Reconfigure, Close and Open should succeed");
    CheckEmpty(config.Syslog.Ident, "The default ident should be empty (the executable's file name)");
    CheckEquals(ASWLog::SyslogFacility::User, config.Syslog.Facility, "The default facility should be User");
    AssertEquals(2, log.Opens.size(), "Initialize and Reconfigure should open syslog; Close and Open should not");
    CheckNotEmpty(executableName, "The executable's file name should be known");
    CheckEquals(executableName, log.Opens[0].Ident, "Without an ident, the executable's file name should be used");
    CheckEquals(ASWLog::SyslogFacility::User, log.Opens[0].Facility, "Initialize should pass the configured facility");
    CheckEquals(std::string("MyApp"), log.Opens[1].Ident, "Reconfigure should pass the new ident");
    CheckEquals(ASWLog::SyslogFacility::Local3, log.Opens[1].Facility, "Reconfigure should pass the new facility");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_SyslogLog::Test_Linux_SendsToTheRealSyslog()
{
#if defined(_WIN32)
    Skip("Only Linux has a syslog");
#else
    // Arrange: the real openlog() and syslog() (each Reconfigure replaces the ident's copy). This writes three Debug
    // lines to the system log; nothing here can read them back, so it only checks that the calls work.
    ASWLog::TASWSyslogLog log;
    auto config = MakeSyslogConfig();
    config.InitialMinimumLevel = ASWLog::Level::Debug;
    config.Syslog.Ident = "ASWLogTests";
    auto otherConfig = config;
    otherConfig.Syslog.Ident = "ASWLogTests_Other";
    otherConfig.Syslog.Facility = ASWLog::SyslogFacility::Local7;

    // Act
    const bool initialized = log.Initialize(config);
    log.LogDebug("ASWLog unit test: syslog smoke test (1 of 3)");
    const bool reconfigured = log.Reconfigure(otherConfig);
    log.LogDebug("ASWLog unit test: syslog smoke test (2 of 3)");
    const bool reconfiguredBack = log.Reconfigure(config);
    log.LogDebug("ASWLog unit test: syslog smoke test (3 of 3)");
    const bool flushed = log.Flush();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(reconfigured && reconfiguredBack, "Reconfigure should succeed");
    CheckTrue(flushed, "Flush should succeed while open");
    CheckTrue(log.ShouldLog(ASWLog::Level::Debug), "ShouldLog() should follow the minimum level on Linux");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_SyslogLog::Test_Send_FollowsTheMultilineMode()
{
    // Arrange
    TCapturingSyslogLog log;
    auto indentConfig = MakeSyslogConfig();
    indentConfig.Line.Multiline = ASWLog::MultilineMode::Indent;
    auto escapeConfig = MakeSyslogConfig();
    escapeConfig.Line.Multiline = ASWLog::MultilineMode::Escape;
    CheckTrue(log.Initialize(MakeSyslogConfig()), "Initialize should succeed");

    // Act
    log.LogInfo("first\nsecond");
    CheckTrue(log.Reconfigure(indentConfig), "Reconfigure should succeed");
    log.LogInfo("first\nsecond");
    CheckTrue(log.Reconfigure(escapeConfig), "Reconfigure should succeed");
    log.LogInfo("first\r\nsecond");

    // Assert
    AssertEquals(3, log.Messages.size(), "Each entry should be one message");
    CheckEquals(std::string("[INFO]: first\nsecond"), log.Messages[0].Text, "Preserve should keep the line break");
    CheckEquals(std::string("[INFO]: first\n    | second"), log.Messages[1].Text, "Indent should mark the next line");
    CheckEquals(std::string("[INFO]: first\\r\\nsecond"), log.Messages[2].Text, "Escape should write \\r and \\n");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_SyslogLog::Test_Send_MapsEachLevelToAPriority()
{
    // Arrange
    TCapturingSyslogLog log;
    auto config = MakeSyslogConfig();
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    auto local3Config = config;
    local3Config.Syslog.Facility = ASWLog::SyslogFacility::Local3;
    auto daemonConfig = config;
    daemonConfig.Syslog.Facility = ASWLog::SyslogFacility::Daemon;
    CheckTrue(log.Initialize(config), "Initialize should succeed");

    // Act
    log.LogTrace("trace");
    log.LogDebug("debug");
    log.LogInfo("info");
    log.LogWarn("warn");
    log.LogError("error");
    log.LogCritical("critical");
    log.LogRaw(ASWLog::Level::Warn, "raw");
    CheckTrue(log.Reconfigure(local3Config), "Reconfigure should succeed");
    log.LogError("local3");
    CheckTrue(log.Reconfigure(daemonConfig), "Reconfigure should succeed");
    log.LogInfo("daemon");

    // Assert
    AssertEquals(9, log.Messages.size(), "Each entry should be one message");
    CheckEquals(UserDebug, log.Messages[0].Priority, "Trace should be LOG_DEBUG");
    CheckEquals(UserDebug, log.Messages[1].Priority, "Debug should be LOG_DEBUG");
    CheckEquals(UserInfo, log.Messages[2].Priority, "Info should be LOG_INFO");
    CheckEquals(UserWarning, log.Messages[3].Priority, "Warn should be LOG_WARNING");
    CheckEquals(UserError, log.Messages[4].Priority, "Error should be LOG_ERR");
    CheckEquals(UserCritical, log.Messages[5].Priority, "Critical should be LOG_CRIT");
    CheckEquals(UserWarning, log.Messages[6].Priority, "A raw entry should get its level's priority");
    CheckEquals((19 << 3) | 3, log.Messages[7].Priority, "The facility should be sent with each message (LOG_LOCAL3 | LOG_ERR)");
    CheckEquals((3 << 3) | 6, log.Messages[8].Priority, "The facility should be sent with each message (LOG_DAEMON | LOG_INFO)");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_SyslogLog::Test_Send_StripsOnlyTheLineEnding()
{
    // Arrange
    TCapturingSyslogLog log;
    auto crlfConfig = MakeSyslogConfig();
    crlfConfig.Line.Ending = ASWLog::LineEnding::CRLF;
    CheckTrue(log.Initialize(MakeSyslogConfig()), "Initialize should succeed");

    // Act
    log.LogInfo("hello");
    log.LogInfo("ends with a break\n");
    log.LogRaw(ASWLog::Level::Info, "raw text\n");
    CheckTrue(log.Reconfigure(crlfConfig), "Reconfigure should succeed");
    log.LogInfo("crlf");
    log.LogInfo("ends with LF\n");

    // Assert
    AssertEquals(5, log.Messages.size(), "Each entry should be one message");
    CheckEquals(std::string("[INFO]: hello"), log.Messages[0].Text, "The LF ending should be stripped");
    CheckEquals(std::string("[INFO]: ends with a break\n"), log.Messages[1].Text, "Only the line ending should be stripped, not the message's own break");
    CheckEquals(std::string("raw text\n"), log.Messages[2].Text, "A raw entry should be sent as is");
    CheckEquals(std::string("[INFO]: crlf"), log.Messages[3].Text, "The CRLF ending should be stripped");
    CheckEquals(std::string("[INFO]: ends with LF\n"), log.Messages[4].Text, "With CRLF endings, the message's own LF should stay");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_SyslogLog::Test_ShouldLog_FollowsThePlatform()
{
#if defined(_WIN32)
    // Arrange: a startup and a shutdown line too, each of which would be formatted
    auto config = MakeSyslogConfig();
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
        ASWLog::TASWSyslogLog log;
        initialized = log.Initialize(config);
        shouldLog = log.ShouldLog(ASWLog::Level::Critical);
        shouldLogRecord = log.ShouldLog(criticalRecord);
        log.LogCriticalFmt("{}", TSyslogLogCountedValue{});
        log.LogCritical("not formatted");
    }

    // Assert
    CheckTrue(initialized, "Initialize should succeed on every platform");
    CheckFalse(shouldLog, "Without a syslog, ShouldLog() should be false");
    CheckFalse(shouldLogRecord, "Without a syslog, ShouldLog(record) should be false");
    CheckEquals(0, SyslogLogFormatCount, "Without a syslog, no *Fmt arguments should be formatted");
    CheckEquals(0, formatter->FormatCount, "Without a syslog, no line (startup, entry or shutdown) should be formatted");
#else
    // Arrange: no startup or shutdown lines, and only checks, so nothing reaches the system log
    ASWLog::TASWSyslogLog log;
    ASWLog::TASWLogRecord infoRecord;
    infoRecord.LogLevel = ASWLog::Level::Info;

    // Act
    const bool initialized = log.Initialize(MakeSyslogConfig());
    log.LogDebugFmt("{}", TSyslogLogCountedValue{});

    // Assert
    CheckTrue(initialized, "Initialize should succeed on every platform");
    CheckEquals(0, SyslogLogFormatCount, "An entry below the minimum level should not be formatted");
    CheckTrue(log.ShouldLog(ASWLog::Level::Info), "On Linux, ShouldLog() should follow the minimum level (Info)");
    CheckFalse(log.ShouldLog(ASWLog::Level::Debug), "On Linux, ShouldLog() should follow the minimum level (Debug)");
    CheckTrue(log.ShouldLog(infoRecord), "On Linux, ShouldLog(record) should follow the minimum level");
#endif
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_SyslogLog)
