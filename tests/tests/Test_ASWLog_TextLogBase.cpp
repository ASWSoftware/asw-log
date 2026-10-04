/* **************************************************************************
Test_ASWLog_TextLogBase.cpp
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
#include "Test_ASWLog_TextLogBase.h"
//---------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_FileLog.h"
#include "ASWLog_Formatter.h"
#include "ASWLog_TextLogBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

const auto GroupBaseTempDir = std::filesystem::temp_directory_path() / "aswlog_textlogbase_tests";
const auto TestTempDir = GroupBaseTempDir / "test";

// What a TMemoryTextLog wrote. Outlives the logger, so a test can check what its destructor wrote.
struct TMemoryOutput
{
    std::vector<std::string> Lines;
    std::vector<bool> EndsLine;
    int AfterEntryCount = 0;
    int FlushCount = 0;
};

// A text logger built only from the TASWTextLogBase hooks, writing to memory
class TMemoryTextLog final : public ASWLog::TASWTextLogBase
{
private:
    TMemoryOutput& m_Output;

public:
    bool IsReady = true; // Result of EnsureReadyUnlocked() while initialized and open
    bool AcceptsWrites = true; // Result of PrepareWriteUnlocked()
    bool FlushResult = true; // Result of FlushUnlocked()
    bool ReconfigureResult = true; // Result of ReconfigureUnlocked()
    bool ReportsOnWrite = false; // WriteLineUnlocked() reports a WriteFailed failure, but writes the line
    bool ThrowsOnFlush = false;
    bool ThrowsOnWrite = false;
    // What ReconfigureUnlocked() saw: the banner of the config it replaced, and of the config stored by then
    std::vector<std::string> ReconfiguredFromBanners;
    std::vector<std::string> ReconfiguredToBanners;

protected:
    void AfterEntryUnlocked() override
    {
        ++m_Output.AfterEntryCount;
    }

    bool CloseUnlocked() override
    {
        m_IsOpen.store(false);
        m_IsInitialized.store(false);
        return true;
    }

    bool EnsureReadyUnlocked() override
    {
        return IsReady && TASWTextLogBase::EnsureReadyUnlocked();
    }

    bool FlushUnlocked() override
    {
        if (ThrowsOnFlush)
            throw std::runtime_error("flush failed");

        ++m_Output.FlushCount;
        return FlushResult;
    }

    std::string_view GetLoggerClassName() const noexcept override
    {
        return "TMemoryTextLog";
    }

    bool InitializeUnlocked() override
    {
        return OpenUnlocked();
    }

    bool OpenUnlocked() override
    {
        m_IsOpen.store(true);
        m_IsInitialized.store(true);
        return true;
    }

    bool PrepareWriteUnlocked(std::chrono::system_clock::time_point /*now*/) override
    {
        return AcceptsWrites;
    }

    bool ReconfigureUnlocked(const ASWLog::TASWLogConfig& previous) override
    {
        ReconfiguredFromBanners.push_back(previous.Startup.Banner);
        ReconfiguredToBanners.push_back(GetConfigUnlocked().Startup.Banner);
        return ReconfigureResult;
    }

    void WriteLineUnlocked(ASWLog::Level /*level*/, std::string_view line, bool endsLine) override
    {
        if (ThrowsOnWrite)
            throw std::runtime_error("write failed");

        if (ReportsOnWrite)
        {
            ASWLog::TASWLogError error;
            error.Kind = ASWLog::ErrorKind::WriteFailed;
            error.Message = "memory write failed";
            ReportErrorUnlocked(std::move(error));
        }

        m_Output.Lines.emplace_back(line);
        m_Output.EndsLine.push_back(endsLine);
    }

public:
    explicit TMemoryTextLog(TMemoryOutput& output)
        : m_Output(output)
    {
    }

    // True if m_Mutex isn't held, checked from another thread (try_lock on a mutex the calling thread holds is
    // undefined)
    bool IsMutexFree()
    {
        bool isFree = false;
        std::thread checker([this, &isFree] {
            if (m_Mutex.try_lock())
            {
                isFree = true;
                m_Mutex.unlock();
            }
                    });
        checker.join();
        return isFree;
    }

    ~TMemoryTextLog() override
    {
        Finalize();
    }
};

// A user's own line layout, "LEVEL|message"
class TPipeFormatter final : public ASWLog::IASWLogFormatter
{
public:
    std::string Format(const ASWLog::TASWLogRecord& record, const ASWLog::TASWLogConfig& /*config*/) const override
    {
        return std::string(ASWLog::Level_ToString(record.LogLevel)).append("|").append(record.Message);
    }
};

