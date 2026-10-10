/* **************************************************************************
Test_ASWLog_NullLog.cpp
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
#include "Test_ASWLog_NullLog.h"
//---------------------------------------------------------------------------
#include <atomic>
#include <format>
#include <string>
#include <thread>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_CategoryLog.h"
#include "ASWLog_MultiLog.h"
#include "ASWLog_NullLog.h"
#include "ASWLog_Version.h"
//---------------------------------------------------------------------------

namespace
{

// A value whose formatting is counted in NullLogFormatCount, to check that no *Fmt call formats
struct TNullLogCountedValue
{
};

int NullLogFormatCount = 0;

} // namespace

template<>
struct std::formatter<TNullLogCountedValue>
{
    constexpr std::format_parse_context::iterator parse(std::format_parse_context& context)
    {
        return context.begin();
    }

    std::format_context::iterator format(const TNullLogCountedValue& /*value*/, std::format_context& context) const
    {
        ++NullLogFormatCount;
        return std::format_to(context.out(), "counted");
    }
};

namespace ASWUnitTests
{

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_NullLog
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_NullLog::TTest_ASWLog_NullLog()
    : inherited("ASWLog_NullLog_Tests")
{
    RegisterTest(&TTest_ASWLog_NullLog::Test_Fmt_NeverFormats, "Fmt_NeverFormats");
    RegisterTest(&TTest_ASWLog_NullLog::Test_GetConfig_IsSafeWhileReconfiguring, "GetConfig_IsSafeWhileReconfiguring");
    RegisterTest(&TTest_ASWLog_NullLog::Test_GetInstance_ReturnsTheSameLogger, "GetInstance_ReturnsTheSameLogger");
    RegisterTest(&TTest_ASWLog_NullLog::Test_Lifecycle_SucceedsAndKeepsTheConfig, "Lifecycle_SucceedsAndKeepsTheConfig");
    RegisterTest(&TTest_ASWLog_NullLog::Test_ShouldLog_IsAlwaysFalse, "ShouldLog_IsAlwaysFalse");
    RegisterTest(&TTest_ASWLog_NullLog::Test_Wrapped_WritesNothing, "Wrapped_WritesNothing");
}
//---------------------------------------------------------------------------
TTest_ASWLog_NullLog::~TTest_ASWLog_NullLog()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_NullLog::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_NullLog::SetUp_Test(ITestCase& /*testCase*/)
{
    NullLogFormatCount = 0;
}
//---------------------------------------------------------------------------
void TTest_ASWLog_NullLog::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_NullLog::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_NullLog::Test_Fmt_NeverFormats()
{
    // Arrange: everything a real logger would need to write an entry
    ASWLog::TASWNullLog log;
    log.SetMinimumLevel(ASWLog::Level::Trace);
    log.SetEnabled(true);

    // Act
    log.LogTraceFmt("{}", TNullLogCountedValue{});
    log.LogCriticalFmt("{}", TNullLogCountedValue{});
    log.LogRawFmt(ASWLog::Level::Error, "{}", TNullLogCountedValue{});
    log.LogForceFmt(ASWLog::Level::Critical, "{}", TNullLogCountedValue{});
    log.LogForceRawFmt(ASWLog::Level::Critical, "{}", TNullLogCountedValue{});
    log.LogInfoFmt({ { "orderId", 17 } }, "{}", TNullLogCountedValue{});

    // Assert
    CheckEquals(0, NullLogFormatCount, "No *Fmt call should format its message");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_NullLog::Test_GetConfig_IsSafeWhileReconfiguring()
{
    // Arrange: two threads reconfigure while two read the config (for ThreadSanitizer)
    ASWLog::TASWNullLog log;
    ASWLog::TASWLogConfig first;
    first.Startup.Banner = "first";
    ASWLog::TASWLogConfig second;
    second.Startup.Banner = "second";
    log.Initialize(first);
    std::atomic<int> unexpectedCount{ 0 };
    std::atomic<int> failedCount{ 0 };

    // Act
    std::vector<std::thread> threads;
    for (int threadIndex = 0; threadIndex < 4; ++threadIndex)
    {
        threads.emplace_back([&, threadIndex] {
                for (int index = 0; index < 500; ++index)
                {
                    if (threadIndex < 2)
                    {
                        if (!log.Reconfigure(index % 2 == 0 ? second : first))
                            failedCount.fetch_add(1);
                    }
                    else
                    {
                        const auto banner = log.GetConfig()->Startup.Banner;
                        if (banner != "first" && banner != "second")
                            unexpectedCount.fetch_add(1);
                    }
                }
            });
    }

    for (auto& thread : threads)
        thread.join();

    // Assert
    CheckEquals(0, failedCount.load(), "Every Reconfigure() should succeed");
    CheckEquals(0, unexpectedCount.load(), "Every read should see one of the configs whole");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_NullLog::Test_GetInstance_ReturnsTheSameLogger()
{
    // Act
    auto& first = ASWLog::TASWNullLog::GetInstance();
    auto& second = ASWLog::TASWNullLog::GetInstance();

    // Assert
    CheckSame(&first, &second, "GetInstance() should always return the same logger");
    CheckFalse(first.ShouldLog(ASWLog::Level::Critical), "The shared instance should write nothing either");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_NullLog::Test_Lifecycle_SucceedsAndKeepsTheConfig()
{
    // Arrange
    ASWLog::TASWNullLog log;
    ASWLog::TASWLogConfig config;
    config.Startup.Banner = "initialized";
    config.InitialMinimumLevel = ASWLog::Level::Debug;
    auto changedConfig = config;
    changedConfig.Startup.Banner = "reconfigured";
    changedConfig.InitialMinimumLevel = ASWLog::Level::Error;

    // Act
    const auto defaultConfig = log.GetConfig();
    const bool initialized = log.Initialize(config);
    const auto initializedConfig = log.GetConfig();
    const auto levelAfterInitialize = log.GetMinimumLevel();
    const bool reconfigured = log.Reconfigure(changedConfig);
    const auto levelAfterReconfigure = log.GetMinimumLevel();
    log.SetMinimumLevel(ASWLog::Level::Warn);
    log.DumpBacktrace();

    // Assert
    AssertNotNull(defaultConfig, "GetConfig() should return a config before Initialize()");
    CheckEmpty(defaultConfig->Startup.Banner, "The config before Initialize() should be the default");
    CheckTrue(initialized && reconfigured, "Initialize() and Reconfigure() should succeed");
    CheckEquals(std::string("initialized"), initializedConfig->Startup.Banner, "An earlier snapshot should stay unchanged");
    CheckEquals(std::string("reconfigured"), log.GetConfig()->Startup.Banner, "GetConfig() should return the last config passed in");
    CheckEquals(ASWLog::Level::Debug, levelAfterInitialize, "Initialize() should seed the minimum level");
    CheckEquals(ASWLog::Level::Debug, levelAfterReconfigure, "Reconfigure() should keep the minimum level");
    CheckEquals(ASWLog::Level::Warn, log.GetMinimumLevel(), "The minimum level should read back as set");
    CheckTrue(log.Open() && log.Flush() && log.IsOpen() && log.Close(), "Open(), Flush(), IsOpen() and Close() should succeed");
    CheckContains(log.GetFullVersionStr(), "TASWNullLog", "The full version should name the logger");
    CheckEquals(std::string(ASWLog::Version), std::string(log.GetVersionStr()), "The version should be the library's");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_NullLog::Test_ShouldLog_IsAlwaysFalse()
{
    // Arrange
    ASWLog::TASWNullLog log;
    log.SetMinimumLevel(ASWLog::Level::Trace);
    log.SetEnabled(true);
    ASWLog::TASWLogRecord forced;
    forced.LogLevel = ASWLog::Level::Critical;
    forced.Forced = true;

    // Act & Assert
    for (const auto level : { ASWLog::Level::Trace, ASWLog::Level::Info, ASWLog::Level::Critical })
        CheckFalse(log.ShouldLog(level), std::format("ShouldLog({}) should be false", ASWLog::Level_ToString(level)));

    CheckFalse(log.ShouldLog(forced), "ShouldLog() should be false for a forced record");
    CheckFalse(log.IsEnabled(), "IsEnabled() should stay false after SetEnabled(true)");
    log.Write(forced);
    log.LogCritical("not written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_NullLog::Test_Wrapped_WritesNothing()
{
    // Arrange: a category and a multi-log in front of a null log
    ASWLog::TASWNullLog log;
    ASWLog::TASWCategoryLog netLog("Net", log);
    netLog.SetMinimumLevel(ASWLog::Level::Trace);
    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(log);
    multiLog.SetMinimumLevel(ASWLog::Level::Trace);

    // Act
    netLog.LogInfoFmt("{}", TNullLogCountedValue{});
    multiLog.LogInfoFmt("{}", TNullLogCountedValue{});

    // Assert
    CheckFalse(netLog.ShouldLog(ASWLog::Level::Critical), "A category over a null log should write nothing");
    CheckFalse(multiLog.ShouldLog(ASWLog::Level::Critical), "A multi-log holding only a null log should write nothing");
    CheckEquals(0, NullLogFormatCount, "Neither should format a message for the null log");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_NullLog)
