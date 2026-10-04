/* **************************************************************************
Test_ASWLog_Base.cpp
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
#include "Test_ASWLog_Base.h"
//---------------------------------------------------------------------------
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_Base.h"
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace
{

// A value whose formatting is counted in CountedValueFormatCount, to check when the *Fmt methods format
struct TCountedValue
{
};

int CountedValueFormatCount = 0;

// A value whose formatting throws, to check that the *Fmt methods don't throw
struct TThrowingValue
{
    bool ThrowStdException = true; // Else throws an int, which isn't a std::exception
};

} // namespace

template<>
struct std::formatter<TCountedValue>
{
    constexpr std::format_parse_context::iterator parse(std::format_parse_context& context)
    {
        return context.begin();
    }

    std::format_context::iterator format(const TCountedValue& /*value*/, std::format_context& context) const
    {
        ++CountedValueFormatCount;
        return std::format_to(context.out(), "counted");
    }
};

template<>
struct std::formatter<TThrowingValue>
{
    constexpr std::format_parse_context::iterator parse(std::format_parse_context& context)
    {
        return context.begin();
    }

    std::format_context::iterator format(const TThrowingValue& value, std::format_context& /*context*/) const
    {
        if (value.ThrowStdException)
            throw std::runtime_error("formatter failed");

        throw 42;
    }
};

namespace ASWUnitTests
{

namespace
{

class TTestLogger final : public ASWLog::TASWLogBase
{
private:
    typedef ASWLog::TASWLogBase inherited;

public:
    ASWLog::Level LastLevel = ASWLog::Level::Info;
    std::string LastMessage;
    std::source_location LastLocation;
    ASWLog::TASWLogRecord LastRecord; // The last record passed to WriteRecord(); its Message refers to LastMessage
    int WriteRecordCount = 0;
    std::chrono::system_clock::time_point FixedNow{}; // What NowUTC() returns, if set
    mutable int NowUTCCount = 0; // How many times NowUTC() was called
    bool ThrowsOnWrite = false; // WriteRecord() throws, like a faulty custom logger

protected:
    std::string_view GetLoggerClassName() const noexcept override
    {
        return "TTestLogger";
    }

    std::chrono::system_clock::time_point NowUTC() const noexcept override
    {
        ++NowUTCCount;
        return FixedNow != std::chrono::system_clock::time_point{} ? FixedNow : inherited::NowUTC();
    }

    void WriteRecord(const ASWLog::TASWLogRecord& record) override
    {
        if (ThrowsOnWrite)
            throw std::runtime_error("write failed");

        ++WriteRecordCount;
        LastLevel = record.LogLevel;
        LastMessage = std::string(record.Message);
        LastLocation = record.Location;
        LastRecord = record;
        LastRecord.Message = LastMessage;
    }

public:
    bool Initialize(const ASWLog::TASWLogConfig& config) noexcept override
    {
        [[maybe_unused]] const auto previousConfig = SetConfig(config);
        SetMinimumLevel(config.InitialMinimumLevel);
        m_IsInitialized.store(true, std::memory_order_release);
        return true;
    }

    bool Reconfigure(const ASWLog::TASWLogConfig& config) noexcept override
    {
        if (!m_IsInitialized.load(std::memory_order_acquire))
            return false;

        [[maybe_unused]] const auto previousConfig = SetConfig(config);
        return true;
    }

    bool Open() noexcept override
    {
        m_IsInitialized.store(true, std::memory_order_release);
        return true;
    }

    bool Close() noexcept override
    {
        m_IsInitialized.store(false, std::memory_order_release);
        return true;
    }

    bool Flush() noexcept override
    {
        return true;
    }

    bool IsOpen() const noexcept override
    {
        return m_IsInitialized.load(std::memory_order_acquire);
    }

    // Reports a failure through TASWLogBase::ReportError(), as a derived logger would
    void ReportTestError(ASWLog::ErrorKind kind, std::string message = "test failure") noexcept
    {
        ASWLog::TASWLogError error;
        error.Kind = kind;
        error.Message = std::move(message);
        ReportError(std::move(error));
    }
};

// While alive, sends what is written to stderr to 'file' instead, by pointing stderr's file descriptor at it. Inactive
// if stderr has no file descriptor (e.g. in a GUI application without a console).
class TStdErrRedirect
{
private:
    int m_SavedDescriptor = -1;

public:
    explicit TStdErrRedirect(const std::filesystem::path& file)
    {
        std::fflush(stderr);
#if defined(_WIN32)
        std::FILE* target = _wfsopen(file.c_str(), L"wb", _SH_DENYNO);
        if (target == nullptr)
            return;

        m_SavedDescriptor = _dup(_fileno(stderr));
        if (m_SavedDescriptor >= 0 && _dup2(_fileno(target), _fileno(stderr)) != 0)
        {
            _close(m_SavedDescriptor);
            m_SavedDescriptor = -1;
        }
#else
        std::FILE* target = std::fopen(file.c_str(), "wb");
        if (target == nullptr)
            return;

        m_SavedDescriptor = dup(fileno(stderr));
        if (m_SavedDescriptor >= 0 && dup2(fileno(target), fileno(stderr)) < 0)
        {
            close(m_SavedDescriptor);
            m_SavedDescriptor = -1;
        }
#endif
        std::fclose(target);
    }