// A faulty user formatter: throws for every line (IASWLogFormatter::Format() may throw; the line is then dropped)
class TThrowingFormatter final : public ASWLog::IASWLogFormatter
{
public:
    std::string Format(const ASWLog::TASWLogRecord& /*record*/, const ASWLog::TASWLogConfig& /*config*/) const override
    {
        throw std::runtime_error("format failed");
    }
};

// A config with no startup or shutdown lines, and only the level in each line, e.g. "[INFO]: message\n"
ASWLog::TASWLogConfig MakeQuietConfig()
{
    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = "textlogbase.log";
    config.InitialMinimumLevel = ASWLog::Level::Info;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = true;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Line.ShowFunctionName = false;
    config.Line.ShowSourceLine = false;
    config.File.OpenRetryCount = 1;
    config.Shutdown.WriteLine = false;
    config.Startup.WriteTimeInfo = false;
    config.Startup.WriteOSInfo = false;
    config.Startup.WriteDriveInfo = false;
    config.Startup.WriteSystemMemoryInfo = false;
    config.Startup.WriteApplicationInfo = false;
    config.Startup.WriteMemoryUsage = false;
    return config;
}

std::string ReadFileText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};

    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

} // namespace

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_TextLogBase
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_TextLogBase::TTest_ASWLog_TextLogBase()
    : inherited("ASWLog_TextLogBase_Tests")
{
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Finalize_WritesShutdownLineFromDestructor, "Finalize_WritesShutdownLineFromDestructor");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Flush_CallsHookAndReturnsItsResult, "Flush_CallsHookAndReturnsItsResult");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Flush_ThrowingHookDoesNotEscape, "Flush_ThrowingHookDoesNotEscape");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Formatter_FormatsEveryFileLine, "Formatter_FormatsEveryFileLine");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Initialize_ThrowingFormatterStillInitializes, "Initialize_ThrowingFormatterStillInitializes");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Initialize_WritesStartupLinesThenCallsAfterEntry, "Initialize_WritesStartupLinesThenCallsAfterEntry");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Log_AtLevelOffIsNeverWritten, "Log_AtLevelOffIsNeverWritten");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Log_DroppedWhenNotReadyOrNotPrepared, "Log_DroppedWhenNotReadyOrNotPrepared");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Log_FormatsFiltersAndCallsAfterEntry, "Log_FormatsFiltersAndCallsAfterEntry");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Log_MinimumLevelOffAllowsOnlyForcedEntries, "Log_MinimumLevelOffAllowsOnlyForcedEntries");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Log_ThrowingWriteDoesNotEscape, "Log_ThrowingWriteDoesNotEscape");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_LogRaw_WritesMessageAsIs, "LogRaw_WritesMessageAsIs");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_OnLogEntry_CallbackCanReconfigureTheLogger, "OnLogEntry_CallbackCanReconfigureTheLogger");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Reconfigure_AppliesNewConfigButKeepsLevel, "Reconfigure_AppliesNewConfigButKeepsLevel");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Reconfigure_IsSafeWhileOtherThreadsLog, "Reconfigure_IsSafeWhileOtherThreadsLog");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_ReportErrorUnlocked_ReportsAfterTheLockIsReleased, "ReportErrorUnlocked_ReportsAfterTheLockIsReleased");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_SetEnabled_FalseWritesNothing, "SetEnabled_FalseWritesNothing");
}
//---------------------------------------------------------------------------
TTest_ASWLog_TextLogBase::~TTest_ASWLog_TextLogBase()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::SetUp_Group()
{
    Log("Setting up temp group folder: " + GroupBaseTempDir.string());
    std::filesystem::create_directories(GroupBaseTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::SetUp_Test(ITestCase& testCase)
{
    Log("  Setting up temp folder for " + testCase.GetName() + ": " + TestTempDir.string());
    std::filesystem::create_directories(TestTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::TearDown_Group()
{
    Log("Cleaning up temp group folder:" + GroupBaseTempDir.string());
    std::filesystem::remove_all(GroupBaseTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::TearDown_Test(ITestCase& testCase)
{
    Log("  Cleaning up temp folder for " + testCase.GetName() + ": " + TestTempDir.string());
    std::filesystem::remove_all(TestTempDir);
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Finalize_WritesShutdownLineFromDestructor()
{
    // Arrange
    TMemoryOutput output;
    auto config = MakeQuietConfig();
    config.Shutdown.WriteLine = true;
    config.Shutdown.Banner = "goodbye";

    // Act
    {
        TMemoryTextLog log(output);
        CheckTrue(log.Initialize(config), __func__, __LINE__, "Initialize should succeed");
    }

    // Assert
    CheckEquals(static_cast<std::size_t>(1), output.Lines.size(), __func__, __LINE__, "The destructor should write one shutdown line");
    if (output.Lines.size() == 1)
    {
        const auto& line = output.Lines[0];
        CheckTrue(line.starts_with("[INFO]: Logger shutdown: "), __func__, __LINE__, "The shutdown line should be formatted: " + line);
        CheckTrue(line.ends_with(", goodbye\n"), __func__, __LINE__, "The shutdown line should end with the shutdown banner: " + line);
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Flush_CallsHookAndReturnsItsResult()
{
    // Arrange: called through the interface, as generic code would
    TMemoryOutput output;
    TMemoryTextLog log(output);
    ASWLog::IASWLog& logger = log;
    CheckTrue(logger.Initialize(MakeQuietConfig()), __func__, __LINE__, "Initialize should succeed");

    // Act
    const bool flushed = logger.Flush();

    log.FlushResult = false;
    const bool failedFlush = logger.Flush();

    log.FlushResult = true;
    logger.SetEnabled(false);
    const bool flushedWhileDisabled = logger.Flush();

    // Assert
    CheckTrue(flushed, __func__, __LINE__, "Flush() should return true when FlushUnlocked() succeeds");
    CheckTrue(!failedFlush, __func__, __LINE__, "Flush() should return false when FlushUnlocked() fails");
    CheckTrue(flushedWhileDisabled, __func__, __LINE__, "Flush() should still flush while the logger is disabled");
    CheckEquals(3, output.FlushCount, __func__, __LINE__, "Each Flush() should call FlushUnlocked() once");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Flush_ThrowingHookDoesNotEscape()
{
    // Arrange
    TMemoryOutput output;
    TMemoryTextLog log(output);
    CheckTrue(log.Initialize(MakeQuietConfig()), __func__, __LINE__, "Initialize should succeed");
    log.ThrowsOnFlush = true;
    static_assert(noexcept(log.Flush()), "Flush() must be noexcept");

    // Act
    const bool flushed = log.Flush();
    log.ThrowsOnFlush = false;
    log.LogInfo("after_failed_flush");

    // Assert
    CheckTrue(!flushed, __func__, __LINE__, "A throwing FlushUnlocked() should make Flush() return false, not throw");
    CheckEquals(static_cast<std::size_t>(1), output.Lines.size(), __func__, __LINE__, "Logging should continue after a failed flush (the lock is released)");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Formatter_FormatsEveryFileLine()
{
    // Arrange
    const auto logPath = TestTempDir / "textlogbase.log";
    std::vector<std::string> callbackLines;
    auto config = MakeQuietConfig();
    config.Line.Formatter = std::make_shared<TPipeFormatter>();
    config.Startup.Banner = "start";
    config.Shutdown.WriteLine = true;
    config.Shutdown.Banner = "bye";
    config.OnLogEntryMinimumLevel = ASWLog::Level::Info;
    config.OnLogEntry = [&callbackLines](const ASWLog::TASWLogRecord& /*record*/, std::string_view line) {
            callbackLines.emplace_back(line);
        };

    // Act
    {
        ASWLog::TASWFileLog log;
        CheckTrue(log.Initialize(config), __func__, __LINE__, "Initialize should succeed");
        log.LogInfo("hello");
        log.LogWarn("careful");
        log.LogRaw(ASWLog::Level::Info, "raw text\n");
    }

    const auto contents = ReadFileText(logPath);

    // Assert
    const std::string expectedStart = "INFO|start\nINFO|hello\nWARN|careful\nraw text\nINFO|Logger shutdown: ";
    CheckTrue(contents.starts_with(expectedStart), __func__, __LINE__,
        "Every formatted line, including the startup and shutdown lines, should use the formatter; raw text should not: " + contents);
    CheckTrue(contents.ends_with(", bye\n"), __func__, __LINE__, "The shutdown line should end with the shutdown banner: " + contents);
    CheckEquals(static_cast<std::size_t>(3), callbackLines.size(), __func__, __LINE__, "OnLogEntry should get the three logged entries");
    if (callbackLines.size() == 3)
        CheckEquals(std::string("INFO|hello\n"), callbackLines[0], __func__, __LINE__, "OnLogEntry should get the line in the formatter's layout");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Initialize_ThrowingFormatterStillInitializes()
{
    // Arrange: a startup line goes through a formatter that throws
    TMemoryOutput output;
    auto config = MakeQuietConfig();
    config.Startup.Banner = "banner";
    config.Line.Formatter = std::make_shared<TThrowingFormatter>();
    std::vector<std::string> reportedMessages;
    config.ErrorReportInterval = std::chrono::milliseconds(0);
    config.OnError = [&reportedMessages](const ASWLog::TASWLogError& error) {
            reportedMessages.push_back(error.Message);
        };

    TMemoryTextLog log(output);
    bool initialized = false;
    bool threw = false;

    // Act
    try
    {
        initialized = log.Initialize(config);
        log.LogInfo("formatted"); // Dropped, since its line can't be formatted
        log.LogRaw(ASWLog::Level::Info, "raw"); // Written: raw entries don't use the formatter
    }
    catch (...)
    {
        threw = true;
    }

    // Assert
    CheckFalse(threw, __func__, __LINE__, "A throwing formatter should not throw out of Initialize() or the Log* methods");
    CheckTrue(initialized, __func__, __LINE__, "Initialize() should succeed: the startup lines are best effort once the output is open");
    CheckTrue(log.IsOpen(), __func__, __LINE__, "The logger should be open");
    CheckTrue(output.Lines == std::vector<std::string>{ "raw" }, __func__, __LINE__, "Only the raw entry should be written");
    CheckEquals(2, output.AfterEntryCount, __func__, __LINE__, "AfterEntryUnlocked should still run after the startup lines, and after the raw entry");
    CheckTrue(reportedMessages == std::vector<std::string>{ "Skipped the startup lines: format failed", "Dropped an entry: format failed" }, __func__, __LINE__,
        "The skipped startup lines and the dropped entry should be reported");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Initialize_WritesStartupLinesThenCallsAfterEntry()
{
    // Arrange
    TMemoryOutput output;
    TMemoryTextLog log(output);
    auto config = MakeQuietConfig();
    config.Startup.Banner = "starting";
    config.Startup.WriteTimeInfo = true;

    // Act
    const bool initialized = log.Initialize(config);
    const bool initializedAgain = log.Initialize(config);

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckFalse(initializedAgain, __func__, __LINE__, "Initialize should fail while the logger is initialized");
    CheckTrue(log.IsOpen(), __func__, __LINE__, "The logger should be open after Initialize");
    CheckEquals(static_cast<std::size_t>(2), output.Lines.size(), __func__, __LINE__, "Initialize should write the banner and the time line");
    if (output.Lines.size() == 2)
    {
        CheckEquals(std::string("[INFO]: starting\n"), output.Lines[0], __func__, __LINE__, "The banner should come first");
        CheckTrue(output.Lines[1].starts_with("[INFO]: Time: "), __func__, __LINE__, "The time line should follow: " + output.Lines[1]);
    }
    CheckEquals(1, output.AfterEntryCount, __func__, __LINE__, "Initialize should call AfterEntryUnlocked once, after the startup lines");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Log_AtLevelOffIsNeverWritten()
{
    // Arrange
    TMemoryOutput output;
    int callbackCount = 0;
    auto config = MakeQuietConfig();
    config.InitialMinimumLevel = ASWLog::Level::Trace;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Trace;
    config.OnLogEntry = [&callbackCount](const ASWLog::TASWLogRecord&, std::string_view) {
            ++callbackCount;
        };

    TMemoryTextLog log(output);
    CheckTrue(log.Initialize(config), __func__, __LINE__, "Initialize should succeed");
    const int afterEntryCountAtStart = output.AfterEntryCount;

    // Act
    log.Log(ASWLog::Level::Off, "off");
    log.LogRaw(ASWLog::Level::Off, "off_raw\n");
    log.LogForce(ASWLog::Level::Off, "off_forced");
    log.LogForceRaw(ASWLog::Level::Off, "off_forced_raw\n");
    log.LogInfoFmt("{}", "off_fmt_check"); // A normal entry after them, to show the logger still works

    // Assert
    CheckTrue(output.Lines == std::vector<std::string>{ "[INFO]: off_fmt_check\n" }, __func__, __LINE__,
        "A message logged at Off should never be written, even when forced");
    CheckEquals(1, callbackCount, __func__, __LINE__, "OnLogEntry should only fire for the written entry");
    CheckEquals(afterEntryCountAtStart + 1, output.AfterEntryCount, __func__, __LINE__,
        "An entry at Off should be dropped before reaching the logger's output");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Log_DroppedWhenNotReadyOrNotPrepared()
{
    // Arrange
    TMemoryOutput output;
    int callbackCount = 0;
    auto config = MakeQuietConfig();
    config.OnLogEntryMinimumLevel = ASWLog::Level::Info;
    config.OnLogEntry = [&callbackCount](const ASWLog::TASWLogRecord&, std::string_view) {
            ++callbackCount;
        };

    TMemoryTextLog log(output);
    log.LogInfo("before_initialize");
    CheckTrue(log.Initialize(config), __func__, __LINE__, "Initialize should succeed");
    const int afterEntryCountAtStart = output.AfterEntryCount;

    // Act
    log.IsReady = false;
    log.LogInfo("not_ready");
    log.LogForce(ASWLog::Level::Info, "not_ready_forced");
    log.IsReady = true;

    log.AcceptsWrites = false;
    log.LogInfo("not_prepared");
    log.AcceptsWrites = true;

    log.Close();
    log.LogInfo("after_close");

    // Assert
    CheckTrue(output.Lines.empty(), __func__, __LINE__, "No entry should be written");
    CheckEquals(0, callbackCount, __func__, __LINE__, "OnLogEntry should not be called for a dropped entry");
    CheckEquals(afterEntryCountAtStart + 1, output.AfterEntryCount, __func__, __LINE__,
        "AfterEntryUnlocked should be skipped when EnsureReadyUnlocked fails, but called when only PrepareWriteUnlocked drops the line");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Log_FormatsFiltersAndCallsAfterEntry()
{
    // Arrange
    TMemoryOutput output;
    std::vector<std::string> callbackLines;
    auto config = MakeQuietConfig();
    config.Line.Ending = ASWLog::LineEnding::CRLF;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Trace;
    config.OnLogEntry = [&callbackLines](const ASWLog::TASWLogRecord&, std::string_view line) {
            callbackLines.emplace_back(line);
        };

    TMemoryTextLog log(output);
    CheckTrue(log.Initialize(config), __func__, __LINE__, "Initialize should succeed");
    const int afterEntryCountAtStart = output.AfterEntryCount;

    // Act
    log.LogDebug("filtered");
    log.LogInfo("info_entry");
    log.LogForce(ASWLog::Level::Debug, "forced_entry");

    // Assert
    const std::vector<std::string> expected = { "[INFO]: info_entry\r\n", "[DEBUG]: forced_entry\r\n" };
    CheckTrue(output.Lines == expected, __func__, __LINE__, "Only the Info entry and the forced Debug entry should be written, formatted");
    CheckTrue(callbackLines == expected, __func__, __LINE__, "OnLogEntry should get each written line");
    CheckTrue(output.EndsLine == std::vector<bool>{ true, true }, __func__, __LINE__, "A formatted entry should end its line");
    CheckEquals(afterEntryCountAtStart + 2, output.AfterEntryCount, __func__, __LINE__, "AfterEntryUnlocked should be called once per entry that reached the logger");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Log_MinimumLevelOffAllowsOnlyForcedEntries()
{
    // Arrange
    TMemoryOutput output;
    int callbackCount = 0;
    auto config = MakeQuietConfig();
    config.InitialMinimumLevel = ASWLog::Level::Off;
    config.Startup.Banner = "startup banner";
    config.Startup.WriteTimeInfo = true;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Off;
    config.OnLogEntry = [&callbackCount](const ASWLog::TASWLogRecord&, std::string_view) {
            ++callbackCount;
        };

    TMemoryTextLog log(output);

    // Act
    const bool initialized = log.Initialize(config);
    const auto linesAfterInitialize = output.Lines.size();
    log.LogCritical("critical");
    log.LogRaw(ASWLog::Level::Critical, "critical_raw\n");
    log.LogCriticalFmt("{}", "critical_fmt");
    log.LogForce(ASWLog::Level::Info, "forced");
    log.LogForceRaw(ASWLog::Level::Trace, "forced_raw\n");

    // Assert
    CheckTrue(initialized, __func__, __LINE__, "Initialize should succeed");
    CheckTrue(log.GetMinimumLevel() == ASWLog::Level::Off, __func__, __LINE__, "InitialMinimumLevel should seed the minimum level");
    CheckEquals(static_cast<std::size_t>(0), linesAfterInitialize, __func__, __LINE__,
        "With the minimum level at Off, Initialize should write no startup lines");
    CheckTrue(output.Lines == std::vector<std::string>{ "[INFO]: forced\n", "forced_raw\n" }, __func__, __LINE__,
        "With the minimum level at Off, only forced entries should be written");
    CheckEquals(0, callbackCount, __func__, __LINE__, "With OnLogEntryMinimumLevel at Off, OnLogEntry should never fire");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Log_ThrowingWriteDoesNotEscape()
{
    // Arrange
    TMemoryOutput output;
    TMemoryTextLog log(output);
    std::vector<std::string> reportedMessages;
    auto config = MakeQuietConfig();
    config.ErrorReportInterval = std::chrono::milliseconds(0);
    config.OnError = [&reportedMessages](const ASWLog::TASWLogError& error) {
            reportedMessages.push_back(error.Message);
        };
    CheckTrue(log.Initialize(config), __func__, __LINE__, "Initialize should succeed");

    // Act
    bool threw = false;
    log.ThrowsOnWrite = true;
    try
    {
        log.LogInfo("write_throws");
        log.LogForceRaw(ASWLog::Level::Error, "write_throws_raw");
    }
    catch (...)
    {
        threw = true;
    }
    log.ThrowsOnWrite = false;
    log.LogInfo("after_throw"); // Would deadlock if the throw had left the mutex locked

    // Assert
    CheckFalse(threw, __func__, __LINE__, "An exception from WriteLineUnlocked should not reach the caller");
    CheckTrue(output.Lines == std::vector<std::string>{ "[INFO]: after_throw\n" }, __func__, __LINE__, "Logging should work after a failed write");
    CheckTrue(reportedMessages == std::vector<std::string>{ "Dropped an entry: write failed", "Dropped an entry: write failed" }, __func__, __LINE__,
        "Each dropped entry should be reported");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_LogRaw_WritesMessageAsIs()
{
    // Arrange
    TMemoryOutput output;
    TMemoryTextLog log(output);
    CheckTrue(log.Initialize(MakeQuietConfig()), __func__, __LINE__, "Initialize should succeed");

    // Act
    log.LogRaw(ASWLog::Level::Debug, "filtered_raw");
    log.LogRaw(ASWLog::Level::Info, "partial");
    log.LogForceRaw(ASWLog::Level::Trace, " line\n");

    // Assert
    CheckTrue(output.Lines == std::vector<std::string>{ "partial", " line\n" }, __func__, __LINE__, "Raw entries should be written as is, without a format or a line ending");
    CheckTrue(output.EndsLine == std::vector<bool>{ false, false }, __func__, __LINE__, "A raw entry doesn't end its line");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_OnLogEntry_CallbackCanReconfigureTheLogger()
{
    // Arrange: a callback that replaces the config it belongs to, e.g. to turn itself off after the first error. The
    // running callback (and the state it captured) must stay alive until it returns, and calling back into the logger
    // must not deadlock.
    TMemoryOutput output;
    TMemoryTextLog log(output);
    std::vector<std::string> calls;
    const auto configWithoutCallback = MakeQuietConfig();
    const std::string callbackState = "callback_state"; // Copied into the callback, so the config owns it

    auto config = MakeQuietConfig();
    config.OnLogEntry = [&log, &calls, &configWithoutCallback, callbackState](const ASWLog::TASWLogRecord&, std::string_view) {
            const bool reconfigured = log.Reconfigure(configWithoutCallback);
            calls.push_back(callbackState + (reconfigured ? ":reconfigured" : ":failed"));
        };
    CheckTrue(log.Initialize(config), __func__, __LINE__, "Initialize should succeed");

    // Act
    log.LogError("first_error");
    log.LogError("second_error");

    // Assert
    CheckTrue(calls == std::vector<std::string>{ "callback_state:reconfigured" }, __func__, __LINE__,
        "The callback should run once, reconfigure the logger, and still have its own state afterwards");
    CheckTrue(output.Lines == std::vector<std::string>{ "[ERROR]: first_error\n", "[ERROR]: second_error\n" }, __func__,
        __LINE__, "Both entries should be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Reconfigure_AppliesNewConfigButKeepsLevel()
{
    // Arrange
    TMemoryOutput output;
    TMemoryTextLog log(output);

    auto firstConfig = MakeQuietConfig();
    firstConfig.Startup.Banner = "first";

    auto secondConfig = MakeQuietConfig();
    secondConfig.Startup.Banner = "second";
    secondConfig.Line.ShowLevel = false;
    secondConfig.InitialMinimumLevel = ASWLog::Level::Error;

    // Act
    const bool reconfiguredBeforeInitialize = log.Reconfigure(secondConfig);
    CheckTrue(log.Initialize(firstConfig), __func__, __LINE__, "Initialize should succeed");
    log.SetMinimumLevel(ASWLog::Level::Debug);
    log.LogDebug("before");
    const bool reconfigured = log.Reconfigure(secondConfig);
    log.LogDebug("after");

    log.ReconfigureResult = false;
    secondConfig.Startup.Banner = "third";
    const bool reconfiguredWithFailingHook = log.Reconfigure(secondConfig);

    // Assert
    CheckFalse(reconfiguredBeforeInitialize, __func__, __LINE__, "Reconfigure() should fail before Initialize()");
    CheckTrue(reconfigured, __func__, __LINE__, "Reconfigure() should succeed once initialized");
    CheckTrue(output.Lines == std::vector<std::string>{ "[INFO]: first\n", "[DEBUG]: before\n", ": after\n" }, __func__,
        __LINE__, "Entries after Reconfigure() should use the new line layout, and Reconfigure() should write no startup lines");
    CheckEquals(static_cast<int>(ASWLog::Level::Debug), static_cast<int>(log.GetMinimumLevel()), __func__, __LINE__,
        "Reconfigure() should keep the minimum level set with SetMinimumLevel(), not apply InitialMinimumLevel");
    CheckTrue(log.ReconfiguredFromBanners == std::vector<std::string>{ "first", "second" }, __func__, __LINE__,
        "ReconfigureUnlocked() should get the config it replaced (and not be called before Initialize())");
    CheckTrue(log.ReconfiguredToBanners == std::vector<std::string>{ "second", "third" }, __func__, __LINE__,
        "The new config should be stored before ReconfigureUnlocked() is called");
    CheckFalse(reconfiguredWithFailingHook, __func__, __LINE__, "Reconfigure() should fail if ReconfigureUnlocked() fails");
    CheckEquals(std::string("third"), log.GetConfig()->Startup.Banner, __func__, __LINE__,
        "The new config should be kept when ReconfigureUnlocked() fails");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Reconfigure_IsSafeWhileOtherThreadsLog()
{
    // Arrange: one thread keeps switching between configs that differ in the line layout, the formatter and the
    // callback, while others log. Run under ThreadSanitizer (CI) to find data races.
    constexpr int ThreadCount = 4;
    constexpr int EntriesPerThread = 2000;
    constexpr int MinimumReconfigureCount = 3;

    TMemoryOutput output;
    auto callbackCount = std::make_shared<std::atomic<int> >(0);

    auto plainConfig = MakeQuietConfig();
    plainConfig.Line.ShowLevel = false;

    auto callbackConfig = MakeQuietConfig();
    callbackConfig.OnLogEntryMinimumLevel = ASWLog::Level::Info;
    callbackConfig.OnLogEntry = [callbackCount](const ASWLog::TASWLogRecord&, std::string_view) {
            callbackCount->fetch_add(1, std::memory_order_relaxed);
        };

    auto formatterConfig = MakeQuietConfig();
    formatterConfig.Line.Formatter = std::make_shared<TPipeFormatter>();

    TMemoryTextLog log(output);
    CheckTrue(log.Initialize(plainConfig), __func__, __LINE__, "Initialize should succeed");

    // Act
    std::atomic<bool> loggingDone{ false };
    int reconfigureCount = 0;
    int failedReconfigureCount = 0;
    std::thread reconfigurer([&] {
        const ASWLog::TASWLogConfig* configs[] = { &callbackConfig, &formatterConfig, &plainConfig };
        for (std::size_t i = 0; !loggingDone.load() || i < MinimumReconfigureCount; ++i)
        {
            if (!log.Reconfigure(*configs[i % 3]))
                ++failedReconfigureCount;

            ++reconfigureCount;
        }
            });

    std::vector<std::thread> loggers;
    for (int threadIndex = 0; threadIndex < ThreadCount; ++threadIndex)
    {
        loggers.emplace_back([&log] {
                for (int entry = 0; entry < EntriesPerThread; ++entry)
                    log.LogInfo("entry");
            });
    }

    for (auto& thread : loggers)
        thread.join();

    loggingDone.store(true);
    reconfigurer.join();

    // Assert: every line has one of the three layouts
    std::size_t unexpectedLines = 0;
    for (const auto& line : output.Lines)
    {
        if (line != "[INFO]: entry\n" && line != ": entry\n" && line != "INFO|entry\n")
            ++unexpectedLines;
    }

    CheckEquals(static_cast<std::size_t>(ThreadCount * EntriesPerThread), output.Lines.size(), __func__, __LINE__,
        "Every entry should be written while the logger is reconfigured");
    CheckEquals(static_cast<std::size_t>(0), unexpectedLines, __func__, __LINE__, "Every line should be written with one whole config");
    CheckTrue(reconfigureCount >= MinimumReconfigureCount, __func__, __LINE__, "The logger should have been reconfigured");
    CheckEquals(0, failedReconfigureCount, __func__, __LINE__, "Every Reconfigure() should succeed");
    CheckTrue(callbackCount->load() <= ThreadCount * EntriesPerThread, __func__, __LINE__,
        "OnLogEntry should be called at most once per entry");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_ReportErrorUnlocked_ReportsAfterTheLockIsReleased()
{
    // Arrange: WriteLineUnlocked() reports a failure while m_Mutex is held
    TMemoryOutput output;
    TMemoryTextLog log(output);
    std::vector<std::string> reportedMessages;
    std::vector<bool> isMutexFreeInHandler;
    auto config = MakeQuietConfig();
    config.ErrorReportInterval = std::chrono::milliseconds(0);
    config.OnError = [&log, &reportedMessages, &isMutexFreeInHandler](const ASWLog::TASWLogError& error) {
            reportedMessages.push_back(error.Message);
            isMutexFreeInHandler.push_back(log.IsMutexFree());
        };
    CheckTrue(log.Initialize(config), __func__, __LINE__, "Initialize should succeed");
    log.ReportsOnWrite = true;

    // Act
    log.LogInfo("first");
    log.LogRaw(ASWLog::Level::Info, "second");

    // Assert
    CheckTrue(reportedMessages == std::vector<std::string>{ "memory write failed", "memory write failed" }, __func__, __LINE__,
        "Each failure reported by a hook should reach OnError");
    CheckTrue(isMutexFreeInHandler == std::vector<bool>{ true, true }, __func__, __LINE__,
        "OnError should be called after the logger's lock is released, so it can use the logger");
    CheckTrue(output.Lines == std::vector<std::string>{ "[INFO]: first\n", "second" }, __func__, __LINE__, "The entries should still be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_SetEnabled_FalseWritesNothing()
{
    // Arrange
    TMemoryOutput output;
    int callbackCount = 0;
    auto config = MakeQuietConfig();
    config.Startup.Banner = "startup banner";
    config.Startup.WriteTimeInfo = true;
    config.Shutdown.WriteLine = true;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Trace;
    config.OnLogEntry = [&callbackCount](const ASWLog::TASWLogRecord&, std::string_view) {
            ++callbackCount;
        };

    // Act
    bool openWhileDisabled = false;
    std::size_t linesWhileDisabled = 0;
    int afterEntryCountWhileDisabled = 0;
    std::size_t linesBeforeDestruction = 0;
    {
        TMemoryTextLog log(output);
        log.SetEnabled(false);
        CheckTrue(log.Initialize(config), __func__, __LINE__, "Initialize should succeed while disabled");
        const int afterEntryCountAtStart = output.AfterEntryCount;

        log.LogCritical("critical");
        log.LogRaw(ASWLog::Level::Critical, "critical_raw\n");
        log.LogForce(ASWLog::Level::Critical, "forced");
        log.LogForceRaw(ASWLog::Level::Critical, "forced_raw\n");
        log.LogCriticalFmt("{}", "critical_fmt");
        openWhileDisabled = log.IsOpen();
        linesWhileDisabled = output.Lines.size();
        afterEntryCountWhileDisabled = output.AfterEntryCount - afterEntryCountAtStart;

        log.SetEnabled(true);
        log.LogInfo("enabled_again");
        linesBeforeDestruction = output.Lines.size();

        log.SetEnabled(false); // So the destructor writes no shutdown line
    }

    // Assert
    CheckTrue(openWhileDisabled, __func__, __LINE__, "A disabled logger should stay open");
    CheckEquals(static_cast<std::size_t>(0), linesWhileDisabled, __func__, __LINE__,
        "A disabled logger should write nothing: no startup lines, entries, or forced entries");
    CheckEquals(0, afterEntryCountWhileDisabled, __func__, __LINE__, "A disabled logger's entries should not reach its output at all");
    CheckEquals(static_cast<std::size_t>(1), linesBeforeDestruction, __func__, __LINE__, "Logging should resume once enabled again");
    CheckTrue(output.Lines == std::vector<std::string>{ "[INFO]: enabled_again\n" }, __func__, __LINE__,
        "Disabling before destruction should suppress the shutdown line");
    CheckEquals(1, callbackCount, __func__, __LINE__, "OnLogEntry should only fire for the entry written while enabled");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_TextLogBase)
