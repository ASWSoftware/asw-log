/* **************************************************************************
Test_ASWLog_ConsoleLog.cpp
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
#include "Test_ASWLog_ConsoleLog.h"
//---------------------------------------------------------------------------
#include <chrono>
#include <cstdint>
#include <iostream>
#include <streambuf>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_ConsoleLog.h"
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------
#include "UT_Helper_Environment.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

// A TASWConsoleLog whose streams report the color support a test chooses, through the DetectStreamColorSupport()
// hook, since under a test runner the real stdout and stderr aren't terminals.
class TColorDetectConsoleLog : public ASWLog::TASWConsoleLog
{
public:
    bool StdOutSupportsColor = false;
    bool StdErrSupportsColor = false;

protected:
    bool DetectStreamColorSupport(bool isStdErr) override
    {
        return isStdErr ? StdErrSupportsColor : StdOutSupportsColor;
    }
};

// RAII helper: redirects a standard stream's buffer to one that fails every write, like a console that's gone,
// restoring the original buffer (which also clears the stream's failed state) on destruction.
class TFailingOutput
{
private:
    class TFailingBuffer final : public std::streambuf
    {
    protected:
        int_type overflow(int_type /*character*/) override
        {
            return traits_type::eof();
        }

        std::streamsize xsputn(const char* /*data*/, std::streamsize /*size*/) override
        {
            return 0;
        }
    };

private:
    std::ostream& m_Stream;
    TFailingBuffer m_Buffer;
    std::streambuf* m_OriginalBuffer;

public:
    explicit TFailingOutput(std::ostream& stream)
        : m_Stream(stream),
          m_Buffer(),
          m_OriginalBuffer(stream.rdbuf(&m_Buffer))
    {
    }

    ~TFailingOutput()
    {
        m_Stream.rdbuf(m_OriginalBuffer);
    }

    TFailingOutput(const TFailingOutput&) = delete;
    TFailingOutput& operator=(const TFailingOutput&) = delete;
};

// RAII helper: redirects a standard stream's buffer to one that counts how often the stream is flushed, restoring the
// original buffer on destruction.
class TFlushCounter
{
private:
    class TCountingBuffer final : public std::stringbuf
    {
    public:
        int SyncCount = 0;

    protected:
        int sync() override
        {
            ++SyncCount;
            return std::stringbuf::sync();
        }
    };

private:
    std::ostream& m_Stream;
    TCountingBuffer m_Buffer;
    std::streambuf* m_OriginalBuffer;

public:
    explicit TFlushCounter(std::ostream& stream)
        : m_Stream(stream),
          m_Buffer(),
          m_OriginalBuffer(stream.rdbuf(&m_Buffer))
    {
    }

    ~TFlushCounter()
    {
        m_Stream.rdbuf(m_OriginalBuffer);
    }

    TFlushCounter(const TFlushCounter&) = delete;
    TFlushCounter& operator=(const TFlushCounter&) = delete;

    int GetCount() const
    {
        return m_Buffer.SyncCount;
    }
};

// RAII helper: redirects a standard stream's buffer to an internal buffer for
// the lifetime of the object, restoring the original buffer on destruction.
class TStreamCapture
{
private:
    std::ostream& m_Stream;
    std::ostringstream m_Buffer;
    std::streambuf* m_OriginalBuffer;

public:
    explicit TStreamCapture(std::ostream& stream)
        : m_Stream(stream),
          m_Buffer(),
          m_OriginalBuffer(stream.rdbuf(m_Buffer.rdbuf()))
    {
    }

    ~TStreamCapture()
    {
        m_Stream.rdbuf(m_OriginalBuffer);
    }

    std::string Str() const
    {
        return m_Buffer.str();
    }
};

