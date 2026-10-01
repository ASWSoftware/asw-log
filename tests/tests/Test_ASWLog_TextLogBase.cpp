/* **************************************************************************
Test_ASWLog_TextLogBase.cpp
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
#include "Test_ASWLog_TextLogBase.h"
//---------------------------------------------------------------------------
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_FileLog.h"
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
};

// A text logger built only from the TASWTextLogBase hooks, writing to memory
class TMemoryTextLog final : public ASWLog::TASWTextLogBase
{
private:
    TMemoryOutput& m_Output;

public:
    bool IsReady = true; // Result of EnsureReadyUnlocked() while initialized and open
    bool AcceptsWrites = true; // Result of PrepareWriteUnlocked()
    bool ThrowsOnWrite = false;

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

    void WriteLineUnlocked(ASWLog::Level /*level*/, std::string_view line, bool endsLine) override
    {
        if (ThrowsOnWrite)
            throw std::runtime_error("write failed");

        m_Output.Lines.emplace_back(line);
        m_Output.EndsLine.push_back(endsLine);
    }

public:
    explicit TMemoryTextLog(TMemoryOutput& output)
        : m_Output(output)
    {
    }

    ~TMemoryTextLog() override
    {
        Finalize();
    }
};

// A file logger with its own line layout, "LEVEL|message", as a user's derived class would write it
class TPipeFileLog final : public ASWLog::TASWFileLog
{
protected:
    std::string FormatLine(ASWLog::Level level, std::string_view message, std::source_location /*loc*/,
        std::chrono::system_clock::time_point /*now*/) const override
    {
        return std::string(ASWLog::Level_ToString(level)).append("|").append(message);
    }

public:
    ~TPipeFileLog() override
    {
        Finalize(); // So the shutdown line uses this class's FormatLine()
    }
};

// A config with no startup or shutdown lines, and only the level in each line, e.g. "[INFO]: message\n"
ASWLog::TASWLogConfig MakeQuietConfig()
{
    ASWLog::TASWLogConfig config;
    config.LogsFolderPath = TestTempDir;
    config.LogFilePath = "textlogbase.log";
    config.InitialMinimumLevel = ASWLog::Level::Info;
    config.LogUTCDateTime = false;
    config.LogLevelStr = true;
    config.LogProcessId = false;
    config.LogThreadId = false;
    config.LogMethodName = false;
    config.LogSourceLine = false;
    config.OpenRetryCount = 1;
    config.WriteShutdownLog = false;
    config.Init_LogTimeInfo = false;
    config.Init_LogOSInfo = false;
    config.Init_LogDriveInfo = false;
    config.Init_LogSysMemInfo = false;
    config.Init_LogApplicationInfo = false;
    config.Init_LogMemoryUsage = false;
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
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_FormatLine_OverrideChangesFileLineLayout, "FormatLine_OverrideChangesFileLineLayout");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Initialize_WritesStartupLinesThenCallsAfterEntry, "Initialize_WritesStartupLinesThenCallsAfterEntry");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Log_DroppedWhenNotReadyOrNotPrepared, "Log_DroppedWhenNotReadyOrNotPrepared");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Log_FormatsFiltersAndCallsAfterEntry, "Log_FormatsFiltersAndCallsAfterEntry");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_Log_ThrowingWriteDoesNotEscape, "Log_ThrowingWriteDoesNotEscape");
    RegisterTest(&TTest_ASWLog_TextLogBase::Test_LogRaw_WritesMessageAsIs, "LogRaw_WritesMessageAsIs");
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
    config.WriteShutdownLog = true;
    config.BannerMessage_Shutdown = "goodbye";

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
void TTest_ASWLog_TextLogBase::Test_FormatLine_OverrideChangesFileLineLayout()
{
    // Arrange
    const auto logPath = TestTempDir / "textlogbase.log";
    std::vector<std::string> callbackLines;
    auto config = MakeQuietConfig();
    config.BannerMessage_Init = "start";
    config.WriteShutdownLog = true;
    config.BannerMessage_Shutdown = "bye";
    config.CallbackMinimumLevel = ASWLog::Level::Info;
    config.OnLogEntry = [&callbackLines](ASWLog::Level /*level*/, std::string_view line) {
            callbackLines.emplace_back(line);
        };

    // Act
    {
        TPipeFileLog log;
        CheckTrue(log.Initialize(config), __func__, __LINE__, "Initialize should succeed");
        log.LogInfo("hello");
        log.LogWarn("careful");
        log.LogRaw(ASWLog::Level::Info, "raw text\n");
    }

    const auto contents = ReadFileText(logPath);

    // Assert
    const std::string expectedStart = "INFO|start\nINFO|hello\nWARN|careful\nraw text\nINFO|Logger shutdown: ";
    CheckTrue(contents.starts_with(expectedStart), __func__, __LINE__,
        "Every formatted line, including the startup and shutdown lines, should use the override; raw text should not: " + contents);
    CheckTrue(contents.ends_with(", bye\n"), __func__, __LINE__, "The shutdown line should end with the shutdown banner: " + contents);
    CheckEquals(static_cast<std::size_t>(3), callbackLines.size(), __func__, __LINE__, "OnLogEntry should get the three logged entries");
    if (callbackLines.size() == 3)
        CheckEquals(std::string("INFO|hello\n"), callbackLines[0], __func__, __LINE__, "OnLogEntry should get the line in the override's layout");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_TextLogBase::Test_Initialize_WritesStartupLinesThenCallsAfterEntry()
{
    // Arrange
    TMemoryOutput output;
    TMemoryTextLog log(output);
    auto config = MakeQuietConfig();
    config.BannerMessage_Init = "starting";
    config.Init_LogTimeInfo = true;

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
void TTest_ASWLog_TextLogBase::Test_Log_DroppedWhenNotReadyOrNotPrepared()
{
    // Arrange
    TMemoryOutput output;
    int callbackCount = 0;
    auto config = MakeQuietConfig();
    config.CallbackMinimumLevel = ASWLog::Level::Info;
    config.OnLogEntry = [&callbackCount](ASWLog::Level, std::string_view) {
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
    config.LogLineEnding = ASWLog::LineEnding::CRLF;
    config.CallbackMinimumLevel = ASWLog::Level::Trace;
    config.OnLogEntry = [&callbackLines](ASWLog::Level, std::string_view line) {
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
void TTest_ASWLog_TextLogBase::Test_Log_ThrowingWriteDoesNotEscape()
{
    // Arrange
    TMemoryOutput output;
    TMemoryTextLog log(output);
    CheckTrue(log.Initialize(MakeQuietConfig()), __func__, __LINE__, "Initialize should succeed");

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

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_TextLogBase)