    ~TStdErrRedirect()
    {
        if (m_SavedDescriptor < 0)
            return;

        std::fflush(stderr);
#if defined(_WIN32)
        _dup2(m_SavedDescriptor, _fileno(stderr));
        _close(m_SavedDescriptor);
#else
        dup2(m_SavedDescriptor, fileno(stderr));
        close(m_SavedDescriptor);
#endif
    }

    TStdErrRedirect(const TStdErrRedirect&) = delete;
    TStdErrRedirect& operator=(const TStdErrRedirect&) = delete;

    bool IsActive() const noexcept
    {
        return m_SavedDescriptor >= 0;
    }
};

} // namespace

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_Base
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_Base::TTest_ASWLog_Base()
    : inherited("ASWLog_Base_Tests")
{
    RegisterTest(&TTest_ASWLog_Base::Test_GetConfig_ReturnsUnchangingSnapshot, "GetConfig_ReturnsUnchangingSnapshot");
    RegisterTest(&TTest_ASWLog_Base::Test_GetFullVersionStr_ContainsVersion, "GetFullVersionStr_ContainsVersion");
    RegisterTest(&TTest_ASWLog_Base::Test_Interface_MethodsAreNoexcept, "Interface_MethodsAreNoexcept");
    RegisterTest(&TTest_ASWLog_Base::Test_LogFormatMethods_AcceptEveryArgumentKind, "LogFormatMethods_AcceptEveryArgumentKind");
    RegisterTest(&TTest_ASWLog_Base::Test_LogFormatMethods_LogErrorInsteadOfThrowing, "LogFormatMethods_LogErrorInsteadOfThrowing");
    RegisterTest(&TTest_ASWLog_Base::Test_LogFormatMethods_PassCallerLocation, "LogFormatMethods_PassCallerLocation");
    RegisterTest(&TTest_ASWLog_Base::Test_LogFormatMethods_SkipFormattingWhenNotWritten, "LogFormatMethods_SkipFormattingWhenNotWritten");
    RegisterTest(&TTest_ASWLog_Base::Test_LogLevelConvenienceMethods, "LogLevelConvenienceMethods");
    RegisterTest(&TTest_ASWLog_Base::Test_LogMethods_PassRecordsToWrite, "LogMethods_PassRecordsToWrite");
    RegisterTest(&TTest_ASWLog_Base::Test_ReportError_FailureInHandlerIsNotReportedAgain, "ReportError_FailureInHandlerIsNotReportedAgain");
    RegisterTest(&TTest_ASWLog_Base::Test_ReportError_ThrottlesEachKindSeparately, "ReportError_ThrottlesEachKindSeparately");
    RegisterTest(&TTest_ASWLog_Base::Test_ReportError_ThrowingHandlerDoesNotEscape, "ReportError_ThrowingHandlerDoesNotEscape");
    RegisterTest(&TTest_ASWLog_Base::Test_ReportError_WritesToStdErrWithoutHandler, "ReportError_WritesToStdErrWithoutHandler");
    RegisterTest(&TTest_ASWLog_Base::Test_SetGetMinimumLevel_RoundTrips, "SetGetMinimumLevel_RoundTrips");
    RegisterTest(&TTest_ASWLog_Base::Test_ShouldLog_ReflectsEnabledAndLevel, "ShouldLog_ReflectsEnabledAndLevel");
    RegisterTest(&TTest_ASWLog_Base::Test_Write_AppliesEnabledOffAndLevelChecks, "Write_AppliesEnabledOffAndLevelChecks");
    RegisterTest(&TTest_ASWLog_Base::Test_Write_KeepsFieldsAlreadyStamped, "Write_KeepsFieldsAlreadyStamped");
    RegisterTest(&TTest_ASWLog_Base::Test_Write_StampsOnlyWrittenEntries, "Write_StampsOnlyWrittenEntries");
    RegisterTest(&TTest_ASWLog_Base::Test_Write_ThrowingWriteRecordDropsEntry, "Write_ThrowingWriteRecordDropsEntry");
}
//---------------------------------------------------------------------------
TTest_ASWLog_Base::~TTest_ASWLog_Base()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_GetConfig_ReturnsUnchangingSnapshot()
{
    // Arrange: the snapshot is read-only, so it can't be changed behind the logger's back
    static_assert(std::is_same_v<decltype(std::declval<const ASWLog::IASWLog&>().GetConfig()),
        std::shared_ptr<const ASWLog::TASWLogConfig> >);

    TTestLogger logger;
    const auto defaultConfig = logger.GetConfig();

    ASWLog::TASWLogConfig firstConfig;
    firstConfig.Startup.Banner = "first";
    ASWLog::TASWLogConfig secondConfig;
    secondConfig.Startup.Banner = "second";

    // Act
    logger.Initialize(firstConfig);
    const auto firstSnapshot = logger.GetConfig();
    const bool reconfigured = logger.Reconfigure(secondConfig);
    const auto secondSnapshot = logger.GetConfig();

    // Assert
    CheckTrue(defaultConfig != nullptr && defaultConfig->Startup.Banner.empty(), __func__, __LINE__,
        "Before Initialize(), GetConfig() should return the default config");
    CheckTrue(reconfigured, __func__, __LINE__, "Reconfigure() should succeed once initialized");
    CheckEquals(std::string("first"), firstSnapshot->Startup.Banner, __func__, __LINE__,
        "A snapshot should keep the settings it was taken with after Reconfigure()");
    CheckEquals(std::string("second"), secondSnapshot->Startup.Banner, __func__, __LINE__,
        "GetConfig() should return the settings passed to Reconfigure()");
    CheckEquals(std::string("first"), firstConfig.Startup.Banner, __func__, __LINE__,
        "The config passed to Initialize() should be copied, not kept");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_GetFullVersionStr_ContainsVersion()
{
    // Arrange
    TTestLogger logger;
    const std::string expected = std::string("TTestLogger - Base version ") + std::string(logger.GetVersionStr());

    // Act
    const std::string version = logger.GetFullVersionStr();

    // Assert
    CheckEquals(expected, version, __func__, __LINE__, "Full version string should combine the logger class name and the version number");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_Interface_MethodsAreNoexcept()
{
    // Arrange: through the interface, as a caller holding an IASWLog& sees it. String views rather than literals, since
    // the standard doesn't make converting a literal to std::string_view noexcept.
    TTestLogger testLogger;
    ASWLog::IASWLog& logger = testLogger;
    const ASWLog::TASWLogConfig config;
    const ASWLog::TASWLogRecord record;
    const std::string_view message = "message";
    const std::string runtimeFormat = "{} {}";

    // Assert: checked at compile time, so this test fails to compile if one of them can throw
    static_assert(noexcept(logger.GetConfig()));
    static_assert(noexcept(logger.Initialize(config)));
    static_assert(noexcept(logger.Reconfigure(config)));
    static_assert(noexcept(logger.Open()));
    static_assert(noexcept(logger.Close()));
    static_assert(noexcept(logger.Write(record)));
    static_assert(noexcept(logger.Log(ASWLog::Level::Info, message)));
    static_assert(noexcept(logger.LogRaw(ASWLog::Level::Info, message)));
    static_assert(noexcept(logger.LogForce(ASWLog::Level::Info, message)));
    static_assert(noexcept(logger.LogForceRaw(ASWLog::Level::Info, message)));
    static_assert(noexcept(logger.LogTrace(message)));
    static_assert(noexcept(logger.LogDebug(message)));
    static_assert(noexcept(logger.LogInfo(message)));
    static_assert(noexcept(logger.LogWarn(message)));
    static_assert(noexcept(logger.LogError(message)));
    static_assert(noexcept(logger.LogCritical(message)));
    static_assert(noexcept(logger.LogFmt(ASWLog::Level::Info, "{} {}", 1, message)));
    static_assert(noexcept(logger.LogRawFmt(ASWLog::Level::Info, "{}", 1)));
    static_assert(noexcept(logger.LogForceFmt(ASWLog::Level::Info, "{}", 1)));
    static_assert(noexcept(logger.LogForceRawFmt(ASWLog::Level::Info, "{}", 1)));
    static_assert(noexcept(logger.LogTraceFmt("{}", 1)));
    static_assert(noexcept(logger.LogDebugFmt("{}", 1)));
    static_assert(noexcept(logger.LogInfoFmt("{}", 1)));
    static_assert(noexcept(logger.LogWarnFmt("{}", 1)));
    static_assert(noexcept(logger.LogErrorFmt("{}", 1)));
    static_assert(noexcept(logger.LogCriticalFmt("{}", 1)));
    static_assert(noexcept(logger.LogInfoFmt(ASWLog::RuntimeFormat(runtimeFormat), 1, 2)));

    CheckTrue(logger.ShouldLog(ASWLog::Level::Info), __func__, __LINE__, "The noexcept checks above are made at compile time");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_LogFormatMethods_AcceptEveryArgumentKind()
{
    // Arrange
    // rvalue, lvalue and const lvalue arguments deduce different Args, which the level shortcuts must forward
    // unchanged to LogFmt() for the checked format string's type to match (a mismatch wouldn't compile)
    TTestLogger logger;
    int value = 1;
    const int constValue = 2;
    std::string text = "three";
    const std::string_view view = "four";
    static constexpr std::string_view constantFormat = "{}-{}"; // A compile-time constant, but not a literal
    const std::string runtimeFormat = "{} {} {} {} {}";

    // Act and Assert
    logger.LogInfoFmt("{} {} {} {} {}", value, constValue, text, view, 5);
    CheckEquals(std::string("1 2 three four 5"), logger.LastMessage, __func__, __LINE__, "A level shortcut should format every kind of argument");

    logger.LogFmt(ASWLog::Level::Info, "{} {} {} {} {}", value, constValue, text, view, 5);
    CheckEquals(std::string("1 2 three four 5"), logger.LastMessage, __func__, __LINE__, "LogFmt() should format every kind of argument");

    logger.LogWarnFmt(constantFormat, value, text);
    CheckEquals(std::string("1-three"), logger.LastMessage, __func__, __LINE__, "A constexpr string_view format should be accepted");

    logger.LogInfoFmt(ASWLog::RuntimeFormat(runtimeFormat), value, constValue, text, view, 5);
    CheckEquals(std::string("1 2 three four 5"), logger.LastMessage, __func__, __LINE__, "A RuntimeFormat() string that matches its arguments should format like a literal");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_LogFormatMethods_LogErrorInsteadOfThrowing()
{
    // Arrange
    TTestLogger logger;
    std::string wrongArgumentCountMessage;
    std::string stdExceptionMessage;
    std::string otherExceptionMessage;
    bool threw = false;

    // Act
    try
    {
        logger.LogInfoFmt(ASWLog::RuntimeFormat("bad {} {}"), 1); // A literal would fail to compile
        wrongArgumentCountMessage = logger.LastMessage;

        logger.LogErrorFmt("value {}", TThrowingValue{ true });
        stdExceptionMessage = logger.LastMessage;

        logger.LogForceFmt(ASWLog::Level::Warn, "value {}", TThrowingValue{ false });
        otherExceptionMessage = logger.LastMessage;
    }
    catch (...)
    {
        threw = true;
    }

    // Assert
    CheckFalse(threw, __func__, __LINE__, "A *Fmt call should not throw when formatting fails");
    CheckTrue(wrongArgumentCountMessage.starts_with("[ASWLog format error: "), __func__, __LINE__, "A format string that doesn't match its arguments should log the error: " + wrongArgumentCountMessage);
    CheckTrue(wrongArgumentCountMessage.ends_with("] bad {} {}"), __func__, __LINE__, "The logged error should end with the format string: " + wrongArgumentCountMessage);
    CheckEquals(std::string("[ASWLog format error: formatter failed] value {}"), stdExceptionMessage, __func__, __LINE__, "A std::exception from a formatter should be logged with its message");
    CheckEquals(std::string("[ASWLog format error: unknown exception] value {}"), otherExceptionMessage, __func__, __LINE__, "Any other exception from a formatter should be logged too");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_LogFormatMethods_PassCallerLocation()
{
    // Arrange
    TTestLogger logger;
    logger.SetMinimumLevel(ASWLog::Level::Trace); // So LogTraceFmt()/LogDebugFmt() aren't filtered before formatting
    const std::string testName = __func__;
    const std::string thisFile = std::source_location::current().file_name();
    const std::string runtimeFormat = "runtime {}";

    // Checks the location and message the last *Fmt call passed to the logger
    const auto checkCall = [&](int expectedLine, const std::string& method, const std::string& expectedMessage)
        {
            const auto& location = logger.LastLocation;
            CheckEquals(expectedMessage, logger.LastMessage, testName, __LINE__, method + " should format the message");
            CheckEquals(thisFile, std::string(location.file_name()), testName, __LINE__, method + " should pass the caller's file, not ASWLog_Interface.h");
            CheckEquals(static_cast<int64_t>(expectedLine), static_cast<int64_t>(location.line()), testName, __LINE__, method + " should pass the caller's line");
            CheckTrue(std::string_view(location.function_name()).find(testName) != std::string_view::npos, testName, __LINE__, method + " should pass the caller's function");
        };

    // Act and Assert: each expected line is the line after the one that records it
    int line = __LINE__ + 1;
    logger.LogFmt(ASWLog::Level::Info, "fmt {}", 1);
    checkCall(line, "LogFmt", "fmt 1");

    line = __LINE__ + 1;
    logger.LogRawFmt(ASWLog::Level::Info, "raw {}", 2);
    checkCall(line, "LogRawFmt", "raw 2");

    line = __LINE__ + 1;
    logger.LogForceFmt(ASWLog::Level::Info, "force {}", 3);
    checkCall(line, "LogForceFmt", "force 3");

    line = __LINE__ + 1;
    logger.LogForceRawFmt(ASWLog::Level::Info, "force raw {}", 4);
    checkCall(line, "LogForceRawFmt", "force raw 4");

    line = __LINE__ + 1;
    logger.LogTraceFmt("trace {}", 5);
    checkCall(line, "LogTraceFmt", "trace 5");

    line = __LINE__ + 1;
    logger.LogDebugFmt("debug {}", 6);
    checkCall(line, "LogDebugFmt", "debug 6");

    line = __LINE__ + 1;
    logger.LogInfoFmt("info {}", 7);
    checkCall(line, "LogInfoFmt", "info 7");

    line = __LINE__ + 1;
    logger.LogWarnFmt("warn {}", 8);
    checkCall(line, "LogWarnFmt", "warn 8");

    line = __LINE__ + 1;
    logger.LogErrorFmt("error {}", 9);
    checkCall(line, "LogErrorFmt", "error 9");

    line = __LINE__ + 1;
    logger.LogCriticalFmt("critical {}", 10);
    checkCall(line, "LogCriticalFmt", "critical 10");

    line = __LINE__ + 1;
    logger.LogInfoFmt(ASWLog::RuntimeFormat(runtimeFormat), 11);
    checkCall(line, "LogInfoFmt with RuntimeFormat()", "runtime 11");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_LogFormatMethods_SkipFormattingWhenNotWritten()
{
    // Arrange
    TTestLogger logger; // Minimum level Info
    CountedValueFormatCount = 0;

    // Act and Assert: each step checks how many times the value has been formatted so far
    logger.LogDebugFmt("{}", TCountedValue{});
    logger.LogFmt(ASWLog::Level::Trace, "{}", TCountedValue{});
    logger.LogRawFmt(ASWLog::Level::Debug, "{}", TCountedValue{});
    logger.LogFmt(ASWLog::Level::Off, "{}", TCountedValue{});
    logger.LogForceFmt(ASWLog::Level::Off, "{}", TCountedValue{});
    CheckEquals(0, CountedValueFormatCount, __func__, __LINE__, "An entry below the minimum level, or at Off, should not be formatted");

    logger.LogInfoFmt("{}", TCountedValue{});
    CheckEquals(1, CountedValueFormatCount, __func__, __LINE__, "An entry at the minimum level should be formatted once");
    CheckEquals(std::string("counted"), logger.LastMessage, __func__, __LINE__, "The formatted entry should reach the logger");

    logger.LogForceFmt(ASWLog::Level::Trace, "{}", TCountedValue{});
    logger.LogForceRawFmt(ASWLog::Level::Trace, "{}", TCountedValue{});
    CheckEquals(3, CountedValueFormatCount, __func__, __LINE__, "A forced entry below the minimum level should still be formatted");

    logger.SetEnabled(false);
    logger.LastMessage.clear();
    logger.LogErrorFmt("{}", TCountedValue{});
    logger.LogForceFmt(ASWLog::Level::Critical, "{}", TCountedValue{});
    logger.LogForceRawFmt(ASWLog::Level::Critical, "{}", TCountedValue{});
    CheckEquals(3, CountedValueFormatCount, __func__, __LINE__, "Nothing should be formatted while the logger is disabled, not even forced entries");
    CheckTrue(logger.LastMessage.empty(), __func__, __LINE__, "Nothing should reach the logger while it is disabled");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_LogLevelConvenienceMethods()
{
    // Arrange
    TTestLogger logger;
    logger.SetMinimumLevel(ASWLog::Level::Trace); // So TASWLogBase::Write() passes LogTrace()/LogDebug() on

    // Act
    logger.LogTrace("trace");

    // Assert
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Trace), static_cast<int32_t>(logger.LastLevel), __func__, __LINE__, "LogTrace should set Trace level");
    CheckEquals(std::string("trace"), logger.LastMessage, __func__, __LINE__, "LogTrace should store the message");

    // Act
    logger.LogDebug("debug");

    // Assert
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Debug), static_cast<int32_t>(logger.LastLevel), __func__, __LINE__, "LogDebug should set Debug level");
    CheckEquals(std::string("debug"), logger.LastMessage, __func__, __LINE__, "LogDebug should store the message");

    // Act
    logger.LogInfo("info");

    // Assert
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Info), static_cast<int32_t>(logger.LastLevel), __func__, __LINE__, "LogInfo should set Info level");
    CheckEquals(std::string("info"), logger.LastMessage, __func__, __LINE__, "LogInfo should store the message");

    // Act
    logger.LogWarn("warn");

    // Assert
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Warn), static_cast<int32_t>(logger.LastLevel), __func__, __LINE__, "LogWarn should set Warn level");
    CheckEquals(std::string("warn"), logger.LastMessage, __func__, __LINE__, "LogWarn should store the message");