// Returns a config with every optional banner/metadata field disabled, so
// tests can enable only the specific fields they exercise.
ASWLog::TASWLogConfig MakeQuietConfig()
{
    ASWLog::TASWLogConfig config;
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.Shutdown.WriteLine = false;
    config.Startup.WriteTimeInfo = false;
    config.Startup.WriteOSInfo = false;
    config.Startup.WriteDriveInfo = false;
    config.Startup.WriteSystemMemoryInfo = false;
    config.Startup.WriteApplicationInfo = false;
    config.Startup.WriteMemoryUsage = false;
    return config;
}

} // namespace

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_ConsoleLog
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_ConsoleLog::TTest_ASWLog_ConsoleLog()
    : inherited("ASWLog_ConsoleLog_Tests")
{
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_ColorModeAlways_WrapsOutputWithAnsiCodes, "ColorModeAlways_WrapsOutputWithAnsiCodes");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_ColorModeAuto_ColorsOnlyStreamsThatSupportIt, "ColorModeAuto_ColorsOnlyStreamsThatSupportIt");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_ColorModeAuto_HonorsNoColor, "ColorModeAuto_HonorsNoColor");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_ColorModeNever_SuppressesAnsiCodes, "ColorModeNever_SuppressesAnsiCodes");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_Flush_FlushesStdOutAndStdErrWhileOpen, "Flush_FlushesStdOutAndStdErrWhileOpen");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_GetColorMode_ReflectsSetColorMode, "GetColorMode_ReflectsSetColorMode");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_GetInstance_ReturnsSameInstance, "GetInstance_ReturnsSameInstance");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_GetLevelColor_OffHasNoColor, "GetLevelColor_OffHasNoColor");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_Initialize_SuppressesInfoBannersBelowMinimumLevel, "Initialize_SuppressesInfoBannersBelowMinimumLevel");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_Initialize_WritesDriveInfoWhenEnabled, "Initialize_WritesDriveInfoWhenEnabled");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_IsColorSupported_ReflectsDetectedStreams, "IsColorSupported_ReflectsDetectedStreams");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_LogLineMetadata_Options, "LogLineMetadata_Options");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_LogProcessAndThreadIds_AreOSIds, "LogProcessAndThreadIds_AreOSIds");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_LogRawAndForceOptions, "LogRawAndForceOptions");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_LogRespectsMinimumLevel, "LogRespectsMinimumLevel");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_OnError_ReportsOnlyTheWriteThatFailedTheStream, "OnError_ReportsOnlyTheWriteThatFailedTheStream");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_OnLogEntry_FiresForQualifyingLevelsOnly, "OnLogEntry_FiresForQualifyingLevelsOnly");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_ResetLevelColor_RestoresDefault, "ResetLevelColor_RestoresDefault");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_ResetLevelColors_RestoresAllDefaults, "ResetLevelColors_RestoresAllDefaults");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_SetLevelColor_EmptyStringDisablesColorForLevel, "SetLevelColor_EmptyStringDisablesColorForLevel");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_SetLevelColor_OverridesDefaultColor, "SetLevelColor_OverridesDefaultColor");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_WarnAndAboveWriteToStdErr, "WarnAndAboveWriteToStdErr");
}
//---------------------------------------------------------------------------
TTest_ASWLog_ConsoleLog::~TTest_ASWLog_ConsoleLog()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_ColorModeAlways_WrapsOutputWithAnsiCodes()
{
    // Arrange: the real stdout isn't a terminal under a test runner, so only Always colors it
    auto config = MakeQuietConfig();

    ASWLog::TASWConsoleLog logger;
    logger.SetColorMode(ASWLog::ColorMode::Always);

    // Act
    TStreamCapture outCapture(std::cout);
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("colored_message");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckContains(outCapture.Str(), "\x1b[", "ColorMode::Always should wrap output with ANSI escape codes");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_ColorModeAuto_ColorsOnlyStreamsThatSupportIt()
{
    // Arrange: stdout shows colors and stderr doesn't (e.g. stderr redirected to a file)
    TScopedEnvironmentVariable noColor("NO_COLOR", std::nullopt);
    auto config = MakeQuietConfig();

    TColorDetectConsoleLog logger;
    logger.StdOutSupportsColor = true;
    logger.StdErrSupportsColor = false;

    // Act
    TStreamCapture outCapture(std::cout);
    TStreamCapture errCapture(std::cerr);
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("stdout_message");
    logger.LogError("stderr_message");
    logger.Close();

    const auto outContents = outCapture.Str();
    const auto errContents = errCapture.Str();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(ASWLog::ColorMode::Auto, logger.GetColorMode(), "ColorMode should default to Auto");
    CheckContains(outContents, "\x1b[", "Auto should color a stream that shows colors");
    CheckNotContains(errContents, "\x1b[", "Auto should not color a stream that doesn't show colors, such as a file or a pipe");
    CheckContains(errContents, "stderr_message", "The uncolored message should still be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_ColorModeAuto_HonorsNoColor()
{
    // Arrange: both streams show colors, but NO_COLOR asks for none
    TScopedEnvironmentVariable noColor("NO_COLOR", "1");
    auto config = MakeQuietConfig();

    TColorDetectConsoleLog autoLogger;
    autoLogger.StdOutSupportsColor = true;
    autoLogger.StdErrSupportsColor = true;

    TColorDetectConsoleLog alwaysLogger;
    alwaysLogger.StdOutSupportsColor = true;
    alwaysLogger.StdErrSupportsColor = true;
    alwaysLogger.SetColorMode(ASWLog::ColorMode::Always);

    // Act
    std::string autoContents;
    std::string alwaysContents;
    bool autoInitialized = false;
    bool alwaysInitialized = false;
    {
        TStreamCapture outCapture(std::cout);
        autoInitialized = autoLogger.Initialize(config);
        autoLogger.LogInfo("auto_message");
        autoLogger.Close();
        autoContents = outCapture.Str();
    }
    {
        TStreamCapture outCapture(std::cout);
        alwaysInitialized = alwaysLogger.Initialize(config);
        alwaysLogger.LogInfo("always_message");
        alwaysLogger.Close();
        alwaysContents = outCapture.Str();
    }

    // Act: NO_COLOR set to an empty value doesn't count (see https://no-color.org)
    std::string emptyContents;
    bool emptyInitialized = false;
    {
        TScopedEnvironmentVariable emptyNoColor("NO_COLOR", "");
        TColorDetectConsoleLog emptyLogger;
        emptyLogger.StdOutSupportsColor = true;
        emptyLogger.StdErrSupportsColor = true;

        TStreamCapture outCapture(std::cout);
        emptyInitialized = emptyLogger.Initialize(config);
        emptyLogger.LogInfo("empty_message");
        emptyLogger.Close();
        emptyContents = outCapture.Str();
    }

    // Assert
    CheckTrue(autoInitialized && alwaysInitialized && emptyInitialized, "Initialize should succeed");
    CheckNotContains(autoContents, "\x1b[", "Auto should not color any stream when NO_COLOR is set");
    CheckContains(autoContents, "auto_message", "The uncolored message should still be written");
    CheckContains(alwaysContents, "\x1b[", "Always should color even when NO_COLOR is set");
    CheckContains(emptyContents, "\x1b[", "Auto should still color when NO_COLOR is set but empty");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_ColorModeNever_SuppressesAnsiCodes()
{
    // Arrange: a stream that shows colors, so only Never can suppress them
    auto config = MakeQuietConfig();

    TColorDetectConsoleLog logger;
    logger.StdOutSupportsColor = true;
    logger.SetColorMode(ASWLog::ColorMode::Never);

    // Act
    TStreamCapture outCapture(std::cout);
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("plain_message");
    logger.Close();

    const auto contents = outCapture.Str();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckNotContains(contents, "\x1b[", "ColorMode::Never should suppress all ANSI escape codes");
    CheckContains(contents, "plain_message", "The message should still be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_Flush_FlushesStdOutAndStdErrWhileOpen()
{
    // Arrange
    ASWLog::TASWConsoleLog consoleLog;
    ASWLog::IASWLog& logger = consoleLog; // As generic code would flush it

    bool initialized = false;
    bool flushed = false;
    bool flushedWhileClosed = true;
    int stdOutFlushes = 0;
    int stdErrFlushes = 0;

    // Act: count only the flushes made by Flush()
    {
        TFlushCounter stdOutCounter(std::cout);
        TFlushCounter stdErrCounter(std::cerr);
        initialized = logger.Initialize(MakeQuietConfig());

        const int stdOutBefore = stdOutCounter.GetCount();
        const int stdErrBefore = stdErrCounter.GetCount();
        flushed = logger.Flush();
        stdOutFlushes = stdOutCounter.GetCount() - stdOutBefore;
        stdErrFlushes = stdErrCounter.GetCount() - stdErrBefore;

        logger.Close();
        flushedWhileClosed = logger.Flush();
    }

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(flushed, "Flush() should succeed while the logger is open");
    // At least once: std::cerr is tied to std::cout, and some standard libraries flush the tie when std::cerr is flushed
    CheckGreaterThanOrEqual(stdOutFlushes, 1, "Flush() should flush stdout");
    CheckGreaterThanOrEqual(stdErrFlushes, 1, "Flush() should flush stderr");
    CheckFalse(flushedWhileClosed, "Flush() should return false while the logger is closed");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_GetColorMode_ReflectsSetColorMode()
{
    // Arrange
    ASWLog::TASWConsoleLog logger;

    // Assert
    CheckEquals(ASWLog::ColorMode::Auto, logger.GetColorMode(), "ColorMode should default to Auto");

    // Act & Assert
    logger.SetColorMode(ASWLog::ColorMode::Always);
    CheckEquals(ASWLog::ColorMode::Always, logger.GetColorMode(), "SetColorMode(Always) should be reflected by GetColorMode()");

    logger.SetColorMode(ASWLog::ColorMode::Never);
    CheckEquals(ASWLog::ColorMode::Never, logger.GetColorMode(), "SetColorMode(Never) should be reflected by GetColorMode()");

    logger.SetColorMode(ASWLog::ColorMode::Auto);
    CheckEquals(ASWLog::ColorMode::Auto, logger.GetColorMode(), "SetColorMode(Auto) should be reflected by GetColorMode()");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_GetInstance_ReturnsSameInstance()
{
    // Act
    auto& first = ASWLog::TASWConsoleLog::GetInstance();
    auto& second = ASWLog::TASWConsoleLog::GetInstance();

    // Assert
    CheckSame(&first, &second, "GetInstance should return the same logger on every call");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_GetLevelColor_OffHasNoColor()
{
    // Arrange
    ASWLog::TASWConsoleLog logger;
    const std::string defaultCriticalColor = logger.GetLevelColor(ASWLog::Level::Critical);

    // Act
    logger.SetLevelColor(ASWLog::Level::Off, "\x1b[35m");
    logger.ResetLevelColor(ASWLog::Level::Off);
    const auto offColor = logger.GetLevelColor(ASWLog::Level::Off);

    // Assert
    CheckEmpty(offColor, "Off isn't a severity, so it should have no color, and setting one should be ignored");
    CheckEquals(defaultCriticalColor, logger.GetLevelColor(ASWLog::Level::Critical),
        "Setting or resetting Off's color should not change another level's color");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_Initialize_SuppressesInfoBannersBelowMinimumLevel()
{
    // Arrange
    ASWLog::TASWLogConfig config;
    config.InitialMinimumLevel = ASWLog::Level::Warn;
    config.Startup.Banner = "should_not_appear_banner";
    config.Shutdown.WriteLine = false;
    // Startup.Write* toggles are left at their defaults (all true) so this test exercises
    // every internal Info-level banner writer, not just a subset.

    ASWLog::TASWConsoleLog logger;
    logger.SetColorMode(ASWLog::ColorMode::Never);

    // Act
    TStreamCapture outCapture(std::cout);
    TStreamCapture errCapture(std::cerr);
    const bool initialized = logger.Initialize(config);
    logger.LogError("this_error_should_appear");
    logger.Close();

    const auto outContents = outCapture.Str();
    const auto errContents = errCapture.Str();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckNotContains(outContents, "should_not_appear_banner", "Startup.Banner (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckNotContains(outContents, "Time:", "Init time info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckNotContains(outContents, "OS:", "Init OS info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckNotContains(outContents, "Drive:", "Init drive info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckNotContains(outContents, "System memory:", "Init system memory info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckNotContains(outContents, "App:", "Init application info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckNotContains(outContents, "App Memory:", "Init app memory info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckContains(errContents, "this_error_should_appear", "Messages at or above InitialMinimumLevel should still be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_Initialize_WritesDriveInfoWhenEnabled()
{
    // Arrange
    auto config = MakeQuietConfig();
    config.Startup.WriteDriveInfo = true;

    ASWLog::TASWConsoleLog logger;
    logger.SetColorMode(ASWLog::ColorMode::Never);

    // Act
    TStreamCapture outCapture(std::cout);
    const bool initialized = logger.Initialize(config);
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckContains(outCapture.Str(), "Drive:", "Startup.WriteDriveInfo should write disk space diagnostics to the console when enabled");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_IsColorSupported_ReflectsDetectedStreams()
{
    // Arrange
    auto config = MakeQuietConfig();

    TColorDetectConsoleLog noStreamLogger;

    TColorDetectConsoleLog stdErrOnlyLogger;
    stdErrOnlyLogger.StdErrSupportsColor = true;

    // Assert: color support is not yet determined before Initialize() runs
    CheckFalse(stdErrOnlyLogger.IsColorSupported(), "IsColorSupported should be false before Initialize() runs");

    // Act
    const bool noStreamInitialized = noStreamLogger.Initialize(config);
    const bool stdErrOnlyInitialized = stdErrOnlyLogger.Initialize(config);
    noStreamLogger.Close();
    stdErrOnlyLogger.Close();

    // Assert
    CheckTrue(noStreamInitialized && stdErrOnlyInitialized, "Initialize should succeed");
    CheckFalse(noStreamLogger.IsColorSupported(), "IsColorSupported should be false when neither stream shows colors");
    CheckTrue(stdErrOnlyLogger.IsColorSupported(), "IsColorSupported should be true when either stream shows colors");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_LogLineMetadata_Options()
{
    // Arrange
    auto config = MakeQuietConfig();
    config.Line.ShowTimestamp = true;
    config.Line.ShowLevel = true;
    config.Line.ShowProcessId = true;
    config.Line.ShowThreadId = true;
    config.Line.ShowFunctionName = true;
    config.Line.ShowSourceLine = true;

    ASWLog::TASWConsoleLog logger;
    logger.SetColorMode(ASWLog::ColorMode::Never);

    // Act
    TStreamCapture outCapture(std::cout);
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("metadata_message");
    logger.Close();

    const auto contents = outCapture.Str();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckContains(contents, "Z", "Line.ShowTimestamp should add a UTC timestamp");
    CheckContains(contents, "INFO", "Line.ShowLevel should include the log level");
    CheckContains(contents, "[P:", "Line.ShowProcessId should include the process id");
    CheckContains(contents, "[T:", "Line.ShowThreadId should include the thread id");
    CheckContains(contents, "Test_LogLineMetadata_Options", "Line.ShowFunctionName should include the calling method name");
    CheckContains(contents, "metadata_message", "Metadata log line should still contain the message");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_LogProcessAndThreadIds_AreOSIds()
{
    // Arrange
    auto config = MakeQuietConfig();
    config.Line.ShowProcessId = true;
    config.Line.ShowThreadId = true;

    ASWLog::TASWConsoleLog logger;
    logger.SetColorMode(ASWLog::ColorMode::Never);
    const auto processId = ASWLog::GetCurrentOSProcessId();
    const auto mainThreadId = ASWLog::GetCurrentOSThreadId();
    std::uint32_t workerThreadId = 0;
    std::string contents;

    // Act: log from this thread and from another one. The capture ends before the checks, so their messages aren't
    // captured.
    {
        TStreamCapture captureOut(std::cout);
        logger.Initialize(config);
        logger.LogInfo("main_thread_entry");

        std::thread worker([&] {
            workerThreadId = ASWLog::GetCurrentOSThreadId();
            logger.LogInfo("worker_thread_entry");
                });
        worker.join();
        logger.Close();
        contents = captureOut.Str();
    }

    // Assert
    const auto processTag = "[P:" + std::to_string(processId) + "]";
    CheckTrue(mainThreadId != workerThreadId, "Two threads should have different OS thread ids");
    CheckContains(contents, processTag + "[T:" + std::to_string(mainThreadId) + "]: main_thread_entry", "The entry should show the OS process id and the logging thread's OS thread id");
    CheckContains(contents, processTag + "[T:" + std::to_string(workerThreadId) + "]: worker_thread_entry", "An entry from another thread should show that thread's OS thread id");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_LogRawAndForceOptions()
{
    // Arrange
    auto config = MakeQuietConfig();

    ASWLog::TASWConsoleLog logger;
    logger.SetColorMode(ASWLog::ColorMode::Never);

    // Act
    TStreamCapture outCapture(std::cout);
    TStreamCapture errCapture(std::cerr);
    const bool initialized = logger.Initialize(config);
    logger.LogRaw(ASWLog::Level::Info, "raw_message");
    logger.LogForceRaw(ASWLog::Level::Warn, "raw_force_message");
    logger.Close();

    const auto outContents = outCapture.Str();
    const auto errContents = errCapture.Str();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckContains(outContents, "raw_message", "LogRaw should write the raw message to stdout");
    CheckContains(errContents, "raw_force_message", "LogForceRaw should write the raw message to stderr even when filtered");
    CheckNotContains(outContents, "raw_message\n", "LogRaw should not append a newline by default");
    CheckNotContains(errContents, "raw_force_message\n", "LogForceRaw should not append a trailing newline");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_LogRespectsMinimumLevel()
{
    // Arrange
    auto config = MakeQuietConfig();
    config.InitialMinimumLevel = ASWLog::Level::Error;

    ASWLog::TASWConsoleLog logger;
    logger.SetColorMode(ASWLog::ColorMode::Never);

    // Act
    TStreamCapture outCapture(std::cout);
    TStreamCapture errCapture(std::cerr);
    const bool initialized = logger.Initialize(config);
    logger.LogWarn("filtered_message");
    logger.LogForce(ASWLog::Level::Warn, "forced_message");
    logger.LogError("allowed_message");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckNotContains(errCapture.Str(), "filtered_message", "Log should respect the minimum level unless forced");
    CheckContains(errCapture.Str(), "forced_message", "LogForce should bypass the minimum level");
    CheckContains(errCapture.Str(), "allowed_message", "Log should write a message at or above the minimum level");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_OnError_ReportsOnlyTheWriteThatFailedTheStream()
{
    // Arrange
    auto config = MakeQuietConfig();
    config.ErrorReportInterval = std::chrono::milliseconds(0);

    std::vector<ASWLog::TASWLogError> reports;
    config.OnError = [&reports](const ASWLog::TASWLogError& error) {
            reports.push_back(error);
        };

    ASWLog::TASWConsoleLog logger;
    logger.SetColorMode(ASWLog::ColorMode::Never);

    // Act: stdout fails, and stays failed
    bool initialized = false;
    {
        TFailingOutput failingOut(std::cout);
        initialized = logger.Initialize(config);
        logger.LogInfo("lost_first");
        logger.LogInfo("lost_second");
        logger.Close();
    }

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(1, reports.size(), "Only the write that failed the stream should be reported, not every line after it");
    if (reports.empty())
        return;

    CheckEquals(ASWLog::ErrorKind::WriteFailed, reports[0].Kind, "A failed console write should be reported as WriteFailed");
    CheckEquals(std::string("Couldn't write to stdout"), reports[0].Message, "The report should name the stream");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_OnLogEntry_FiresForQualifyingLevelsOnly()
{
    // Arrange
    auto config = MakeQuietConfig();
    config.OnLogEntryMinimumLevel = ASWLog::Level::Error;

    std::vector<std::string> callbackMessages;
    config.OnLogEntry = [&callbackMessages](const ASWLog::TASWLogRecord&, std::string_view line)
        {
            callbackMessages.emplace_back(line);
        };

    ASWLog::TASWConsoleLog logger;
    logger.SetColorMode(ASWLog::ColorMode::Never);

    // Act
    TStreamCapture outCapture(std::cout);
    TStreamCapture errCapture(std::cerr);
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("below_threshold");
    logger.LogError("above_threshold");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckEquals(1, callbackMessages.size(), "OnLogEntry should only fire for entries at or above OnLogEntryMinimumLevel");
    if (!callbackMessages.empty())
        CheckContains(callbackMessages[0], "above_threshold", "Callback should receive the same formatted line written to the console");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_ResetLevelColor_RestoresDefault()
{
    // Arrange
    ASWLog::TASWConsoleLog logger;

    const std::string defaultInfoColor = logger.GetLevelColor(ASWLog::Level::Info);
    logger.SetLevelColor(ASWLog::Level::Info, "\x1b[35m");

    // Act
    logger.ResetLevelColor(ASWLog::Level::Info);
    const auto restoredColor = logger.GetLevelColor(ASWLog::Level::Info);

    // Assert
    CheckEquals(defaultInfoColor, restoredColor, "ResetLevelColor should restore the level's built-in default color");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_ResetLevelColors_RestoresAllDefaults()
{
    // Arrange
    ASWLog::TASWConsoleLog logger;

    const std::string defaultInfoColor = logger.GetLevelColor(ASWLog::Level::Info);
    const std::string defaultErrorColor = logger.GetLevelColor(ASWLog::Level::Error);
    logger.SetLevelColor(ASWLog::Level::Info, "\x1b[35m");
    logger.SetLevelColor(ASWLog::Level::Error, "\x1b[34m");

    // Act
    logger.ResetLevelColors();

    // Assert
    CheckEquals(defaultInfoColor, logger.GetLevelColor(ASWLog::Level::Info), "ResetLevelColors should restore Info's default color");
    CheckEquals(defaultErrorColor, logger.GetLevelColor(ASWLog::Level::Error), "ResetLevelColors should restore Error's default color");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_SetLevelColor_EmptyStringDisablesColorForLevel()
{
    // Arrange
    auto config = MakeQuietConfig();

    ASWLog::TASWConsoleLog logger;
    logger.SetColorMode(ASWLog::ColorMode::Always);
    logger.SetLevelColor(ASWLog::Level::Info, "");

    // Act
    TStreamCapture outCapture(std::cout);
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("uncolored_message");
    logger.Close();

    const auto contents = outCapture.Str();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckNotContains(contents, "\x1b[", "An empty level color should suppress both the color prefix and the reset code for that level");
    CheckContains(contents, "uncolored_message", "The message should still be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_SetLevelColor_OverridesDefaultColor()
{
    // Arrange
    auto config = MakeQuietConfig();

    ASWLog::TASWConsoleLog logger;
    logger.SetColorMode(ASWLog::ColorMode::Always);

    const std::string defaultInfoColor = logger.GetLevelColor(ASWLog::Level::Info);
    const std::string customColor = "\x1b[35m"; // magenta

    // Act
    logger.SetLevelColor(ASWLog::Level::Info, customColor);
    const auto updatedColor = logger.GetLevelColor(ASWLog::Level::Info);

    TStreamCapture outCapture(std::cout);
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("custom_colored_message");
    logger.Close();

    const auto contents = outCapture.Str();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckNotEmpty(defaultInfoColor, "GetLevelColor should return a non-empty default color for Info");
    CheckEquals(customColor, updatedColor, "GetLevelColor should reflect the color set via SetLevelColor");
    CheckContains(contents, customColor, "WriteLogEntry should use the custom color for Info");
    CheckNotContains(contents, defaultInfoColor, "The default color should no longer appear for Info once overridden");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_WarnAndAboveWriteToStdErr()
{
    // Arrange
    auto config = MakeQuietConfig();

    ASWLog::TASWConsoleLog logger;
    logger.SetColorMode(ASWLog::ColorMode::Never);

    // Act
    TStreamCapture outCapture(std::cout);
    TStreamCapture errCapture(std::cerr);
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("stdout_message");
    logger.LogWarn("stderr_message");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckContains(outCapture.Str(), "stdout_message", "Info and below should be written to stdout");
    CheckContains(errCapture.Str(), "stderr_message", "Warn and above should be written to stderr");
    CheckNotContains(outCapture.Str(), "stderr_message", "Warn message should not also appear on stdout");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_ConsoleLog)
