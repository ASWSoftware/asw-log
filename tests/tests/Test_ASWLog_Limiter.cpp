/* **************************************************************************
Test_ASWLog_Limiter.cpp
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
#include "Test_ASWLog_Limiter.h"
//---------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <source_location>
#include <string>
#include <thread>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_Limiter.h"
#include "ASWLog_MemoryLog.h"
//---------------------------------------------------------------------------

namespace
{

// A value whose formatting is counted in LimiterFormatCount, to check whether a macro formats its arguments
struct TLimiterCountedValue
{
};

int LimiterFormatCount = 0;

} // namespace

template<>
struct std::formatter<TLimiterCountedValue>
{
    constexpr std::format_parse_context::iterator parse(std::format_parse_context& context)
    {
        return context.begin();
    }

    std::format_context::iterator format(const TLimiterCountedValue& /*value*/, std::format_context& context) const
    {
        ++LimiterFormatCount;
        return std::format_to(context.out(), "counted");
    }
};

namespace ASWUnitTests
{

namespace
{

using namespace std::chrono_literals;

// A config with no startup or shutdown lines and only the level and the fields in each line, e.g. "[INFO]: message"
ASWLog::TASWLogConfig MakeLimiterConfig()
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

// Each macro's call site, which keeps its limiter for the whole process
void LogEveryFourth(ASWLog::IASWLog& log, ASWLog::Level level, int value)
{
    ASWLOG_EVERY_N(log, 4, level, "Item {}", value);
}

void LogOnce(ASWLog::IASWLog& log, ASWLog::Level level)
{
    ASWLOG_ONCE(log, level, "Once {}", TLimiterCountedValue{});
}

void LogRetry(ASWLog::IASWLog& log, int attempt)
{
    ASWLOG_EVERY_INTERVAL(log, 20ms, ASWLog::Level::Warn, { { "attempt", attempt } }, "Retrying");
}

} // namespace

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_Limiter
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_Limiter::TTest_ASWLog_Limiter()
    : inherited("ASWLog_Limiter_Tests")
{
    RegisterTest(&TTest_ASWLog_Limiter::Test_EveryInterval_AllowsOneCallPerInterval, "EveryInterval_AllowsOneCallPerInterval");
    RegisterTest(&TTest_ASWLog_Limiter::Test_EveryInterval_HandlesTheLongestInterval, "EveryInterval_HandlesTheLongestInterval");
    RegisterTest(&TTest_ASWLog_Limiter::Test_EveryInterval_ZeroAllowsEveryCall, "EveryInterval_ZeroAllowsEveryCall");
    RegisterTest(&TTest_ASWLog_Limiter::Test_EveryN_AllowsEveryNthCall, "EveryN_AllowsEveryNthCall");
    RegisterTest(&TTest_ASWLog_Limiter::Test_Macros_EveryIntervalAddsTheSuppressedField, "Macros_EveryIntervalAddsTheSuppressedField");
    RegisterTest(&TTest_ASWLog_Limiter::Test_Macros_EveryNCountsOnlyEntriesThatWouldBeWritten, "Macros_EveryNCountsOnlyEntriesThatWouldBeWritten");
    RegisterTest(&TTest_ASWLog_Limiter::Test_Macros_KeepTheCallersSourceLocation, "Macros_KeepTheCallersSourceLocation");
    RegisterTest(&TTest_ASWLog_Limiter::Test_Macros_OnceLogsTheFirstEntryThatWouldBeWritten, "Macros_OnceLogsTheFirstEntryThatWouldBeWritten");
    RegisterTest(&TTest_ASWLog_Limiter::Test_Once_AllowsOnlyTheFirstCall, "Once_AllowsOnlyTheFirstCall");
    RegisterTest(&TTest_ASWLog_Limiter::Test_Threads_AllowExactlyTheLimitedCalls, "Threads_AllowExactlyTheLimitedCalls");
}
//---------------------------------------------------------------------------
TTest_ASWLog_Limiter::~TTest_ASWLog_Limiter()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Limiter::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Limiter::SetUp_Test(ITestCase& /*testCase*/)
{
    LimiterFormatCount = 0;
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Limiter::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Limiter::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_Limiter::Test_EveryInterval_AllowsOneCallPerInterval()
{
    // Arrange
    auto limiter = ASWLog::TASWLogLimiter::EveryInterval(5s);
    const std::chrono::steady_clock::time_point start(100s);
    std::uint64_t refused[7]{};

    // Act: 0 s allowed; 1, 2 and 4.999 s refused; 5 s allowed (the interval runs from there); 9 s refused; 10 s allowed
    const bool first = limiter.Allow(refused[0], start);
    const bool second = limiter.Allow(refused[1], start + 1s);
    const bool third = limiter.Allow(refused[2], start + 2s);
    const bool justBefore = limiter.Allow(refused[3], start + 4999ms);
    const bool atInterval = limiter.Allow(refused[4], start + 5s);
    const bool afterIt = limiter.Allow(refused[5], start + 9s);
    const bool atNextInterval = limiter.Allow(refused[6], start + 10s);

    // Assert
    CheckTrue(first, "The first call should be allowed");
    CheckFalse(second || third || justBefore, "The calls within the interval should be refused");
    CheckTrue(atInterval, "The first call once the interval has passed should be allowed");
    CheckFalse(afterIt, "The interval should run from the call allowed last");
    CheckTrue(atNextInterval, "The first call once the next interval has passed should be allowed");
    CheckEquals(0, refused[0], "The first call follows no refused ones");
    CheckEquals(0, refused[1] + refused[2] + refused[3] + refused[5], "A refused call should get a count of 0");
    CheckEquals(3, refused[4], "The allowed call should get the count of the calls refused since the one before");
    CheckEquals(1, refused[6], "The count should start again after each allowed call");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Limiter::Test_EveryInterval_HandlesTheLongestInterval()
{
    // Arrange: the time after the allowed call would overflow the clock's ticks
    auto limiter = ASWLog::TASWLogLimiter::EveryInterval(std::chrono::steady_clock::duration::max());
    const std::chrono::steady_clock::time_point start(100s);
    std::uint64_t refused = 0;

    // Act
    const bool first = limiter.Allow(refused, start);
    const bool later = limiter.Allow(refused, start + std::chrono::hours(24 * 365));

    // Assert
    CheckTrue(first, "The first call should be allowed");
    CheckFalse(later, "A call within the (longest) interval should be refused, not allowed by a wrapped time");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Limiter::Test_EveryInterval_ZeroAllowsEveryCall()
{
    // Arrange
    auto zero = ASWLog::TASWLogLimiter::EveryInterval(0s);
    auto negative = ASWLog::TASWLogLimiter::EveryInterval(-1s);
    const std::chrono::steady_clock::time_point now(100s);
    std::uint64_t refused = 0;
    int allowed = 0;

    // Act
    for (int i = 0; i < 3; ++i)
        allowed += (zero.Allow(refused, now) ? 1 : 0) + (negative.Allow(refused, now) ? 1 : 0);

    // Assert
    CheckEquals(6, allowed, "An interval of 0 or less should allow every call");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Limiter::Test_EveryN_AllowsEveryNthCall()
{
    // Arrange
    auto everyThird = ASWLog::TASWLogLimiter::EveryN(3);
    auto everyZeroth = ASWLog::TASWLogLimiter::EveryN(0);
    std::string pattern;
    std::vector<std::uint64_t> refusedCounts;
    int zerothAllowed = 0;

    // Act
    for (int i = 0; i < 8; ++i)
    {
        std::uint64_t refused = 0;
        const bool allowed = everyThird.Allow(refused);
        pattern += allowed ? 'x' : '.';
        if (allowed)
            refusedCounts.push_back(refused);

        zerothAllowed += everyZeroth.Allow() ? 1 : 0;
    }

    // Assert
    CheckEquals(std::string("x..x..x."), pattern, "The 1st, 4th and 7th calls should be allowed");
    AssertEquals(3, refusedCounts.size(), "Three calls should be allowed");
    CheckEquals(0, refusedCounts[0], "The first follows no refused calls");
    CheckEquals(2, refusedCounts[1], "Each later one follows n - 1 refused calls");
    CheckEquals(8, zerothAllowed, "An n of 0 should allow every call");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Limiter::Test_Macros_EveryIntervalAddsTheSuppressedField()
{
    // The call site's limiter lasts for the process
    static int runCount = 0;
    if (runCount++ > 0)
        Skip("The macro's limiter keeps its state for the process, so this test runs once per process");

    // Arrange
    ASWLog::TASWMemoryLog log;
    CheckTrue(log.Initialize(MakeLimiterConfig()), "Initialize should succeed");
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    int calls = 0;

    // Act: call until the interval has passed and a second entry is written (a bounded wait on that)
    while (log.GetLineCount() < 2 && std::chrono::steady_clock::now() < deadline)
        LogRetry(log, calls++);

    // Assert
    const auto lines = log.GetLines();
    AssertEquals(2, lines.size(), "The first call and the first one after the interval should be written");
    CheckGreaterThan(calls, 2, "Calls within the interval should have been skipped");
    CheckEquals(std::string("[WARN][attempt=0]: Retrying"), lines[0], "The first entry follows no skipped calls");
    CheckEquals(std::format("[WARN][suppressed={} attempt={}]: Retrying", calls - 2, calls - 1), lines[1],
        "The next entry should say how many calls were skipped, with the call's own fields");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Limiter::Test_Macros_EveryNCountsOnlyEntriesThatWouldBeWritten()
{
    static int runCount = 0;
    if (runCount++ > 0)
        Skip("The macro's limiter keeps its state for the process, so this test runs once per process");

    // Arrange
    ASWLog::TASWMemoryLog log;
    CheckTrue(log.Initialize(MakeLimiterConfig()), "Initialize should succeed");

    // Act: Debug calls are filtered out (Info is the minimum level), so they don't count
    for (int i = 1; i <= 10; ++i)
    {
        LogEveryFourth(log, ASWLog::Level::Debug, -i);
        LogEveryFourth(log, ASWLog::Level::Info, i);
    }

    // Assert
    const auto lines = log.GetLines();
    AssertEquals(3, lines.size(), "Every 4th written entry of 10 should be written");
    CheckEquals(std::string("[INFO]: Item 1"), lines[0], "The first entry should be written");
    CheckEquals(std::string("[INFO]: Item 5"), lines[1], "Then the 5th");
    CheckEquals(std::string("[INFO]: Item 9"), lines[2], "Then the 9th");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Limiter::Test_Macros_KeepTheCallersSourceLocation()
{
    // Arrange
    ASWLog::TASWMemoryLog log;
    auto config = MakeLimiterConfig();
    std::optional<std::source_location> location;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Trace;
    config.OnLogEntry = [&location](const ASWLog::TASWLogRecord& record, std::string_view /*formattedLine*/) {
            location = record.Location;
        };
    CheckTrue(log.Initialize(config), "Initialize should succeed");

    // Act
    const auto expectedLine = static_cast<std::uint_least32_t>(__LINE__ + 1);
    ASWLOG_EVERY_N(log, 1, ASWLog::Level::Info, "located");

    // Assert
    AssertTrue(location.has_value(), "The entry should be written");
    CheckEquals(expectedLine, location->line(), "The entry should have the macro's line");
    CheckContains(std::string(location->function_name()), "Test_Macros_KeepTheCallersSourceLocation", "The entry should have the calling function");
    CheckEndsWith(std::string(location->file_name()), "Test_ASWLog_Limiter.cpp", "The entry should have the calling file");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Limiter::Test_Macros_OnceLogsTheFirstEntryThatWouldBeWritten()
{
    static int runCount = 0;
    if (runCount++ > 0)
        Skip("The macro's limiter keeps its state for the process, so this test runs once per process");

    // Arrange
    ASWLog::TASWMemoryLog log;
    CheckTrue(log.Initialize(MakeLimiterConfig()), "Initialize should succeed");

    // Act: filtered calls first (Debug, with Info the minimum level), then the level lets Debug through
    LogOnce(log, ASWLog::Level::Debug);
    LogOnce(log, ASWLog::Level::Debug);
    const auto filteredLineCount = log.GetLineCount();
    log.SetMinimumLevel(ASWLog::Level::Debug);
    for (int i = 0; i < 3; ++i)
        LogOnce(log, ASWLog::Level::Debug);

    // Assert
    CheckEquals(0, filteredLineCount, "Filtered calls should write nothing");
    const auto lines = log.GetLines();
    AssertEquals(1, lines.size(), "Only the first entry that would be written should be written");
    CheckEquals(std::string("[DEBUG]: Once counted"), lines[0], "It should be the formatted entry");
    CheckEquals(1, LimiterFormatCount, "Only the written entry's arguments should be formatted");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Limiter::Test_Once_AllowsOnlyTheFirstCall()
{
    // Arrange
    auto limiter = ASWLog::TASWLogLimiter::Once();
    std::uint64_t refused = 99;

    // Act
    const bool first = limiter.Allow(refused);
    const auto firstRefused = refused;
    const bool second = limiter.Allow(refused);
    const bool third = limiter.Allow();

    // Assert
    CheckTrue(first, "The first call should be allowed");
    CheckEquals(0, firstRefused, "The first call follows no refused ones");
    CheckFalse(second || third, "Later calls should be refused");
    CheckEquals(0, refused, "A refused call should get a count of 0");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Limiter::Test_Threads_AllowExactlyTheLimitedCalls()
{
    // Arrange: threads sharing each kind of limiter (also for ThreadSanitizer), EveryInterval at one fixed time
    constexpr int ThreadCount = 8;
    constexpr int CallsPerThread = 1000;
    auto once = ASWLog::TASWLogLimiter::Once();
    auto everySeventh = ASWLog::TASWLogLimiter::EveryN(7);
    auto everyHour = ASWLog::TASWLogLimiter::EveryInterval(1h);
    const std::chrono::steady_clock::time_point now(100s);
    std::atomic<int> onceAllowed{ 0 };
    std::atomic<int> everySeventhAllowed{ 0 };
    std::atomic<int> everyHourAllowed{ 0 };
    std::atomic<std::uint64_t> everyHourRefused{ 0 };
    std::vector<std::thread> threads;

    // Act
    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&] {
                std::uint64_t refused = 0;
                for (int i = 0; i < CallsPerThread; ++i)
                {
                    onceAllowed += once.Allow(refused, now) ? 1 : 0;
                    everySeventhAllowed += everySeventh.Allow(refused, now) ? 1 : 0;
                    if (everyHour.Allow(refused, now))
                    {
                        ++everyHourAllowed;
                        everyHourRefused += refused;
                    }
                }
            });
    }

    for (auto& thread : threads)
        thread.join();

    std::uint64_t nextHourRefused = 0;
    const bool nextHour = everyHour.Allow(nextHourRefused, now + 1h);

    // Assert: a call refused while another was allowed may be counted with either allowed call, but each just once
    CheckEquals(1, onceAllowed.load(), "Once should allow exactly one call");
    CheckEquals((ThreadCount * CallsPerThread + 6) / 7, everySeventhAllowed.load(), "EveryN should allow exactly every 7th call");
    CheckEquals(1, everyHourAllowed.load(), "EveryInterval should allow exactly one call at one time");
    CheckTrue(nextHour, "A call an interval later should be allowed");
    CheckEquals(ThreadCount * CallsPerThread - 1, everyHourRefused.load() + nextHourRefused, "Every refused call should be counted once");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_Limiter)
