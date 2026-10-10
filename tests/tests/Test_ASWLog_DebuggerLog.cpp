/* **************************************************************************
Test_ASWLog_DebuggerLog.cpp
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
#include "Test_ASWLog_DebuggerLog.h"
//---------------------------------------------------------------------------
#include <chrono>
#include <cstring>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#endif
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_CrashHandler.h"
#include "ASWLog_DebuggerLog.h"
//---------------------------------------------------------------------------

namespace
{

// A value whose formatting is counted in DebuggerLogFormatCount, to check whether a *Fmt call formats
struct TDebuggerLogCountedValue
{
};

int DebuggerLogFormatCount = 0;

} // namespace

template<>
struct std::formatter<TDebuggerLogCountedValue>
{
    constexpr std::format_parse_context::iterator parse(std::format_parse_context& context)
    {
        return context.begin();
    }

    std::format_context::iterator format(const TDebuggerLogCountedValue& /*value*/, std::format_context& context) const
    {
        ++DebuggerLogFormatCount;
        return std::format_to(context.out(), "counted");
    }
};

namespace ASWUnitTests
{

namespace
{

// A config with no startup or shutdown lines and only the level in each line, e.g. "[INFO]: message\n"
ASWLog::TASWLogConfig MakeDebuggerConfig()
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

#if defined(_WIN32)
// A debugger logger whose crash write a test can call
class TCrashTestDebuggerLog final : public ASWLog::TASWDebuggerLog
{
public:
    void WriteCrashLine(std::string_view line) noexcept
    {
        WriteCrashLineDirect(line);
    }
};

// Receives what OutputDebugString sends while no debugger is attached, as DebugView does: a writer waits for the
// DBWIN_BUFFER_READY event, puts its process id and the text (in the ANSI code page) in the DBWIN_BUFFER shared memory,
// and sets DBWIN_DATA_READY. Only one listener can have these at a time.
class TDebugOutputListener
{
private:
    static constexpr DWORD BufferSize = 4096;

    HANDLE m_BufferReady = nullptr;
    HANDLE m_DataReady = nullptr;
    HANDLE m_Mapping = nullptr;
    const char* m_View = nullptr;
    bool m_IsListening = false;

public:
    TDebugOutputListener()
    {
        m_BufferReady = CreateEventW(nullptr, FALSE, FALSE, L"DBWIN_BUFFER_READY");
        bool isTaken = GetLastError() == ERROR_ALREADY_EXISTS;
        m_DataReady = CreateEventW(nullptr, FALSE, FALSE, L"DBWIN_DATA_READY");
        isTaken = isTaken || GetLastError() == ERROR_ALREADY_EXISTS;
        m_Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, BufferSize, L"DBWIN_BUFFER");
        isTaken = isTaken || GetLastError() == ERROR_ALREADY_EXISTS;

        if (m_Mapping != nullptr)
            m_View = static_cast<const char*>(MapViewOfFile(m_Mapping, FILE_MAP_READ, 0, 0, BufferSize));

        m_IsListening = !isTaken && m_BufferReady != nullptr && m_DataReady != nullptr && m_View != nullptr;
    }

    ~TDebugOutputListener()
    {
        if (m_View != nullptr)
            UnmapViewOfFile(m_View);

        for (const HANDLE handle : { m_Mapping, m_DataReady, m_BufferReady })
        {
            if (handle != nullptr)
                CloseHandle(handle);
        }
    }

    TDebugOutputListener(const TDebugOutputListener&) = delete;
    TDebugOutputListener& operator=(const TDebugOutputListener&) = delete;

    // False if another listener (e.g. DebugView) has the shared objects
    bool IsListening() const
    {
        return m_IsListening;
    }

    // Runs 'write', which sends one string, and returns the text this process sent, or nothing if none came within
    // a second. Strings from other processes (which may write too) are skipped.
    template<typename TWrite>
    std::optional<std::string> Capture(const TWrite& write)
    {
        SetEvent(m_BufferReady);
        write();

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (WaitForSingleObject(m_DataReady, 100) != WAIT_OBJECT_0)
                continue;

            DWORD processId = 0;
            std::memcpy(&processId, m_View, sizeof(processId));
            const std::string_view text(m_View + sizeof(processId), BufferSize - sizeof(processId));

            if (processId == GetCurrentProcessId())
                return std::string(text.substr(0, text.find('\0')));

            SetEvent(m_BufferReady); // Another process's string: wait for ours
        }

        return std::nullopt;
    }
};