    // Act
    logger.LogError("error");

    // Assert
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Error), static_cast<int32_t>(logger.LastLevel), __func__, __LINE__, "LogError should set Error level");
    CheckEquals(std::string("error"), logger.LastMessage, __func__, __LINE__, "LogError should store the message");

    // Act
    logger.LogCritical("critical");

    // Assert
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Critical), static_cast<int32_t>(logger.LastLevel), __func__, __LINE__, "LogCritical should set Critical level");
    CheckEquals(std::string("critical"), logger.LastMessage, __func__, __LINE__, "LogCritical should store the message");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_LogMethods_PassRecordsToWrite()
{
    // Arrange: a logger that only implements WriteRecord(), as a custom logger would
    TTestLogger logger;
    logger.SetMinimumLevel(ASWLog::Level::Trace);
    const std::string testName = __func__;

    // Checks the record the last logging call passed on
    const auto checkRecord = [&](int expectedLine, const std::string& method, ASWLog::Level expectedLevel,
                                 const std::string& expectedMessage, bool expectedRaw, bool expectedForced)
        {
            const auto& record = logger.LastRecord;
            CheckEquals(static_cast<int32_t>(expectedLevel), static_cast<int32_t>(record.LogLevel), testName, __LINE__, method + " should pass its level");
            CheckEquals(expectedMessage, std::string(record.Message), testName, __LINE__, method + " should pass its message");
            CheckEquals(static_cast<int64_t>(expectedLine), static_cast<int64_t>(record.Location.line()), testName, __LINE__, method + " should pass the caller's line");
            CheckTrue(expectedRaw == record.Raw, testName, __LINE__, method + " should set Raw correctly");
            CheckTrue(expectedForced == record.Forced, testName, __LINE__, method + " should set Forced correctly");
        };

    // Act and Assert: each expected line is the line after the one that records it
    int line = __LINE__ + 1;
    logger.Log(ASWLog::Level::Info, "log");
    checkRecord(line, "Log", ASWLog::Level::Info, "log", false, false);

    line = __LINE__ + 1;
    logger.LogRaw(ASWLog::Level::Warn, "raw");
    checkRecord(line, "LogRaw", ASWLog::Level::Warn, "raw", true, false);

    line = __LINE__ + 1;
    logger.LogForce(ASWLog::Level::Debug, "force");
    checkRecord(line, "LogForce", ASWLog::Level::Debug, "force", false, true);

    line = __LINE__ + 1;
    logger.LogForceRaw(ASWLog::Level::Error, "force raw");
    checkRecord(line, "LogForceRaw", ASWLog::Level::Error, "force raw", true, true);

    line = __LINE__ + 1;
    logger.LogTrace("trace");
    checkRecord(line, "LogTrace", ASWLog::Level::Trace, "trace", false, false);

    line = __LINE__ + 1;
    logger.LogCritical("critical");
    checkRecord(line, "LogCritical", ASWLog::Level::Critical, "critical", false, false);

    line = __LINE__ + 1;
    logger.LogRawFmt(ASWLog::Level::Info, "raw {}", 1);
    checkRecord(line, "LogRawFmt", ASWLog::Level::Info, "raw 1", true, false);

    line = __LINE__ + 1;
    logger.LogForceFmt(ASWLog::Level::Info, "force {}", 2);
    checkRecord(line, "LogForceFmt", ASWLog::Level::Info, "force 2", false, true);

    line = __LINE__ + 1;
    logger.LogForceRawFmt(ASWLog::Level::Info, "force raw {}", 3);
    checkRecord(line, "LogForceRawFmt", ASWLog::Level::Info, "force raw 3", true, true);

    CheckEquals(9, logger.WriteRecordCount, testName, __LINE__, "Each call should pass exactly one record to WriteRecord()");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_ReportError_FailureInHandlerIsNotReportedAgain()
{
    // Arrange: a handler that fails the same way again, like one that logs to the logger whose output failed. Without
    // the guard, each report would start another, until the stack overflowed.
    TTestLogger logger;
    std::vector<ASWLog::ErrorKind> reportedKinds;
    ASWLog::TASWLogConfig config;
    config.ErrorReportInterval = std::chrono::milliseconds(0);
    config.OnError = [&logger, &reportedKinds](const ASWLog::TASWLogError& error) {
            reportedKinds.push_back(error.Kind);
            if (reportedKinds.size() < 10)
                logger.ReportTestError(ASWLog::ErrorKind::WriteFailed, "failure inside the handler");
        };
    logger.Initialize(config);

    // Act
    logger.ReportTestError(ASWLog::ErrorKind::OpenFailed);
    logger.ReportTestError(ASWLog::ErrorKind::FlushFailed);

    // Assert
    CheckTrue(reportedKinds == std::vector<ASWLog::ErrorKind>{ ASWLog::ErrorKind::OpenFailed, ASWLog::ErrorKind::FlushFailed }, __func__, __LINE__,
        "Only the failures outside the handler should be reported, and the next failure after it should be reported again");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_ReportError_ThrottlesEachKindSeparately()
{
    // Arrange
    using namespace std::chrono_literals;
    const auto startTime = std::chrono::sys_days{ 2026y / 1 / 15 } + 10h;
    TTestLogger logger;
    logger.FixedNow = startTime;

    std::vector<ASWLog::TASWLogError> reports;
    ASWLog::TASWLogConfig config;
    config.OnError = [&reports](const ASWLog::TASWLogError& error) {
            reports.push_back(error);
        };
    logger.Initialize(config); // Default ErrorReportInterval: 1 minute

    // Act
    logger.ReportTestError(ASWLog::ErrorKind::OpenFailed, "first");
    logger.ReportTestError(ASWLog::ErrorKind::OpenFailed, "suppressed 1");
    logger.ReportTestError(ASWLog::ErrorKind::WriteFailed, "other kind"); // Another kind is reported at once
    logger.ReportTestError(ASWLog::ErrorKind::OpenFailed, "suppressed 2");
    logger.FixedNow = startTime + 59s;
    logger.ReportTestError(ASWLog::ErrorKind::OpenFailed, "suppressed 3");
    logger.FixedNow = startTime + 60s;
    logger.ReportTestError(ASWLog::ErrorKind::OpenFailed, "after the interval");
    const auto throttledCount = reports.size();

    config.ErrorReportInterval = 0ms;
    logger.Reconfigure(config);
    logger.ReportTestError(ASWLog::ErrorKind::OpenFailed, "unthrottled 1");
    logger.ReportTestError(ASWLog::ErrorKind::OpenFailed, "unthrottled 2");

    // Assert
    CheckEquals(static_cast<std::size_t>(3), throttledCount, __func__, __LINE__, "Each kind should be reported at most once per ErrorReportInterval");
    CheckEquals(static_cast<std::size_t>(5), reports.size(), __func__, __LINE__, "An ErrorReportInterval of 0 should report every failure");
    if (reports.size() != 5)
        return;

    CheckEquals(std::string("first"), reports[0].Message, __func__, __LINE__, "The first failure should be reported");
    CheckEquals(static_cast<std::size_t>(0), reports[0].SuppressedCount, __func__, __LINE__, "Nothing was left out before the first report");
    CheckEquals(std::string("other kind"), reports[1].Message, __func__, __LINE__, "Another kind should have its own interval");
    CheckEquals(std::string("after the interval"), reports[2].Message, __func__, __LINE__, "The kind should be reported again once the interval has passed");
    CheckEquals(static_cast<std::size_t>(3), reports[2].SuppressedCount, __func__, __LINE__, "The report should count the failures left out since the previous one");
    CheckEquals(std::string("unthrottled 2"), reports[4].Message, __func__, __LINE__, "With an interval of 0, every failure should be reported");
    CheckEquals(static_cast<std::size_t>(0), reports[4].SuppressedCount, __func__, __LINE__, "With an interval of 0, nothing should be left out");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_ReportError_ThrowingHandlerDoesNotEscape()
{
    // Arrange
    TTestLogger logger;
    int handlerCalls = 0;
    ASWLog::TASWLogConfig config;
    config.ErrorReportInterval = std::chrono::milliseconds(0);
    config.OnError = [&handlerCalls](const ASWLog::TASWLogError& /*error*/) {
            ++handlerCalls;
            throw std::runtime_error("handler failed");
        };
    logger.Initialize(config);
    logger.ThrowsOnWrite = true;
    bool threw = false;

    // Act: ReportError() is noexcept, so an escaping exception would end the test run with std::terminate
    try
    {
        logger.ReportTestError(ASWLog::ErrorKind::OpenFailed);
        logger.LogError("dropped"); // Reported from TASWLogBase::Write()
    }
    catch (...)
    {
        threw = true;
    }

    // Assert
    CheckFalse(threw, __func__, __LINE__, "An exception from OnError should not escape");
    CheckEquals(2, handlerCalls, __func__, __LINE__, "A handler that threw should still be called for the next failure");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_ReportError_WritesToStdErrWithoutHandler()
{
    // Arrange
    const auto stdErrFile = std::filesystem::temp_directory_path() / "aswlog_base_stderr.txt";
    TTestLogger logger; // Default config: no OnError

    // Act
    bool isRedirected = false;
    {
        TStdErrRedirect redirect(stdErrFile);
        isRedirected = redirect.IsActive();
        if (isRedirected)
        {
            logger.ReportTestError(ASWLog::ErrorKind::OpenFailed, "Couldn't open the log file");
            logger.ReportTestError(ASWLog::ErrorKind::OpenFailed, "throttled"); // Within the default interval
        }
    }

    if (!isRedirected)
    {
        std::error_code removeError;
        std::filesystem::remove(stdErrFile, removeError);
        Skip(__func__, __LINE__, "stderr has no file descriptor to redirect (e.g. a GUI application)");
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
    CheckEquals(std::string("ASWLog TTestLogger: OPEN_FAILED: Couldn't open the log file\n"), written, __func__, __LINE__,
        "Without OnError, a failure should be written to stderr as one line naming the logger, once per interval");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_SetGetMinimumLevel_RoundTrips()
{
    // Arrange
    TTestLogger logger;
    ASWLog::TASWLogConfig config;
    config.InitialMinimumLevel = ASWLog::Level::Warn;

    // Act
    logger.Initialize(config);

    // Assert
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Warn), static_cast<int32_t>(logger.GetMinimumLevel()), __func__, __LINE__, "GetMinimumLevel should be seeded from the config's InitialMinimumLevel at Initialize() time");

    // Act
    logger.SetMinimumLevel(ASWLog::Level::Trace);

    // Assert
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Trace), static_cast<int32_t>(logger.GetMinimumLevel()), __func__, __LINE__, "SetMinimumLevel should update the value returned by GetMinimumLevel immediately");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_ShouldLog_ReflectsEnabledAndLevel()
{
    // Arrange: everything through the interface, as code holding only an IASWLog& would use it
    TTestLogger testLogger;
    ASWLog::IASWLog& logger = testLogger;

    // Act
    logger.SetMinimumLevel(ASWLog::Level::Warn);
    const bool enabledByDefault = logger.IsEnabled();
    const bool shouldLogInfo = logger.ShouldLog(ASWLog::Level::Info);
    const bool shouldLogWarn = logger.ShouldLog(ASWLog::Level::Warn);
    const bool shouldLogOff = logger.ShouldLog(ASWLog::Level::Off);

    logger.SetEnabled(false);
    const bool enabledAfterDisable = logger.IsEnabled();
    const bool shouldLogCriticalWhileDisabled = logger.ShouldLog(ASWLog::Level::Critical);

    logger.SetEnabled(true);
    logger.SetMinimumLevel(ASWLog::Level::Off);
    const bool shouldLogCriticalAtMinimumOff = logger.ShouldLog(ASWLog::Level::Critical);

    // Assert
    CheckTrue(enabledByDefault, __func__, __LINE__, "A logger should be enabled by default");
    CheckTrue(logger.GetMinimumLevel() == ASWLog::Level::Off, __func__, __LINE__, "The minimum level should be readable through IASWLog");
    CheckFalse(shouldLogInfo, __func__, __LINE__, "An entry below the minimum level should not be logged");
    CheckTrue(shouldLogWarn, __func__, __LINE__, "An entry at the minimum level should be logged");
    CheckFalse(shouldLogOff, __func__, __LINE__, "An entry at Off should never be logged");
    CheckFalse(enabledAfterDisable, __func__, __LINE__, "SetEnabled(false) should disable the logger");
    CheckFalse(shouldLogCriticalWhileDisabled, __func__, __LINE__, "Nothing should be logged while the logger is disabled");
    CheckFalse(shouldLogCriticalAtMinimumOff, __func__, __LINE__, "Nothing should be logged at a minimum level of Off");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_Write_AppliesEnabledOffAndLevelChecks()
{
    // Arrange
    TTestLogger logger;
    logger.SetMinimumLevel(ASWLog::Level::Warn);

    // Act and Assert: WriteRecordCount shows whether each entry reached WriteRecord()
    logger.LogInfo("below_minimum");
    CheckEquals(0, logger.WriteRecordCount, __func__, __LINE__, "An entry below the minimum level should not reach WriteRecord()");

    logger.LogWarn("at_minimum");
    CheckEquals(1, logger.WriteRecordCount, __func__, __LINE__, "An entry at the minimum level should reach WriteRecord()");

    logger.LogForce(ASWLog::Level::Info, "forced");
    CheckEquals(2, logger.WriteRecordCount, __func__, __LINE__, "A forced entry should ignore the minimum level");

    logger.LogForce(ASWLog::Level::Off, "forced_off");
    CheckEquals(2, logger.WriteRecordCount, __func__, __LINE__, "An entry at Off should never be written, even forced");

    logger.SetEnabled(false);
    logger.LogForce(ASWLog::Level::Critical, "forced_while_disabled");
    CheckEquals(2, logger.WriteRecordCount, __func__, __LINE__, "A disabled logger should write nothing, even forced");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_Write_KeepsFieldsAlreadyStamped()
{
    // Arrange: a record stamped elsewhere, e.g. by a multi-log before passing it on
    TTestLogger logger;
    logger.FixedNow = std::chrono::system_clock::time_point(std::chrono::hours(1000));

    ASWLog::TASWLogRecord record;
    record.LogLevel = ASWLog::Level::Error;
    record.Message = "already_stamped";
    record.Timestamp = std::chrono::system_clock::time_point(std::chrono::hours(2000));
    record.ProcessId = 7;
    record.ThreadId = 9;

    // Act
    logger.Write(record);

    // Assert
    CheckEquals(1, logger.WriteRecordCount, __func__, __LINE__, "The record should be written");
    CheckTrue(logger.LastRecord.Timestamp == record.Timestamp, __func__, __LINE__, "A Timestamp already set should be kept");
    CheckEquals(static_cast<int64_t>(7), static_cast<int64_t>(logger.LastRecord.ProcessId), __func__, __LINE__, "A ProcessId already set should be kept");
    CheckEquals(static_cast<int64_t>(9), static_cast<int64_t>(logger.LastRecord.ThreadId), __func__, __LINE__, "A ThreadId already set should be kept");
    CheckEquals(0, logger.NowUTCCount, __func__, __LINE__, "The clock should not be read for a record that already has its Timestamp");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_Write_StampsOnlyWrittenEntries()
{
    // Arrange
    TTestLogger logger;
    logger.FixedNow = std::chrono::system_clock::time_point(std::chrono::hours(1000));
    logger.SetMinimumLevel(ASWLog::Level::Warn);

    // Act
    logger.LogInfo("filtered");
    const int clockReadsWhenFiltered = logger.NowUTCCount;

    logger.LogError("written");

    // Assert
    CheckEquals(0, clockReadsWhenFiltered, __func__, __LINE__, "A filtered entry should not be stamped (no clock read)");
    CheckEquals(1, logger.NowUTCCount, __func__, __LINE__, "A written entry should be stamped once");
    CheckTrue(logger.LastRecord.Timestamp == logger.FixedNow, __func__, __LINE__, "The Timestamp should come from the logger's NowUTC()");
    CheckEquals(static_cast<int64_t>(ASWLog::GetCurrentOSProcessId()), static_cast<int64_t>(logger.LastRecord.ProcessId), __func__, __LINE__, "The ProcessId should be this process's");
    CheckEquals(static_cast<int64_t>(ASWLog::GetCurrentOSThreadId()), static_cast<int64_t>(logger.LastRecord.ThreadId), __func__, __LINE__, "The ThreadId should be the calling thread's");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Base::Test_Write_ThrowingWriteRecordDropsEntry()
{
    // Arrange: a custom logger whose WriteRecord() throws. Without the catch in TASWLogBase::Write(), the exception
    // would leave a noexcept function and call std::terminate, ending the test run.
    TTestLogger logger;
    std::vector<ASWLog::TASWLogError> reports;
    ASWLog::TASWLogConfig config;
    config.ErrorReportInterval = std::chrono::milliseconds(0);
    config.OnError = [&reports](const ASWLog::TASWLogError& error) {
            reports.push_back(error);
        };
    logger.Initialize(config);
    logger.ThrowsOnWrite = true;

    // Act
    logger.LogError("dropped");
    logger.LogForceRaw(ASWLog::Level::Critical, "dropped_too");
    logger.ThrowsOnWrite = false;
    logger.LogError("written");

    // Assert
    CheckEquals(1, logger.WriteRecordCount, __func__, __LINE__, "Only the entry written after the failures should be recorded");
    CheckEquals(std::string("written"), logger.LastMessage, __func__, __LINE__, "The logger should keep working after WriteRecord() threw");
    CheckEquals(static_cast<std::size_t>(2), reports.size(), __func__, __LINE__, "Each dropped entry should be reported");
    if (reports.empty())
        return;

    CheckTrue(reports[0].Kind == ASWLog::ErrorKind::Exception, __func__, __LINE__, "A dropped entry should be reported as an Exception");
    CheckEquals(std::string("Dropped an entry: write failed"), reports[0].Message, __func__, __LINE__, "The report should give the exception's what()");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_Base)
