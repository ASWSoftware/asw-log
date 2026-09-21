/* **************************************************************************
Test_ASWLog_Base.cpp
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
#include "Test_ASWLog_Base.h"
//---------------------------------------------------------------------------
#include <string>
//---------------------------------------------------------------------------
#include "ASWLog_Base.h"
//---------------------------------------------------------------------------

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

protected:
    std::string_view GetLoggerClassName() const noexcept override
    {
        return "TTestLogger";
    }

public:
    bool Initialize(const ASWLog::TASWLogConfig& config) override
    {
        m_Config = config;
        m_MinimumLevel.store(m_Config.InitialMinimumLevel, std::memory_order_release);
        m_IsInitialized.store(true, std::memory_order_release);
        return true;
    }

    bool Open() override
    {
        m_IsInitialized.store(true, std::memory_order_release);
        return true;
    }

    bool Close() override
    {
        m_IsInitialized.store(false, std::memory_order_release);
        return true;
    }

    bool IsOpen() const noexcept override
    {
        return m_IsInitialized.load(std::memory_order_acquire);
    }

    void Log(ASWLog::Level level, std::string_view message, std::source_location loc = std::source_location::current()) override
    {
        static_cast<void>(loc);
        LastLevel = level;
        LastMessage = std::string(message);
    }

    void LogRaw(ASWLog::Level level, std::string_view message, std::source_location loc = std::source_location::current()) override
    {
        Log(level, message, loc);
    }

    void LogForce(ASWLog::Level level, std::string_view message, std::source_location loc = std::source_location::current()) override
    {
        Log(level, message, loc);
    }

    void LogForceRaw(ASWLog::Level level, std::string_view message, std::source_location loc = std::source_location::current()) override
    {
        Log(level, message, loc);
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
    RegisterTest(&TTest_ASWLog_Base::Test_GetConfig_ReturnsLiveMutableReference, "GetConfig_ReturnsLiveMutableReference");
    RegisterTest(&TTest_ASWLog_Base::Test_GetFullVersionStr_ContainsVersion, "GetFullVersionStr_ContainsVersion");
    RegisterTest(&TTest_ASWLog_Base::Test_LogLevelConvenienceMethods, "LogLevelConvenienceMethods");
    RegisterTest(&TTest_ASWLog_Base::Test_SetGetMinimumLevel_RoundTrips, "SetGetMinimumLevel_RoundTrips");
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
void TTest_ASWLog_Base::Test_GetConfig_ReturnsLiveMutableReference()
{
    // Arrange
    TTestLogger logger;
    const TTestLogger& constLogger = logger;

    // Assert: defaults are visible through the non-const overload
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Info), static_cast<int32_t>(logger.GetConfig().InitialMinimumLevel), __func__, __LINE__, "Default minimum level should be Info");

    // Act: mutate through the reference returned by the non-const overload
    logger.GetConfig().InitialMinimumLevel = ASWLog::Level::Error;

    // Assert: the mutation persists on subsequent reads, proving GetConfig() returns a live
    // reference rather than a copy - callers rely on this to configure a logger in place
    // (e.g. `logger.GetConfig().InitialMinimumLevel = X;`) before calling Initialize().
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Error), static_cast<int32_t>(logger.GetConfig().InitialMinimumLevel), __func__, __LINE__, "GetConfig() should return a live reference so external mutation persists");

    // Assert: the const overload observes the same underlying config, not a stale copy
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Error), static_cast<int32_t>(constLogger.GetConfig().InitialMinimumLevel), __func__, __LINE__, "The const GetConfig() overload should observe the same underlying config as the non-const overload");
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
void TTest_ASWLog_Base::Test_LogLevelConvenienceMethods()
{
    // Arrange
    TTestLogger logger;

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

} // namespace ASWUnitTests