#endif

} // namespace

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_DebuggerLog
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_DebuggerLog::TTest_ASWLog_DebuggerLog()
    : inherited("ASWLog_DebuggerLog_Tests")
{
    RegisterTest(&TTest_ASWLog_DebuggerLog::Test_GetInstance_ReturnsTheSameLogger, "GetInstance_ReturnsTheSameLogger");
    RegisterTest(&TTest_ASWLog_DebuggerLog::Test_ShouldLog_FollowsThePlatform, "ShouldLog_FollowsThePlatform");
    RegisterTest(&TTest_ASWLog_DebuggerLog::Test_Windows_SendsEachLineToTheDebugOutput, "Windows_SendsEachLineToTheDebugOutput");
    RegisterTest(&TTest_ASWLog_DebuggerLog::Test_Windows_WritesTheCrashLineDirectly, "Windows_WritesTheCrashLineDirectly");
}
//---------------------------------------------------------------------------
TTest_ASWLog_DebuggerLog::~TTest_ASWLog_DebuggerLog()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_DebuggerLog::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_DebuggerLog::SetUp_Test(ITestCase& /*testCase*/)
{
    DebuggerLogFormatCount = 0;
}
//---------------------------------------------------------------------------
void TTest_ASWLog_DebuggerLog::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_DebuggerLog::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_DebuggerLog::Test_GetInstance_ReturnsTheSameLogger()
{
    // Act
    auto& first = ASWLog::TASWDebuggerLog::GetInstance();
    auto& second = ASWLog::TASWDebuggerLog::GetInstance();

    // Assert
    CheckSame(&first, &second, "GetInstance() should always return the same logger");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_DebuggerLog::Test_ShouldLog_FollowsThePlatform()
{
    // Arrange
    ASWLog::TASWDebuggerLog log;
    const bool initialized = log.Initialize(MakeDebuggerConfig());
    ASWLog::TASWLogRecord infoRecord;
    infoRecord.LogLevel = ASWLog::Level::Info;

    // Act
    log.LogInfoFmt("{}", TDebuggerLogCountedValue{});

    // Assert
    CheckTrue(initialized, "Initialize should succeed on every platform");
#if defined(_WIN32)
    CheckTrue(log.ShouldLog(ASWLog::Level::Info), "On Windows, ShouldLog() should follow the minimum level (Info)");
    CheckFalse(log.ShouldLog(ASWLog::Level::Debug), "On Windows, ShouldLog() should follow the minimum level (Debug)");
    CheckTrue(log.ShouldLog(infoRecord), "On Windows, ShouldLog(record) should follow the minimum level");
    CheckEquals(1, DebuggerLogFormatCount, "On Windows, a *Fmt entry should be formatted");
#else
    CheckFalse(log.ShouldLog(ASWLog::Level::Critical), "Without a debugger output, ShouldLog() should be false");
    CheckFalse(log.ShouldLog(infoRecord), "Without a debugger output, ShouldLog(record) should be false");
    CheckEquals(0, DebuggerLogFormatCount, "Without a debugger output, nothing should be formatted");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_DebuggerLog::Test_Windows_SendsEachLineToTheDebugOutput()
{
#if defined(_WIN32)
    // Arrange: listen like DebugView, which needs no debugger attached and no other listener
    if (IsDebuggerPresent())
        Skip("A debugger receives the debug output instead of a listener");

    TDebugOutputListener listener;
    if (!listener.IsListening())
        Skip("Another debug output listener (e.g. DebugView) is running");

    ASWLog::TASWDebuggerLog log;
    CheckTrue(log.Initialize(MakeDebuggerConfig()), "Initialize should succeed");
    auto crlfConfig = MakeDebuggerConfig();
    crlfConfig.Line.Ending = ASWLog::LineEnding::CRLF;

    // Act
    const auto formatted = listener.Capture([&log] {
            log.LogInfo("hello");
        });
    const auto raw = listener.Capture([&log] {
            log.LogRaw(ASWLog::Level::Warn, "raw text");
        });
    const bool reconfigured = log.Reconfigure(crlfConfig);
    const auto crlf = listener.Capture([&log] {
            log.LogError("crlf");
        });

    // Assert
    CheckTrue(reconfigured, "Reconfigure should succeed");
    CheckTrue(formatted == std::string("[INFO]: hello\n"), "The formatted line, with its ending, should be sent: " + formatted.value_or("(none)"));
    CheckTrue(raw == std::string("raw text"), "A raw entry should be sent as is: " + raw.value_or("(none)"));
    CheckTrue(crlf == std::string("[ERROR]: crlf\r\n"), "The line should end with the configured ending: " + crlf.value_or("(none)"));
#else
    Skip("Only Windows has a debugger output");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_DebuggerLog::Test_Windows_WritesTheCrashLineDirectly()
{
#if defined(_WIN32)
    // Arrange
    if (IsDebuggerPresent())
        Skip("A debugger receives the debug output instead of a listener");

    TDebugOutputListener listener;
    if (!listener.IsListening())
        Skip("Another debug output listener (e.g. DebugView) is running");

    TCrashTestDebuggerLog log;
    const std::string longLine(ASWLog::Detail::TCrashText::Capacity + 500, 'x');

    // Act
    const auto crashLine = listener.Capture([&log] {
            log.WriteCrashLine("[CRITICAL]: Crash: test\n");
        });
    const auto cutLine = listener.Capture([&log, &longLine] {
            log.WriteCrashLine(longLine);
        });

    // Assert
    CheckTrue(crashLine == std::string("[CRITICAL]: Crash: test\n"), "The crash line should be sent as is: " + crashLine.value_or("(none)"));
    AssertTrue(cutLine.has_value(), "A line longer than a crash line can be should still be sent");
    CheckEquals(ASWLog::Detail::TCrashText::Capacity, cutLine->size(), "It should be cut to a crash line's capacity");
#else
    Skip("Only Windows has a debugger output");
#endif
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_DebuggerLog)
