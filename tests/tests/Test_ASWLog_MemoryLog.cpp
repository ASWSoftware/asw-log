/* **************************************************************************
Test_ASWLog_MemoryLog.cpp
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
#include "Test_ASWLog_MemoryLog.h"
//---------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <set>
#include <string>
#include <thread>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_MemoryLog.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

// How long a test waits for something another thread does, before it fails
constexpr std::chrono::milliseconds WaitTimeout = std::chrono::seconds(5);

//---------------------------------------------------------------------------

// A config with no startup or shutdown lines and only the level in each line, e.g. "[INFO]: message", keeping at most
// 'maxLines' lines and 'maxBytes' bytes
ASWLog::TASWLogConfig MakeMemoryConfig(std::size_t maxLines = 1000, std::size_t maxBytes = 0)
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
    config.Memory.MaxLines = maxLines;
    config.Memory.MaxBytes = maxBytes;
    return config;
}

} // namespace

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_MemoryLog
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_MemoryLog::TTest_ASWLog_MemoryLog()
    : inherited("ASWLog_MemoryLog_Tests")
{
    RegisterTest(&TTest_ASWLog_MemoryLog::Test_Async_KeepsTheQueuedLines, "Async_KeepsTheQueuedLines");
    RegisterTest(&TTest_ASWLog_MemoryLog::Test_Clear_ForgetsTheLinesButNotTheSequence, "Clear_ForgetsTheLinesButNotTheSequence");
    RegisterTest(&TTest_ASWLog_MemoryLog::Test_Close_KeepsTheLines, "Close_KeepsTheLines");
    RegisterTest(&TTest_ASWLog_MemoryLog::Test_GetLinesSince_ReturnsNewLinesAndCountsMissedOnes, "GetLinesSince_ReturnsNewLinesAndCountsMissedOnes");
    RegisterTest(&TTest_ASWLog_MemoryLog::Test_KeepsLinesWithoutTheirEnding, "KeepsLinesWithoutTheirEnding");
    RegisterTest(&TTest_ASWLog_MemoryLog::Test_MaxBytes_CutsALineThatAloneIsTooLong, "MaxBytes_CutsALineThatAloneIsTooLong");
    RegisterTest(&TTest_ASWLog_MemoryLog::Test_MaxBytes_DropsTheOldestLines, "MaxBytes_DropsTheOldestLines");
    RegisterTest(&TTest_ASWLog_MemoryLog::Test_MaxLines_KeepsTheNewest, "MaxLines_KeepsTheNewest");
    RegisterTest(&TTest_ASWLog_MemoryLog::Test_Reconfigure_AppliesTheNewLimits, "Reconfigure_AppliesTheNewLimits");
    RegisterTest(&TTest_ASWLog_MemoryLog::Test_Threads_ReaderGetsEveryLineOnce, "Threads_ReaderGetsEveryLineOnce");
}
//---------------------------------------------------------------------------
TTest_ASWLog_MemoryLog::~TTest_ASWLog_MemoryLog()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MemoryLog::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MemoryLog::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MemoryLog::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MemoryLog::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_MemoryLog::Test_Async_KeepsTheQueuedLines()
{
    // Arrange
    auto config = MakeMemoryConfig();
    config.Async.Enabled = true;
    ASWLog::TASWMemoryLog log;
    CheckTrue(log.Initialize(config), "Initialize should succeed");

    // Act
    log.LogInfo("one");
    log.LogWarn("two");
    log.Flush(); // Waits for the queued entries

    // Assert
    CheckTrue(log.GetLines() == std::vector<std::string>{ "[INFO]: one", "[WARN]: two" }, "The queued lines should be kept in order");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MemoryLog::Test_Clear_ForgetsTheLinesButNotTheSequence()
{
    // Arrange
    ASWLog::TASWMemoryLog log;
    CheckTrue(log.Initialize(MakeMemoryConfig()), "Initialize should succeed");
    log.LogInfo("one");
    log.LogInfo("two");

    // Act
    log.Clear();
    const auto countAfterClear = log.GetLineCount();
    const auto afterClear = log.GetLinesSince(1);
    log.LogInfo("three");
    const auto afterNewLine = log.GetLinesSince(afterClear.NextSequence);

    // Assert
    CheckEquals(0, countAfterClear, "Clear() should forget the lines");
    CheckEmpty(afterClear.Lines, "No line should be left after Clear()");
    CheckEquals(2, afterClear.MissedCount, "The cleared lines should count as missed");
    CheckEquals(3, afterClear.NextSequence, "The sequence should go on after Clear()");
    CheckTrue(afterNewLine.Lines == std::vector<std::string>{ "[INFO]: three" }, "A new line should follow the cleared ones");
    CheckEquals(0, afterNewLine.MissedCount, "Nothing should be missed after the cleared lines");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MemoryLog::Test_Close_KeepsTheLines()
{
    // Arrange
    ASWLog::TASWMemoryLog log;
    CheckTrue(log.Initialize(MakeMemoryConfig()), "Initialize should succeed");
    log.LogInfo("kept");

    // Act
    const bool closed = log.Close();
    log.LogInfo("dropped");

    // Assert
    CheckTrue(closed, "Close() should succeed");
    CheckFalse(log.IsOpen(), "The logger should be closed");
    CheckFalse(log.Flush(), "Flush() should fail while closed");
    CheckTrue(log.GetLines() == std::vector<std::string>{ "[INFO]: kept" }, "The lines should be kept after Close(), and no more added");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MemoryLog::Test_GetLinesSince_ReturnsNewLinesAndCountsMissedOnes()
{
    // Arrange: lines 1 to 3 kept, then 4 and 5, which push 1 and 2 out
    ASWLog::TASWMemoryLog log;
    CheckTrue(log.Initialize(MakeMemoryConfig(3)), "Initialize should succeed");
    for (int index = 1; index <= 3; ++index)
        log.LogInfo(std::format("line {}", index));

    // Act
    const auto all = log.GetLinesSince(1);
    const auto fromZero = log.GetLinesSince(0);
    const auto last = log.GetLinesSince(3);
    const auto none = log.GetLinesSince(4);
    log.LogInfo("line 4");
    log.LogInfo("line 5");
    const auto afterDrops = log.GetLinesSince(1);
    const auto ahead = log.GetLinesSince(10);

    // Assert
    const std::vector<std::string> firstThree{ "[INFO]: line 1", "[INFO]: line 2", "[INFO]: line 3" };
    CheckTrue(all.Lines == firstThree, "Every kept line should be returned from 1");
    CheckEquals(4, all.NextSequence, "The next sequence number should follow the last line");
    CheckEquals(0, all.MissedCount, "Nothing should be missed yet");
    CheckTrue(fromZero.Lines == firstThree, "0 should read like 1, the first line's number");
    CheckEquals(0, fromZero.MissedCount, "0 shouldn't count a line that never existed");
    CheckTrue(last.Lines == std::vector<std::string>{ "[INFO]: line 3" }, "Only the lines from the given number should be returned");
    CheckEmpty(none.Lines, "No line should be returned when there is none newer");
    CheckEquals(4, none.NextSequence, "The next sequence number should stay when there is no new line");
    CheckTrue(afterDrops.Lines == std::vector<std::string>{ "[INFO]: line 3", "[INFO]: line 4", "[INFO]: line 5" },
        "The lines still kept should be returned");
    CheckEquals(2, afterDrops.MissedCount, "The dropped lines should count as missed");
    CheckEquals(6, afterDrops.NextSequence, "The next sequence number should follow line 5");
    CheckEmpty(ahead.Lines, "A number past the last line should return nothing");
    CheckEquals(0, ahead.MissedCount, "A number past the last line should miss nothing");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MemoryLog::Test_KeepsLinesWithoutTheirEnding()
{
    // Arrange: LF, and CRLF with indented continuation lines
    ASWLog::TASWMemoryLog log;
    ASWLog::TASWMemoryLog crlfLog;
    auto crlfConfig = MakeMemoryConfig();
    crlfConfig.Line.Ending = ASWLog::LineEnding::CRLF;
    crlfConfig.Line.Multiline = ASWLog::MultilineMode::Indent;
    CheckTrue(log.Initialize(MakeMemoryConfig()), "Initialize should succeed");
    CheckTrue(crlfLog.Initialize(crlfConfig), "Initialize should succeed (CRLF)");

    // Act
    log.LogInfo("plain");
    log.LogInfo("ends with a break\n");
    log.LogRaw(ASWLog::Level::Info, "raw\n");
    log.LogRaw(ASWLog::Level::Info, "partial");
    crlfLog.LogInfo("first\nsecond");
    crlfLog.LogRaw(ASWLog::Level::Info, "raw\r\n");

    // Assert
    const std::vector<std::string> expected{ "[INFO]: plain", "[INFO]: ends with a break\n", "raw\n", "partial" };
    CheckTrue(log.GetLines() == expected, "Formatted lines should lose only their line ending; raw ones should be kept as given");
    CheckTrue(crlfLog.GetLines() == std::vector<std::string>{ "[INFO]: first\r\n    | second", "raw\r\n" },
        "A CRLF ending should be left off, and the line breaks inside the line kept");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MemoryLog::Test_MaxBytes_CutsALineThatAloneIsTooLong()
{
    // Arrange: 20 bytes, so the line "[INFO]: abcde" + a 2-byte UTF-8 character + more is cut inside that character;
    // and 4 bytes, too few for the marker
    ASWLog::TASWMemoryLog log;
    ASWLog::TASWMemoryLog tinyLog;
    CheckTrue(log.Initialize(MakeMemoryConfig(1000, 20)), "Initialize should succeed");
    CheckTrue(tinyLog.Initialize(MakeMemoryConfig(1000, 4)), "Initialize should succeed (tiny)");
    log.LogInfo("first");

    // Act
    log.LogInfo("abcde\xCE\xBBxxxxxxxxxxxxxxxxxxxx");
    tinyLog.LogInfo("hello");

    // Assert
    CheckTrue(log.GetLines() == std::vector<std::string>{ "[INFO]: abcde [cut]" },
        "The line should be cut between UTF-8 characters, marked, and the older line dropped");
    CheckTrue(tinyLog.GetLines() == std::vector<std::string>{ "[INF" }, "Without room for the marker, the line should only be cut");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MemoryLog::Test_MaxBytes_DropsTheOldestLines()
{
    // Arrange: 30 bytes, two 15-byte lines
    ASWLog::TASWMemoryLog log;
    CheckTrue(log.Initialize(MakeMemoryConfig(1000, 30)), "Initialize should succeed");

    // Act
    log.LogInfo("1111111");
    log.LogInfo("2222222");
    const auto atTheLimit = log.GetLines();
    log.LogInfo("3333333");

    // Assert
    CheckTrue(atTheLimit == std::vector<std::string>{ "[INFO]: 1111111", "[INFO]: 2222222" }, "Lines that fit exactly should all be kept");
    CheckTrue(log.GetLines() == std::vector<std::string>{ "[INFO]: 2222222", "[INFO]: 3333333" }, "The oldest line should go to make room");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MemoryLog::Test_MaxLines_KeepsTheNewest()
{
    // Arrange
    ASWLog::TASWMemoryLog log;
    CheckTrue(log.Initialize(MakeMemoryConfig(3)), "Initialize should succeed");

    // Act
    for (int index = 1; index <= 5; ++index)
        log.LogInfo(std::format("line {}", index));

    // Assert
    CheckEquals(3, log.GetLineCount(), "At most MaxLines lines should be kept");
    CheckTrue(log.GetLines() == std::vector<std::string>{ "[INFO]: line 3", "[INFO]: line 4", "[INFO]: line 5" },
        "The newest lines should be kept, oldest first");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MemoryLog::Test_Reconfigure_AppliesTheNewLimits()
{
    // Arrange
    ASWLog::TASWMemoryLog log;
    CheckTrue(log.Initialize(MakeMemoryConfig(10)), "Initialize should succeed");
    for (int index = 1; index <= 5; ++index)
        log.LogInfo(std::format("line {}", index));

    // Act
    const bool fewerLines = log.Reconfigure(MakeMemoryConfig(3));
    const auto afterFewerLines = log.GetLines();
    const bool fewerBytes = log.Reconfigure(MakeMemoryConfig(3, 30));
    const auto afterFewerBytes = log.GetLines();
    const bool unlimited = log.Reconfigure(MakeMemoryConfig(0, 0));
    for (int index = 6; index <= 9; ++index)
        log.LogInfo(std::format("line {}", index));

    // Assert
    CheckTrue(fewerLines && fewerBytes && unlimited, "Reconfigure should succeed");
    CheckTrue(afterFewerLines == std::vector<std::string>{ "[INFO]: line 3", "[INFO]: line 4", "[INFO]: line 5" },
        "A smaller MaxLines should keep the newest lines");
    CheckTrue(afterFewerBytes == std::vector<std::string>{ "[INFO]: line 4", "[INFO]: line 5" }, "A smaller MaxBytes should keep the newest lines");
    CheckEquals(6, log.GetLineCount(), "With no limits, every new line should be kept");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_MemoryLog::Test_Threads_ReaderGetsEveryLineOnce()
{
    // Arrange: four threads log while another reads the new lines as they come (for ThreadSanitizer)
    constexpr int ThreadCount = 4;
    constexpr int EntryCount = 250;
    constexpr std::size_t Total = ThreadCount * EntryCount;
    ASWLog::TASWMemoryLog log;
    CheckTrue(log.Initialize(MakeMemoryConfig(0)), "Initialize should succeed");
    std::set<std::string> seen;
    std::uint64_t missedCount = 0;
    std::size_t readCount = 0;

    // Act
    std::thread reader([&] {
        std::uint64_t next = 1;
        const auto deadline = std::chrono::steady_clock::now() + WaitTimeout;
        while (readCount < Total && std::chrono::steady_clock::now() < deadline)
        {
            const auto newLines = log.GetLinesSince(next);
            missedCount += newLines.MissedCount;
            readCount += newLines.Lines.size();
            seen.insert(newLines.Lines.begin(), newLines.Lines.end());
            next = newLines.NextSequence;
            std::this_thread::yield();
        }
            });

    std::vector<std::thread> writers;
    for (int threadIndex = 0; threadIndex < ThreadCount; ++threadIndex)
    {
        writers.emplace_back([&log, threadIndex] {
                for (int index = 0; index < EntryCount; ++index)
                    log.LogInfo(std::format("{} {}", threadIndex, index));
            });
    }

    for (auto& writer : writers)
        writer.join();

    reader.join();

    // Assert
    CheckEquals(Total, readCount, "The reader should get every line");
    CheckEquals(Total, seen.size(), "The reader should get each line once");
    CheckEquals(0, missedCount, "Nothing should be missed without limits");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_MemoryLog)
