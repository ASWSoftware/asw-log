/* **************************************************************************
Test_ASWLog_ConsoleLog.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 Anthony S. West

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
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#undef min
#undef max
#endif
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_ConsoleLog.h"
#include "ASWLog_Utils.h"
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

// RAII helper: sets an environment variable (or removes it, given std::nullopt), restoring its previous value on
// destruction.
class TScopedEnvironmentVariable
{
private:
    std::string m_Name;
    std::optional<std::string> m_PreviousValue;

    static std::optional<std::string> Get(const std::string& name)
    {
#if defined(_WIN32)
        // The Windows API rather than the C runtime, since that's what TASWConsoleLog reads (and MinGW's runtime has
        // no _dupenv_s)
        const DWORD size = GetEnvironmentVariableA(name.c_str(), nullptr, 0);
        if (size == 0)
            return std::nullopt;

        std::string value(size, '\0');
        value.resize(GetEnvironmentVariableA(name.c_str(), value.data(), size));
        return value;
#else
        const char* value = std::getenv(name.c_str());
        if (value == nullptr)
            return std::nullopt;

        return std::string(value);
#endif
    }

    static void Set(const std::string& name, const std::optional<std::string>& value)
    {
#if defined(_WIN32)
        SetEnvironmentVariableA(name.c_str(), value ? value->c_str() : nullptr); // nullptr removes the variable
#else
        if (value)
            setenv(name.c_str(), value->c_str(), 1);
        else
            unsetenv(name.c_str());
#endif
    }

public:
    TScopedEnvironmentVariable(std::string name, std::optional<std::string> value)
        : m_Name(std::move(name)),
          m_PreviousValue(Get(m_Name))
    {
        Set(m_Name, value);
    }

    ~TScopedEnvironmentVariable()
    {
        Set(m_Name, m_PreviousValue);
    }

    TScopedEnvironmentVariable(const TScopedEnvironmentVariable&) = delete;
    TScopedEnvironmentVariable& operator=(const TScopedEnvironmentVariable&) = delete;
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
    config.LogUTCDateTime = false;
    config.LogLevelStr = false;
    config.LogProcessId = false;
    config.LogThreadId = false;
    config.LogMethodName = false;
    config.LogSourceLine = false;
    config.WriteShutdownLog = false;
    config.Init_LogTimeInfo = false;
    config.Init_LogOSInfo = false;
    config.Init_LogDriveInfo = false;
    config.Init_LogSysMemInfo = false;
    config.Init_LogApplicationInfo = false;
    config.Init_LogMemoryUsage = false;
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
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_GetColorMode_ReflectsSetColorMode, "GetColorMode_ReflectsSetColorMode");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_GetInstance_ReturnsSameInstance, "GetInstance_ReturnsSameInstance");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_Initialize_SuppressesInfoBannersBelowMinimumLevel, "Initialize_SuppressesInfoBannersBelowMinimumLevel");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_Initialize_WritesDriveInfoWhenEnabled, "Initialize_WritesDriveInfoWhenEnabled");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_IsColorSupported_ReflectsDetectedStreams, "IsColorSupported_ReflectsDetectedStreams");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_LogLineMetadata_Options, "LogLineMetadata_Options");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_LogProcessAndThreadIds_AreOSIds, "LogProcessAndThreadIds_AreOSIds");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_LogRawAndForceOptions, "LogRawAndForceOptions");
    RegisterTest(&TTest_ASWLog_ConsoleLog::Test_LogRespectsMinimumLevel, "LogRespectsMinimumLevel");
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
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(outCapture.Str().find("\x1b[") != std::string::npos, __func__, __LINE__, "ColorMode::Always should wrap output with ANSI escape codes");
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
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(logger.GetColorMode() == ASWLog::ColorMode::Auto, __func__, __LINE__, "ColorMode should default to Auto");
    CheckTrue(outContents.find("\x1b[") != std::string::npos, __func__, __LINE__, "Auto should color a stream that shows colors");
    CheckTrue(errContents.find("\x1b[") == std::string::npos, __func__, __LINE__, "Auto should not color a stream that doesn't show colors, such as a file or a pipe");
    CheckTrue(errContents.find("stderr_message") != std::string::npos, __func__, __LINE__, "The uncolored message should still be written");
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

    // Assert
    CheckTrue(autoInitialized && alwaysInitialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(autoContents.find("\x1b[") == std::string::npos, __func__, __LINE__, "Auto should not color any stream when NO_COLOR is set");
    CheckTrue(autoContents.find("auto_message") != std::string::npos, __func__, __LINE__, "The uncolored message should still be written");
    CheckTrue(alwaysContents.find("\x1b[") != std::string::npos, __func__, __LINE__, "Always should color even when NO_COLOR is set");
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
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(contents.find("\x1b[") == std::string::npos, __func__, __LINE__, "ColorMode::Never should suppress all ANSI escape codes");
    CheckTrue(contents.find("plain_message") != std::string::npos, __func__, __LINE__, "The message should still be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_GetColorMode_ReflectsSetColorMode()
{
    // Arrange
    ASWLog::TASWConsoleLog logger;

    // Assert
    CheckTrue(logger.GetColorMode() == ASWLog::ColorMode::Auto, __func__, __LINE__, "ColorMode should default to Auto");

    // Act & Assert
    logger.SetColorMode(ASWLog::ColorMode::Always);
    CheckTrue(logger.GetColorMode() == ASWLog::ColorMode::Always, __func__, __LINE__, "SetColorMode(Always) should be reflected by GetColorMode()");

    logger.SetColorMode(ASWLog::ColorMode::Never);
    CheckTrue(logger.GetColorMode() == ASWLog::ColorMode::Never, __func__, __LINE__, "SetColorMode(Never) should be reflected by GetColorMode()");

    logger.SetColorMode(ASWLog::ColorMode::Auto);
    CheckTrue(logger.GetColorMode() == ASWLog::ColorMode::Auto, __func__, __LINE__, "SetColorMode(Auto) should be reflected by GetColorMode()");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_GetInstance_ReturnsSameInstance()
{
    // Act
    auto& first = ASWLog::TASWConsoleLog::GetInstance();
    auto& second = ASWLog::TASWConsoleLog::GetInstance();

    // Assert
    CheckTrue(&first == &second, __func__, __LINE__, "GetInstance should return the same logger on every call");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_Initialize_SuppressesInfoBannersBelowMinimumLevel()
{
    // Arrange
    ASWLog::TASWLogConfig config;
    config.InitialMinimumLevel = ASWLog::Level::Warn;
    config.BannerMessage_Init = "should_not_appear_banner";
    config.WriteShutdownLog = false;
    // Init_Log* toggles are left at their defaults (all true) so this test exercises
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
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(outContents.find("should_not_appear_banner") == std::string::npos, __func__, __LINE__, "BannerMessage_Init (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckTrue(outContents.find("Time:") == std::string::npos, __func__, __LINE__, "Init time info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckTrue(outContents.find("OS:") == std::string::npos, __func__, __LINE__, "Init OS info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckTrue(outContents.find("Drive:") == std::string::npos, __func__, __LINE__, "Init drive info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckTrue(outContents.find("System memory:") == std::string::npos, __func__, __LINE__, "Init system memory info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckTrue(outContents.find("App:") == std::string::npos, __func__, __LINE__, "Init application info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckTrue(outContents.find("App Memory:") == std::string::npos, __func__, __LINE__, "Init app memory info (Info level) should be suppressed when InitialMinimumLevel is Error");
    CheckTrue(errContents.find("this_error_should_appear") != std::string::npos, __func__, __LINE__, "Messages at or above InitialMinimumLevel should still be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_Initialize_WritesDriveInfoWhenEnabled()
{
    // Arrange
    auto config = MakeQuietConfig();
    config.Init_LogDriveInfo = true;

    ASWLog::TASWConsoleLog logger;
    logger.SetColorMode(ASWLog::ColorMode::Never);

    // Act
    TStreamCapture outCapture(std::cout);
    const bool initialized = logger.Initialize(config);
    logger.Close();

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(outCapture.Str().find("Drive:") != std::string::npos, __func__, __LINE__, "Init_LogDriveInfo should write disk space diagnostics to the console when enabled");
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
    CheckFalse(stdErrOnlyLogger.IsColorSupported(), __func__, __LINE__, "IsColorSupported should be false before Initialize() runs");

    // Act
    const bool noStreamInitialized = noStreamLogger.Initialize(config);
    const bool stdErrOnlyInitialized = stdErrOnlyLogger.Initialize(config);
    noStreamLogger.Close();
    stdErrOnlyLogger.Close();

    // Assert
    CheckTrue(noStreamInitialized && stdErrOnlyInitialized, __func__, __LINE__, "Initialize should succeed");
    CheckFalse(noStreamLogger.IsColorSupported(), __func__, __LINE__, "IsColorSupported should be false when neither stream shows colors");
    CheckTrue(stdErrOnlyLogger.IsColorSupported(), __func__, __LINE__, "IsColorSupported should be true when either stream shows colors");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_LogLineMetadata_Options()
{
    // Arrange
    auto config = MakeQuietConfig();
    config.LogUTCDateTime = true;
    config.LogLevelStr = true;
    config.LogProcessId = true;
    config.LogThreadId = true;
    config.LogMethodName = true;
    config.LogSourceLine = true;

    ASWLog::TASWConsoleLog logger;
    logger.SetColorMode(ASWLog::ColorMode::Never);

    // Act
    TStreamCapture outCapture(std::cout);
    const bool initialized = logger.Initialize(config);
    logger.LogInfo("metadata_message");
    logger.Close();

    const auto contents = outCapture.Str();

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(contents.find("Z") != std::string::npos, __func__, __LINE__, "LogUTCDateTime should add a UTC timestamp");
    CheckTrue(contents.find("INFO") != std::string::npos, __func__, __LINE__, "LogLevelStr should include the log level");
    CheckTrue(contents.find("[P:") != std::string::npos, __func__, __LINE__, "LogProcessId should include the process id");
    CheckTrue(contents.find("[T:") != std::string::npos, __func__, __LINE__, "LogThreadId should include the thread id");
    CheckTrue(contents.find("Test_LogLineMetadata_Options") != std::string::npos, __func__, __LINE__, "LogMethodName should include the calling method name");
    CheckTrue(contents.find("metadata_message") != std::string::npos, __func__, __LINE__, "Metadata log line should still contain the message");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_LogProcessAndThreadIds_AreOSIds()
{
    // Arrange
    auto config = MakeQuietConfig();
    config.LogProcessId = true;
    config.LogThreadId = true;

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
    CheckTrue(mainThreadId != workerThreadId, __func__, __LINE__, "Two threads should have different OS thread ids");
    CheckTrue(contents.find(processTag + "[T:" + std::to_string(mainThreadId) + "]: main_thread_entry") != std::string::npos, __func__, __LINE__, "The entry should show the OS process id and the logging thread's OS thread id");
    CheckTrue(contents.find(processTag + "[T:" + std::to_string(workerThreadId) + "]: worker_thread_entry") != std::string::npos, __func__, __LINE__, "An entry from another thread should show that thread's OS thread id");
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
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(outContents.find("raw_message") != std::string::npos, __func__, __LINE__, "LogRaw should write the raw message to stdout");
    CheckTrue(errContents.find("raw_force_message") != std::string::npos, __func__, __LINE__, "LogForceRaw should write the raw message to stderr even when filtered");
    CheckTrue(outContents.find("raw_message\n") == std::string::npos, __func__, __LINE__, "LogRaw should not append a newline by default");
    CheckTrue(errContents.find("raw_force_message\n") == std::string::npos, __func__, __LINE__, "LogForceRaw should not append a trailing newline");
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
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(errCapture.Str().find("filtered_message") == std::string::npos, __func__, __LINE__, "Log should respect the minimum level unless forced");
    CheckTrue(errCapture.Str().find("forced_message") != std::string::npos, __func__, __LINE__, "LogForce should bypass the minimum level");
    CheckTrue(errCapture.Str().find("allowed_message") != std::string::npos, __func__, __LINE__, "Log should write a message at or above the minimum level");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_ConsoleLog::Test_OnLogEntry_FiresForQualifyingLevelsOnly()
{
    // Arrange
    auto config = MakeQuietConfig();
    config.CallbackMinimumLevel = ASWLog::Level::Error;

    std::vector<std::string> callbackMessages;
    config.OnLogEntry = [&callbackMessages](ASWLog::Level, std::string_view line)
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
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckEquals(static_cast<size_t>(1), callbackMessages.size(), __func__, __LINE__, "OnLogEntry should only fire for entries at or above CallbackMinimumLevel");
    if (!callbackMessages.empty())
        CheckTrue(callbackMessages[0].find("above_threshold") != std::string::npos, __func__, __LINE__, "Callback should receive the same formatted line written to the console");
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
    CheckEquals(defaultInfoColor, restoredColor, __func__, __LINE__, "ResetLevelColor should restore the level's built-in default color");
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
    CheckEquals(defaultInfoColor, logger.GetLevelColor(ASWLog::Level::Info), __func__, __LINE__, "ResetLevelColors should restore Info's default color");
    CheckEquals(defaultErrorColor, logger.GetLevelColor(ASWLog::Level::Error), __func__, __LINE__, "ResetLevelColors should restore Error's default color");
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
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(contents.find("\x1b[") == std::string::npos, __func__, __LINE__, "An empty level color should suppress both the color prefix and the reset code for that level");
    CheckTrue(contents.find("uncolored_message") != std::string::npos, __func__, __LINE__, "The message should still be written");
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
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckFalse(defaultInfoColor.empty(), __func__, __LINE__, "GetLevelColor should return a non-empty default color for Info");
    CheckEquals(customColor, updatedColor, __func__, __LINE__, "GetLevelColor should reflect the color set via SetLevelColor");
    CheckTrue(contents.find(customColor) != std::string::npos, __func__, __LINE__, "WriteLogEntry should use the custom color for Info");
    CheckTrue(contents.find(defaultInfoColor) == std::string::npos, __func__, __LINE__, "The default color should no longer appear for Info once overridden");
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
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(outCapture.Str().find("stdout_message") != std::string::npos, __func__, __LINE__, "Info and below should be written to stdout");
    CheckTrue(errCapture.Str().find("stderr_message") != std::string::npos, __func__, __LINE__, "Warn and above should be written to stderr");
    CheckTrue(outCapture.Str().find("stderr_message") == std::string::npos, __func__, __LINE__, "Warn message should not also appear on stdout");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_ConsoleLog)
