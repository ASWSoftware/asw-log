/* **************************************************************************
Test_ASWLog_CrashHandler.cpp
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
#include "Test_ASWLog_CrashHandler.h"
//---------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#undef min
#undef max
#else
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_ConsoleLog.h"
#include "ASWLog_CrashHandler.h"
#include "ASWLog_FileLog.h"
#include "ASWLog_Formatter.h"
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------
#include "UT_Helper_JSON.h"
//---------------------------------------------------------------------------

// The sanitizers replace or watch the signal handlers and the stack, which a stack overflow test can't work with
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define ASWLOG_TEST_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define ASWLOG_TEST_SANITIZED 1
#endif
#endif

// The VCL GUI runner links the Delphi RTL, which turns a fault into a Delphi exception before the crash handlers see it
// (see ASWLog_CrashHandler.h)
#if defined(_WIN32) && defined(ASWUNITTESTS_RTL_EXCEPTIONS)
#define ASWLOG_TEST_DELPHI_RTL 1
#endif

namespace ASWUnitTests
{

namespace
{

const auto GroupBaseTempDir = std::filesystem::temp_directory_path() / "aswlog_crashhandler_tests";
const auto TestTempDir = GroupBaseTempDir / "test";

// How long a test waits for something another thread or process does, before it fails
constexpr std::chrono::milliseconds WaitTimeout = std::chrono::seconds(30);

// The command line option that makes this test executable a crash helper process (see RunCrashHelperIfRequested())
constexpr std::string_view CrashHelperOption = "--aswlog-crash-helper=";

// The exit codes of a crash helper process. The handlers it installs before InstallCrashHandlers() end it with one of
// the PassedOn* codes, which shows that the crash handlers passed the crash on to the handler of that kind.
constexpr int PassedOnTerminateExitCode = 71;
constexpr int PassedOnAbortExitCode = 74;
constexpr int PassedOnFaultExitCode = 75;
constexpr int NotCrashedExitCode = 72;
constexpr int UnknownScenarioExitCode = 73;

// A user formatter that crashes (a null pointer write) for the message "crash_now", while the logger's lock is held
class TCrashingFormatter final : public ASWLog::IASWLogFormatter
{
public:
    std::string Format(const ASWLog::TASWLogRecord& record, const ASWLog::TASWLogConfig& config) const override;
};

// A user formatter that throws for a crash line, as one might in a crashed process (e.g. out of memory)
class TCrashLineThrowingFormatter final : public ASWLog::IASWLogFormatter
{
public:
    std::string Format(const ASWLog::TASWLogRecord& record, const ASWLog::TASWLogConfig& config) const override
    {
        if (record.Message.starts_with("Crash: "))
            throw std::runtime_error("format failed");

        return ASWLog::TASWTextFormatter::FormatLine(record, config);
    }
};

// A file logger whose lock a test can hold from another thread
class TLockableFileLog final : public ASWLog::TASWFileLog
{
public:
    ~TLockableFileLog() override
    {
        Finalize();
    }

    std::mutex& GetMutex() noexcept
    {
        return m_Mutex;
    }
};

// Never set: lets the endless recursion of RecurseWithoutEnd() look conditional to the compiler
volatile bool StopRecursion = false;

// Read at run time, so the compiler can't see the null pointer that WriteToNullPointer() writes through
volatile std::uintptr_t NullAddress = 0;

// The "previous" handlers a crash helper installs before InstallCrashHandlers(): each ends the process with
// its PassedOn* code, at once and quietly (no error report or core dump)
[[noreturn]] void ExitAsPassedOn() noexcept
{
    std::_Exit(PassedOnTerminateExitCode);
}

#if defined(_WIN32)
LONG WINAPI ExitAsPassedOnFromFilter(EXCEPTION_POINTERS* /*exceptionInfo*/)
{
    TerminateProcess(GetCurrentProcess(), PassedOnFaultExitCode);
    return EXCEPTION_EXECUTE_HANDLER;
}

void ExitAsPassedOnFromSignal(int /*signalNumber*/) // SIGABRT
{
    std::_Exit(PassedOnAbortExitCode);
}
#else
void ExitAsPassedOnFromSignal(int signalNumber, siginfo_t* /*info*/, void* /*context*/)
{
    _exit(signalNumber == SIGABRT ? PassedOnAbortExitCode : PassedOnFaultExitCode);
}
#endif

// Calls HandleCrash(reason), with a 50 ms WaitTimeout, while another thread holds the lock of 'log', so the crash lines
// are written straight to its file in the fixed layout. Returns how long HandleCrash() took.
std::chrono::milliseconds HandleCrashWhileLocked(TLockableFileLog& log, std::string_view reason)
{
    std::mutex mutex;
    std::condition_variable changed;
    bool isLocked = false;
    bool mayUnlock = false;
    std::thread holder([&] {
        std::lock_guard<std::mutex> logLock(log.GetMutex());
        std::unique_lock<std::mutex> lock(mutex);
        isLocked = true;
        changed.notify_all();
        changed.wait(lock, [&] {
            return mayUnlock;
                    });
                });

    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] {
                    return isLocked;
                });
    }

    ASWLog::TASWCrashHandlerOptions options;
    options.WaitTimeout = std::chrono::milliseconds(50);
    const auto start = std::chrono::steady_clock::now();
    ASWLog::HandleCrash(reason, options);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    {
        std::lock_guard<std::mutex> lock(mutex);
        mayUnlock = true;
    }

    changed.notify_all();
    holder.join();

    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
}

// An OnError handler for the tests that cause failures on purpose, so their reports don't go to stderr
void IgnoreError(const ASWLog::TASWLogError& /*error*/)
{
}

// Installs the handlers that end a crash helper process (see ExitAsPassedOn())
void InstallPassOnHandlers()
{
    std::set_terminate(ExitAsPassedOn);

#if defined(_WIN32)
    // In case a crash isn't passed on: no error dialog or report
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); // Not in MinGW's import library
#endif
    SetUnhandledExceptionFilter(ExitAsPassedOnFromFilter);
    std::signal(SIGABRT, ExitAsPassedOnFromSignal);
#else
    // In case a crash isn't passed on: no core dump
    const rlimit noCoreDump{ 0, 0 };
    setrlimit(RLIMIT_CORE, &noCoreDump);

    struct sigaction action {};
    action.sa_sigaction = ExitAsPassedOnFromSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK; // A stack overflow passed on still runs on the alternate stack
    sigemptyset(&action.sa_mask);
    for (const int signalNumber : { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT })
        sigaction(signalNumber, &action, nullptr);
#endif
}

// A config for a crash helper's file logger, "<name>" in TestTempDir: each line is "[LEVEL]: message", no startup or
// shutdown lines, and nothing is flushed until the crash
ASWLog::TASWLogConfig MakeCrashTestConfig(const std::filesystem::path& fileName)
{
    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = fileName;
    config.File.OpenRetryCount = 1;
    config.File.Flush = ASWLog::FlushMode::Manual;
    config.File.FlushImmediatelyAtLevel = ASWLog::Level::Off;
    config.InitialMinimumLevel = ASWLog::Level::Info;
    config.Line.ShowTimestamp = false;
    config.Line.ShowLevel = true;
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
    config.OnError = IgnoreError;
    return config;
}

// A file logger for a crash helper, never destroyed (the process crashes before static destruction)
ASWLog::TASWFileLog& MakeCrashTestLog(const ASWLog::TASWLogConfig& config)
{
    auto* log = new ASWLog::TASWFileLog();
    log->Initialize(config);
    return *log;
}

std::string ReadFileText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};

    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

int RecurseWithoutEnd(int depth)
{
    volatile char frame[4096];
    frame[0] = static_cast<char>(depth);
    if (StopRecursion)
        return frame[0];

    return RecurseWithoutEnd(depth + 1) + frame[0];
}

void WriteToNullPointer()
{
    *reinterpret_cast<volatile int*>(NullAddress) = 1;
}

// In a crash helper process: runs 'scenario', which ends in a crash. Each scenario logs to "<scenario>.log" in
// TestTempDir (with Manual flush, so its entries are still buffered when it crashes) and installs the crash handlers
// after the "previous" ones (see InstallPassOnHandlers()).
[[noreturn]] void RunCrashScenario(std::string_view scenario)
{
    InstallPassOnHandlers();
    const auto fileName = std::filesystem::path(std::string(scenario) + ".log");

    if (scenario == "terminate")
    {
        // A second logger leaves the crash line out, but is flushed
        auto& log = MakeCrashTestLog(MakeCrashTestConfig(fileName));
        auto otherConfig = MakeCrashTestConfig(std::string(scenario) + "_other.log");
        otherConfig.Shutdown.WriteCrashLine = false;
        auto& otherLog = MakeCrashTestLog(otherConfig);

        log.LogInfo("entry_1");
        log.LogInfo("entry_2");
        otherLog.LogInfo("other_entry");
        ASWLog::InstallCrashHandlers();

        try
        {
            throw std::runtime_error("helper exception");
        }
        catch (...)
        {
            std::terminate();
        }
    }
    else if (scenario == "terminate_then_abort")
    {
        // The default terminate handler calls abort(), which raises SIGABRT: a second handler for the same crash
        auto& log = MakeCrashTestLog(MakeCrashTestConfig(fileName));
        log.LogInfo("entry_1");
        std::set_terminate(nullptr);
        ASWLog::InstallCrashHandlers();
        std::terminate();
    }
    else if (scenario == "abort")
    {
        auto& log = MakeCrashTestLog(MakeCrashTestConfig(fileName));
        log.LogInfo("entry_1");
        ASWLog::InstallCrashHandlers();
        std::abort();
    }
    else if (scenario == "fault")
    {
        auto& log = MakeCrashTestLog(MakeCrashTestConfig(fileName));
        log.LogInfo("entry_1");
        ASWLog::InstallCrashHandlers();
        WriteToNullPointer();
    }
    else if (scenario == "fault_backtrace")
    {
        // In a POSIX signal handler, the backtrace is written in the fixed layout
        auto config = MakeCrashTestConfig(fileName);
        config.Backtrace.Capacity = 5;
        config.Backtrace.LowestLevel = ASWLog::Level::Debug;
        auto& log = MakeCrashTestLog(config);
        log.LogDebug("debug_1");
        log.LogInfo("entry_1");
        ASWLog::InstallCrashHandlers();
        WriteToNullPointer();
    }
    else if (scenario == "async")
    {
        // The worker pauses after writing entry_1, and the other entries are queued meanwhile, so they are still
        // queued when the process crashes
        static std::atomic<bool> isWorkerPausing{ false };
        auto config = MakeCrashTestConfig(fileName);
        config.Async.Enabled = true;
        config.OnLogEntryMinimumLevel = ASWLog::Level::Info;
        config.OnLogEntry = [](const ASWLog::TASWLogRecord& record, std::string_view /*line*/) {
                if (record.Message != "entry_1")
                    return;

                isWorkerPausing.store(true);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            };

        auto& log = MakeCrashTestLog(config);
        log.LogInfo("entry_1");
        const auto deadline = std::chrono::steady_clock::now() + WaitTimeout;
        while (!isWorkerPausing.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        for (int entry = 2; entry <= 5; ++entry)
            log.LogInfo(std::format("entry_{}", entry));

        ASWLog::InstallCrashHandlers();
        WriteToNullPointer();
    }
    else if (scenario == "lock_held")
    {
        auto config = MakeCrashTestConfig(fileName);
        config.Line.Formatter = std::make_shared<TCrashingFormatter>();
        auto& log = MakeCrashTestLog(config);

        ASWLog::TASWCrashHandlerOptions options;
        options.WaitTimeout = std::chrono::milliseconds(100);
        ASWLog::InstallCrashHandlers(options);
        log.LogInfo("crash_now");
    }
    else if (scenario == "stack_overflow")
    {
        auto& log = MakeCrashTestLog(MakeCrashTestConfig(fileName));
        log.LogInfo("entry_1");
        ASWLog::InstallCrashHandlers();
        RecurseWithoutEnd(0);
    }
    else
    {
        std::_Exit(UnknownScenarioExitCode);
    }

    std::_Exit(NotCrashedExitCode);
}

// Runs the crash scenario named by the CrashHelperOption on the command line, if there is one, and never returns
// then. Called by a static initializer, before main() runs (and so before ASWUnitTests installs its own crash
// handling), so the scenario can crash the process for real.
bool RunCrashHelperIfRequested()
{
    const auto commandLine = ASWLog::GetCommandLineString();
    const auto optionStart = commandLine.find(CrashHelperOption);
    if (optionStart == std::string::npos)
        return false;

    const auto scenarioStart = optionStart + CrashHelperOption.size();
    auto scenarioEnd = scenarioStart;
    while (scenarioEnd < commandLine.size() && commandLine[scenarioEnd] != ' ' && commandLine[scenarioEnd] != '"')
        ++scenarioEnd;

    RunCrashScenario(std::string_view(commandLine).substr(scenarioStart, scenarioEnd - scenarioStart));
}

// Starts this test executable again as a crash helper for 'scenario' (see RunCrashHelperIfRequested()), with its
// stdout and stderr going to 'outputPath', and waits for it to end, for at most WaitTimeout. Returns its exit code (on
// POSIX, 128 + the signal number if a signal ended it), or -1 if it couldn't be started or didn't end in time.
int RunCrashHelper(std::string_view scenario, const std::filesystem::path& outputPath)
{
    const auto exePath = ASWLog::GetExecutablePath();
    const auto option = std::string(CrashHelperOption) + std::string(scenario);

#if defined(_WIN32)
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    const auto output = CreateFileW(outputPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE)
        return -1;

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = output;
    startupInfo.hStdError = output;

    auto commandLine = L"\"" + exePath.wstring() + L"\" " + std::filesystem::path(option).wstring();
    PROCESS_INFORMATION processInfo{};
    const bool isStarted = CreateProcessW(exePath.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startupInfo, &processInfo) != FALSE;
    CloseHandle(output);
    if (!isStarted)
        return -1;

    CloseHandle(processInfo.hThread);

    int exitCode = -1;
    if (WaitForSingleObject(processInfo.hProcess, static_cast<DWORD>(WaitTimeout.count())) == WAIT_OBJECT_0)
    {
        DWORD processExitCode = 0;
        if (GetExitCodeProcess(processInfo.hProcess, &processExitCode))
            exitCode = static_cast<int>(processExitCode);
    }
    else
    {
        TerminateProcess(processInfo.hProcess, 1);
    }

    CloseHandle(processInfo.hProcess);

    return exitCode;
#else
    posix_spawn_file_actions_t fileActions;
    posix_spawn_file_actions_init(&fileActions);
    posix_spawn_file_actions_addopen(&fileActions, STDOUT_FILENO, outputPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fileActions, STDOUT_FILENO, STDERR_FILENO);

    auto exeArgument = exePath.string();
    auto optionArgument = option;
    std::vector<char*> arguments{ exeArgument.data(), optionArgument.data(), nullptr };
    pid_t processId = 0;
    const int spawnResult = posix_spawn(&processId, exeArgument.c_str(), &fileActions, nullptr, arguments.data(), environ);
    posix_spawn_file_actions_destroy(&fileActions);
    if (spawnResult != 0)
        return -1;

    const auto deadline = std::chrono::steady_clock::now() + WaitTimeout;
    int status = 0;
    for (;;)
    {
        const auto waited = waitpid(processId, &status, WNOHANG);
        if (waited == processId)
            break;

        if (waited < 0 && errno != EINTR)
            return -1;

        if (std::chrono::steady_clock::now() >= deadline)
        {
            kill(processId, SIGKILL);
            waitpid(processId, &status, 0);
            return -1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
#endif
}

// Set only in a crash helper process, which never gets past it (see RunCrashHelperIfRequested()). Defined after
// TestTempDir, which the scenarios use, so it is initialized after it.
[[maybe_unused]] const bool IsCrashHelperProcess = RunCrashHelperIfRequested();

//---------------------------------------------------------------------------
std::string TCrashingFormatter::Format(const ASWLog::TASWLogRecord& record, const ASWLog::TASWLogConfig& config) const
{
    if (record.Message == "crash_now")
        WriteToNullPointer();

    return ASWLog::TASWTextFormatter::FormatLine(record, config);
}

} // namespace

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_CrashHandler
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_CrashHandler::TTest_ASWLog_CrashHandler()
    : inherited("ASWLog_CrashHandler_Tests")
{
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_AppendCrashJSONLine_CutsALongTextButStaysValid, "AppendCrashJSONLine_CutsALongTextButStaysValid");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_AppendCrashJSONLine_UsesTheFixedLayout, "AppendCrashJSONLine_UsesTheFixedLayout");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_AppendCrashLine_CutsALongMessageButKeepsTheEnding, "AppendCrashLine_CutsALongMessageButKeepsTheEnding");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_AppendCrashLine_ShowsTheCategory, "AppendCrashLine_ShowsTheCategory");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_AppendCrashLine_UsesTheFixedLayout, "AppendCrashLine_UsesTheFixedLayout");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_CrashProcess_AbortWritesTheCrashLine, "CrashProcess_AbortWritesTheCrashLine");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_CrashProcess_AsyncQueueIsWrittenBeforeTheCrashLine, "CrashProcess_AsyncQueueIsWrittenBeforeTheCrashLine");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_CrashProcess_FaultWritesTheBacktrace, "CrashProcess_FaultWritesTheBacktrace");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_CrashProcess_FaultWritesTheCrashLine, "CrashProcess_FaultWritesTheCrashLine");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_CrashProcess_LockHeldByTheCrashingThreadWritesTheLineDirectly, "CrashProcess_LockHeldByTheCrashingThreadWritesTheLineDirectly");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_CrashProcess_StackOverflowWritesTheCrashLine, "CrashProcess_StackOverflowWritesTheCrashLine");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_CrashProcess_TerminateThenAbortWritesOneCrashLine, "CrashProcess_TerminateThenAbortWritesOneCrashLine");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_CrashProcess_TerminateWritesTheExceptionAndFlushes, "CrashProcess_TerminateWritesTheExceptionAndFlushes");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_HandleCrash_ConsoleLogWritesTheLineToStdErr, "HandleCrash_ConsoleLogWritesTheLineToStdErr");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_HandleCrash_DisabledLoggerIsOnlyFlushed, "HandleCrash_DisabledLoggerIsOnlyFlushed");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_HandleCrash_FlushesAndWritesTheCrashLine, "HandleCrash_FlushesAndWritesTheCrashLine");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_HandleCrash_JSONLoggerGetsJSONLinesWhenTheLockStaysBusy, "HandleCrash_JSONLoggerGetsJSONLinesWhenTheLockStaysBusy");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_HandleCrash_SkipsClosedAndDestroyedLoggers, "HandleCrash_SkipsClosedAndDestroyedLoggers");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_HandleCrash_SyncsTheLineAtSyncToDiskAtLevel, "HandleCrash_SyncsTheLineAtSyncToDiskAtLevel");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_HandleCrash_ThrowingFormatterGetsTheFixedLayoutLine, "HandleCrash_ThrowingFormatterGetsTheFixedLayoutLine");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_HandleCrash_WaitsForQueuedEntries, "HandleCrash_WaitsForQueuedEntries");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_HandleCrash_WriteCrashLineFalseOnlyFlushes, "HandleCrash_WriteCrashLineFalseOnlyFlushes");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_HandleCrash_WritesTheBacktraceBeforeTheCrashLine, "HandleCrash_WritesTheBacktraceBeforeTheCrashLine");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_HandleCrash_WritesTheLineDirectlyWhenTheLockStaysBusy, "HandleCrash_WritesTheLineDirectlyWhenTheLockStaysBusy");
    RegisterTest(&TTest_ASWLog_CrashHandler::Test_InstallCrashHandlers_ChainsAndUninstallRestores, "InstallCrashHandlers_ChainsAndUninstallRestores");
}
//---------------------------------------------------------------------------
TTest_ASWLog_CrashHandler::~TTest_ASWLog_CrashHandler()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::SetUp_Group()
{
    std::filesystem::create_directories(GroupBaseTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::SetUp_Test(ITestCase& /*testCase*/)
{
    std::filesystem::create_directories(TestTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::TearDown_Group()
{
    std::error_code errorCode;
    std::filesystem::remove_all(GroupBaseTempDir, errorCode);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::TearDown_Test(ITestCase& /*testCase*/)
{
    std::error_code errorCode;
    std::filesystem::remove_all(TestTempDir, errorCode);
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_AppendCrashJSONLine_CutsALongTextButStaysValid()
{
    // Arrange: a category that alone would fill the buffer, and messages of two-byte characters and quotes, each
    // escaped as two characters, too long for the buffer; 0 to 3 leading characters move the cut through every
    // position in the repeating 4-byte unit
    ASWLog::TASWLogRecord categorized;
    const std::string category(3000, 'c');
    categorized.Category = category;
    categorized.Message = "after";

    std::vector<std::string> messages;

    for (std::size_t offset = 0; offset < 4; ++offset)
    {
        std::string message(offset, 'x');

        for (int unit = 0; unit < 1000; ++unit)
            message += "\xCE\xBB\"";

        messages.push_back(message);
    }

    // Act
    ASWLog::Detail::TCrashText categoryLine;
    ASWLog::Detail::AppendCrashJSONLine(categoryLine, categorized, true);

    std::vector<std::string> messageLines;

    for (const auto& message : messages)
    {
        ASWLog::TASWLogRecord record;
        record.Message = message;
        ASWLog::Detail::TCrashText line;
        ASWLog::Detail::AppendCrashJSONLine(line, record, true);
        messageLines.emplace_back(line.View());
    }

    // Assert
    const std::string categoryText(categoryLine.View());
    CheckEndsWith(categoryText, R"(,"pid":0,"tid":0,"message":"after"})" "\r\n", "A cut category should leave room for the rest");
    CheckTrue(IsJSONObjectLine(categoryText.substr(0, categoryText.size() - 2)), "The line with a cut category should be one JSON object");

    for (const auto& text : messageLines)
    {
        CheckLessThanOrEqual(text.size(), ASWLog::Detail::TCrashText::Capacity, "The line should fit the buffer");
        CheckGreaterThan(text.size(), ASWLog::Detail::TCrashText::Capacity - 8, "The message should fill the buffer");
        CheckEndsWith(text, "\"}\r\n", "The cut line should still close the message, the object and the line");
        CheckTrue(IsJSONObjectLine(text.substr(0, text.size() - 2)),
            "The cut line should be one JSON object, with no character or escape cut in two: " + text.substr(text.size() - 12));
    }
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_AppendCrashJSONLine_UsesTheFixedLayout()
{
    // Arrange: a crash line, and a raw backtrace entry from a category
    using namespace std::chrono;
    ASWLog::TASWLogRecord record;
    record.Timestamp = sys_days{ year{ 2026 } / 10 / 4 } + hours(12) + minutes(34) + seconds(56) + milliseconds(789);
    record.LogLevel = ASWLog::Level::Critical;
    record.Message = "Crash: \"test\"";
    record.ProcessId = 12;
    record.ThreadId = 345;
    auto categorizedRaw = record;
    categorizedRaw.LogLevel = ASWLog::Level::Debug;
    categorizedRaw.Category = "Net.Http";
    categorizedRaw.Raw = true;
    categorizedRaw.Message = "kept\n";

    // Act
    ASWLog::Detail::TCrashText lfLine;
    ASWLog::Detail::AppendCrashJSONLine(lfLine, record, false);
    ASWLog::Detail::TCrashText crlfLine;
    ASWLog::Detail::AppendCrashJSONLine(crlfLine, record, true);
    ASWLog::Detail::TCrashText categorizedLine;
    ASWLog::Detail::AppendCrashJSONLine(categorizedLine, categorizedRaw, false);

    // Assert
    CheckEquals(std::string(R"({"time":"2026-10-04T12:34:56.789Z","epoch_ms":1791117296789,"level":"CRITICAL","pid":12,"tid":345,)"
        R"("message":"Crash: \"test\""})" "\n"), std::string(lfLine.View()), "The line should have the fixed layout");
    CheckEndsWith(std::string(crlfLine.View()), "\"}\r\n", "The line should end with the configured line ending");
    CheckEquals(std::string(R"({"time":"2026-10-04T12:34:56.789Z","epoch_ms":1791117296789,"level":"DEBUG","category":"Net.Http",)"
        R"("pid":12,"tid":345,"raw":true,"message":"kept\n"})" "\n"), std::string(categorizedLine.View()),
        "The category and the raw mark should be written like the JSON formatter writes them");

    const ASWLog::TASWJSONFormatter formatter;
    const ASWLog::TASWLogConfig defaultConfig;
    CheckEquals(formatter.Format(categorizedRaw, defaultConfig) + "\n", std::string(categorizedLine.View()),
        "The fixed layout should match the JSON formatter's default fields");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_AppendCrashLine_CutsALongMessageButKeepsTheEnding()
{
    // Arrange
    const std::string message(3000, 'x');
    ASWLog::TASWLogRecord record;
    record.LogLevel = ASWLog::Level::Critical;
    record.Message = message;

    // Act
    ASWLog::Detail::TCrashText line;
    ASWLog::Detail::AppendCrashLine(line, record, true);

    // Assert
    const std::string text(line.View());
    CheckEquals(ASWLog::Detail::TCrashText::Capacity, text.size(), "The line should fill the buffer");
    CheckEndsWith(text, "x\r\n", "The cut line should still end with the line ending");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_AppendCrashLine_UsesTheFixedLayout()
{
    // Arrange
    using namespace std::chrono;
    ASWLog::TASWLogRecord record;
    record.Timestamp = sys_days{ year{ 2026 } / 10 / 4 } + hours(12) + minutes(34) + seconds(56) + milliseconds(789);
    record.LogLevel = ASWLog::Level::Critical;
    record.Message = "Crash: test";
    record.ProcessId = 12;
    record.ThreadId = 345;

    // Act
    ASWLog::Detail::TCrashText lfLine;
    ASWLog::Detail::AppendCrashLine(lfLine, record, false);
    ASWLog::Detail::TCrashText crlfLine;
    ASWLog::Detail::AppendCrashLine(crlfLine, record, true);

    // Assert
    CheckEquals(std::string("[2026-10-04T12:34:56.789Z][CRITICAL][P:12][T:345]: Crash: test\n"), std::string(lfLine.View()),
        "The line should have the fixed layout");
    CheckEquals(std::string("[2026-10-04T12:34:56.789Z][CRITICAL][P:12][T:345]: Crash: test\r\n"), std::string(crlfLine.View()),
        "The line should end with the configured line ending");
    CheckStartsWith(std::string(lfLine.View()), "[" + ASWLog::Time::ToISO8601String(record.Timestamp) + "]",
        "The time should be written like the formatter writes it");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_AppendCrashLine_ShowsTheCategory()
{
    // Arrange: a backtrace entry from a category, as a crash handler writes it in the fixed layout
    using namespace std::chrono;
    ASWLog::TASWLogRecord record;
    record.Timestamp = sys_days{ year{ 2026 } / 10 / 8 } + hours(1) + minutes(2) + seconds(3) + milliseconds(4);
    record.LogLevel = ASWLog::Level::Debug;
    record.Category = "Net.Http";
    record.Message = "kept";
    record.ProcessId = 12;
    record.ThreadId = 345;

    // Act
    ASWLog::Detail::TCrashText line;
    ASWLog::Detail::AppendCrashLine(line, record, false);

    // Assert
    CheckEquals(std::string("[2026-10-08T01:02:03.004Z][DEBUG][Net.Http][P:12][T:345]: kept\n"), std::string(line.View()),
        "The category should follow the level, as in the built-in layout");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_CrashProcess_AbortWritesTheCrashLine()
{
    // Act
    const int exitCode = RunCrashHelper("abort", TestTempDir / "abort_output.txt");

    // Assert: on Windows the line is formatted; in a POSIX signal handler it has the fixed layout
    const auto contents = ReadFileText(TestTempDir / "abort.log");
    CheckEquals(PassedOnAbortExitCode, exitCode, "The crash should be passed on to the abort handler installed before");
    CheckStartsWith(contents, "[INFO]: entry_1\n", "The buffered entry should be flushed");
#if defined(_WIN32)
    CheckEndsWith(contents, "[CRITICAL]: Crash: SIGABRT (abort() called)\n", "The crash line should follow it");
#else
    CheckContains(contents, "][CRITICAL][P:", "The crash line should have the fixed layout");
    CheckEndsWith(contents, "]: Crash: SIGABRT (abort() called)\n", "The crash line should follow it");
#endif
    CheckNotContains(ReadFileText(TestTempDir / "abort_output.txt"), "Sanitizer", "No sanitizer report");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_CrashProcess_AsyncQueueIsWrittenBeforeTheCrashLine()
{
    // Act
    const int exitCode = RunCrashHelper("async", TestTempDir / "async_output.txt");

    // Assert
    const auto contents = ReadFileText(TestTempDir / "async.log");
    CheckEquals(PassedOnFaultExitCode, exitCode, "The crash should be passed on to the fault handler installed before");
    CheckStartsWith(contents, "[INFO]: entry_1\n[INFO]: entry_2\n[INFO]: entry_3\n[INFO]: entry_4\n[INFO]: entry_5\n",
        "The queued entries should be written and flushed first");
    CheckContains(contents, "Crash: ", "The crash line should follow them");
    CheckNotContains(ReadFileText(TestTempDir / "async_output.txt"), "Sanitizer", "No sanitizer report");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_CrashProcess_FaultWritesTheBacktrace()
{
    // Act
    const int exitCode = RunCrashHelper("fault_backtrace", TestTempDir / "fault_backtrace_output.txt");

    // Assert: on Windows through the formatter; in a POSIX signal handler in the fixed layout
    const auto contents = ReadFileText(TestTempDir / "fault_backtrace.log");
    CheckEquals(PassedOnFaultExitCode, exitCode, "The crash should be passed on to the fault handler installed before");
#if defined(_WIN32)
    CheckStartsWith(contents, "[INFO]: entry_1\n[INFO]: Backtrace: the last 1 entry below the minimum level\n[DEBUG]: debug_1\n"
        "[INFO]: Backtrace end\n[CRITICAL]: Crash: unhandled exception 0x", "The backtrace should come before the crash line");
#else
    const auto beginAt = contents.find("][INFO][P:");
    const auto entryAt = contents.find("]: debug_1\n");
    const auto endAt = contents.find("]: Backtrace end\n");
    const auto crashAt = contents.find("]: Crash: SIGSEGV");
    CheckStartsWith(contents, "[INFO]: entry_1\n", "The buffered entry should be flushed first");
    CheckContains(contents, "]: Backtrace: the last 1 entry below the minimum level\n", "The backtrace should start with its marker");
    CheckContains(contents, "][DEBUG][P:", "The kept entry should have the fixed layout");
    CheckTrue(beginAt < entryAt && entryAt < endAt && endAt < crashAt && crashAt != std::string::npos,
        "The backtrace should come before the crash line");
#endif
    CheckNotContains(ReadFileText(TestTempDir / "fault_backtrace_output.txt"), "Sanitizer", "No sanitizer report");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_CrashProcess_FaultWritesTheCrashLine()
{
    // Act
    const int exitCode = RunCrashHelper("fault", TestTempDir / "fault_output.txt");

    // Assert
    const auto contents = ReadFileText(TestTempDir / "fault.log");
    CheckEquals(PassedOnFaultExitCode, exitCode, "The crash should be passed on to the fault handler installed before");
    CheckStartsWith(contents, "[INFO]: entry_1\n", "The buffered entry should be flushed");
#if defined(ASWLOG_TEST_DELPHI_RTL)
    CheckContains(contents, "[CRITICAL]: Crash: unhandled exception 0x0EEDFADE (Delphi exception) at 0x",
        "The crash line should name the Delphi exception the RTL turned the fault into");
#elif defined(_WIN32)
    CheckContains(contents,
        "[CRITICAL]: Crash: unhandled exception 0xC0000005 (access violation writing address 0x0000000000000000) at 0x",
        "The crash line should name the exception and the address");
#else
    CheckContains(contents, "][CRITICAL][P:", "The crash line should have the fixed layout");
    CheckEndsWith(contents, "]: Crash: SIGSEGV (address 0x0000000000000000)\n", "The crash line should name the signal and the address");
#endif
    CheckNotContains(ReadFileText(TestTempDir / "fault_output.txt"), "Sanitizer", "No sanitizer report");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_CrashProcess_LockHeldByTheCrashingThreadWritesTheLineDirectly()
{
#if defined(ASWLOG_TEST_DELPHI_RTL)
    Skip("With the Delphi RTL, a fault in a formatter ends the process before any crash handler runs");
#endif

    // Act: the process crashes in the formatter, holding the logger's lock
    const auto start = std::chrono::steady_clock::now();
    const int exitCode = RunCrashHelper("lock_held", TestTempDir / "lock_held_output.txt");
    const auto elapsed = std::chrono::steady_clock::now() - start;

    // Assert: after the wait timeout (100 ms), the fixed-layout line is written straight to the file
    const auto contents = ReadFileText(TestTempDir / "lock_held.log");
    CheckEquals(PassedOnFaultExitCode, exitCode, "The crash should be passed on to the fault handler installed before");
    CheckLessThan(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 10000,
        "The handler should give up waiting for the lock");
    CheckContains(contents, "][CRITICAL][P:", "The crash line should have the fixed layout");
#if defined(_WIN32)
    CheckContains(contents, "]: Crash: unhandled exception 0xC0000005", "The crash line should name the exception");
#else
    CheckContains(contents, "]: Crash: SIGSEGV", "The crash line should name the signal");
#endif
    CheckNotContains(ReadFileText(TestTempDir / "lock_held_output.txt"), "Sanitizer", "No sanitizer report");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_CrashProcess_StackOverflowWritesTheCrashLine()
{
#if defined(ASWLOG_TEST_SANITIZED)
    Skip("A sanitizer handles the stack itself");
#elif defined(ASWLOG_TEST_DELPHI_RTL)
    Skip("With the Delphi RTL, a stack overflow ends the process before any crash handler runs");
#endif

    // Act
    const int exitCode = RunCrashHelper("stack_overflow", TestTempDir / "stack_overflow_output.txt");

    // Assert
    const auto contents = ReadFileText(TestTempDir / "stack_overflow.log");
    CheckEquals(PassedOnFaultExitCode, exitCode, "The crash should be passed on to the fault handler installed before");
    CheckStartsWith(contents, "[INFO]: entry_1\n", "The buffered entry should be flushed");
#if defined(_WIN32)
    CheckContains(contents, "[CRITICAL]: Crash: unhandled exception 0xC00000FD (stack overflow) at 0x",
        "The crash line should name the exception");
#else
    CheckContains(contents, "]: Crash: SIGSEGV (address 0x", "The crash line should name the signal");
#endif
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_CrashProcess_TerminateThenAbortWritesOneCrashLine()
{
    // Act
    const int exitCode = RunCrashHelper("terminate_then_abort", TestTempDir / "terminate_then_abort_output.txt");

    // Assert: the terminate handler writes the line; the SIGABRT handler that follows passes the crash on without one
    const auto contents = ReadFileText(TestTempDir / "terminate_then_abort.log");
    CheckEquals(PassedOnAbortExitCode, exitCode, "The crash should be passed on to the abort handler installed before");
    CheckEquals(std::string("[INFO]: entry_1\n[CRITICAL]: Crash: std::terminate() called\n"), contents,
        "The crash should get one line, from the terminate handler");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_CrashProcess_TerminateWritesTheExceptionAndFlushes()
{
    // Act
    const int exitCode = RunCrashHelper("terminate", TestTempDir / "terminate_output.txt");

    // Assert
    CheckEquals(PassedOnTerminateExitCode, exitCode, "The crash should be passed on to the terminate handler installed before");
    CheckEquals(std::string("[INFO]: entry_1\n[INFO]: entry_2\n[CRITICAL]: Crash: std::terminate() called, exception: helper exception\n"),
        ReadFileText(TestTempDir / "terminate.log"), "The buffered entries should be flushed, then the formatted crash line written");
    CheckEquals(std::string("[INFO]: other_entry\n"), ReadFileText(TestTempDir / "terminate_other.log"),
        "A logger with WriteCrashLine false should be flushed, without the crash line");
    CheckNotContains(ReadFileText(TestTempDir / "terminate_output.txt"), "Sanitizer", "No sanitizer report");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_HandleCrash_ConsoleLogWritesTheLineToStdErr()
{
    // Arrange
    ASWLog::TASWConsoleLog log;
    log.SetColorMode(ASWLog::ColorMode::Never);
    CheckTrue(log.Initialize(MakeCrashTestConfig("unused.log")), "Initialize should succeed");
    std::ostringstream capturedOut;
    std::ostringstream capturedErr;
    auto* const previousOut = std::cout.rdbuf(capturedOut.rdbuf());
    auto* const previousErr = std::cerr.rdbuf(capturedErr.rdbuf());

    // Act
    ASWLog::HandleCrash("test crash");
    std::cout.rdbuf(previousOut);
    std::cerr.rdbuf(previousErr);

    // Assert
    CheckEquals(std::string("[CRITICAL]: Crash: test crash\n"), capturedErr.str(), "The crash line should go to stderr");
    CheckEquals(std::string(), capturedOut.str(), "Nothing should go to stdout");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_HandleCrash_DisabledLoggerIsOnlyFlushed()
{
    // Arrange
    ASWLog::TASWFileLog log;
    CheckTrue(log.Initialize(MakeCrashTestConfig("disabled.log")), "Initialize should succeed");
    log.LogInfo("entry_1");
    log.SetEnabled(false);

    // Act
    ASWLog::HandleCrash("test crash");

    // Assert
    CheckEquals(std::string("[INFO]: entry_1\n"), ReadFileText(TestTempDir / "disabled.log"),
        "A disabled logger should be flushed, without the crash line");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_HandleCrash_FlushesAndWritesTheCrashLine()
{
    // Arrange
    ASWLog::TASWFileLog log;
    CheckTrue(log.Initialize(MakeCrashTestConfig("crash.log")), "Initialize should succeed");
    log.LogInfo("entry_1");
    const auto before = ReadFileText(TestTempDir / "crash.log");

    // Act
    ASWLog::HandleCrash("test crash");

    // Assert: read while the logger is still open
    CheckEquals(std::string(), before, "The entry should still be buffered before the crash");
    CheckEquals(std::string("[INFO]: entry_1\n[CRITICAL]: Crash: test crash\n"), ReadFileText(TestTempDir / "crash.log"),
        "The buffered entry should be flushed, then the formatted crash line written and flushed");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_HandleCrash_JSONLoggerGetsJSONLinesWhenTheLockStaysBusy()
{
    // Arrange: a JSON logger with a backtrace entry (HandleCrashWhileLocked() holds its lock on another thread meanwhile)
    auto config = MakeCrashTestConfig("busy.json");
    config.Backtrace.Capacity = 5;
    config.Backtrace.LowestLevel = ASWLog::Level::Debug;
    config.Line.Formatter = std::make_shared<const ASWLog::TASWJSONFormatter>();
    TLockableFileLog log;
    CheckTrue(log.Initialize(config), "Initialize should succeed");
    log.LogDebug("debug_1");

    // Act
    const auto elapsed = HandleCrashWhileLocked(log, "busy \"json\"");
    const auto contents = ReadFileText(TestTempDir / "busy.json");

    // Assert
    std::vector<std::string> lines;

    for (std::size_t start = 0; start < contents.size();)
    {
        const auto end = contents.find('\n', start);
        lines.push_back(contents.substr(start, end - start));
        start = end == std::string::npos ? contents.size() : end + 1;
    }

    CheckLessThan(elapsed.count(), 5000, "HandleCrash() should give up waiting for the lock");
    CheckEndsWith(contents, "}\n", "The file should end with a whole line");
    AssertEquals(std::size_t(4), lines.size(), "The backtrace's markers, its entry and the crash line should be written");

    for (const auto& line : lines)
        CheckTrue(IsJSONObjectLine(line), "Each line should be one JSON object: " + line);

    const auto ids = std::format(R"("pid":{},"tid":{},)", ASWLog::GetCurrentOSProcessId(), ASWLog::GetCurrentOSThreadId());
    CheckEndsWith(lines[0], R"("level":"INFO",)" + ids + R"("message":"Backtrace: the last 1 entry below the minimum level"})",
        "The backtrace should start with its marker, in the fixed JSON layout");
    CheckEndsWith(lines[1], R"("level":"DEBUG",)" + ids + R"("message":"debug_1"})", "The kept entry should follow");
    CheckEndsWith(lines[2], R"("level":"INFO",)" + ids + R"("message":"Backtrace end"})", "Then the end marker");
    CheckEndsWith(lines[3], R"("level":"CRITICAL",)" + ids + R"("message":"Crash: busy \"json\""})", "Then the crash line, escaped");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_HandleCrash_SkipsClosedAndDestroyedLoggers()
{
    // Arrange: one logger closed, one destroyed (a crash handler must not touch it)
    ASWLog::TASWFileLog closedLog;
    CheckTrue(closedLog.Initialize(MakeCrashTestConfig("closed.log")), "Initialize should succeed");
    closedLog.LogInfo("entry_1");
    closedLog.Close();

    {
        ASWLog::TASWFileLog destroyedLog;
        CheckTrue(destroyedLog.Initialize(MakeCrashTestConfig("destroyed.log")), "Initialize should succeed");
        destroyedLog.LogInfo("entry_1");
    }

    // Act
    ASWLog::HandleCrash("test crash");

    // Assert
    CheckEquals(std::string("[INFO]: entry_1\n"), ReadFileText(TestTempDir / "closed.log"), "A closed logger should get no crash line");
    CheckEquals(std::string("[INFO]: entry_1\n"), ReadFileText(TestTempDir / "destroyed.log"), "A destroyed logger should get no crash line");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_HandleCrash_SyncsTheLineAtSyncToDiskAtLevel()
{
    // Arrange: the null device takes every write, but can't be synced to disk (see the FileLog OnError tests), so a
    // sync shows as a SyncFailed report. A crash reports nothing itself; Close() passes on what it found.
    std::vector<ASWLog::ErrorKind> reports;
    const auto makeConfig = [&reports](ASWLog::Level syncLevel) {
            auto config = MakeCrashTestConfig("unused.log");
#if defined(_WIN32)
            config.File.FilePath = "NUL"; // A device name in any folder
#else
            config.File.FilePath = "/dev/null"; // Absolute, so it replaces the folder
#endif
            config.File.SyncToDiskAtLevel = syncLevel;
            config.ErrorReportInterval = std::chrono::milliseconds(0);
            config.OnError = [&reports](const ASWLog::TASWLogError& error) {
                    reports.push_back(error.Kind);
                };
            return config;
        };

    ASWLog::TASWFileLog syncingLog;
    CheckTrue(syncingLog.Initialize(makeConfig(ASWLog::Level::Critical)), "Initialize should succeed");
    ASWLog::TASWFileLog otherLog;
    CheckTrue(otherLog.Initialize(makeConfig(ASWLog::Level::Off)), "Initialize should succeed");
    syncingLog.LogInfo("entry_1"); // Below the level, so not synced

    // Act
    ASWLog::HandleCrash("test crash");
    otherLog.Close();
    const auto reportsWithoutSync = reports.size();
    syncingLog.Close();

    // Assert
    CheckEquals(0, reportsWithoutSync, "A logger whose SyncToDiskAtLevel is above Critical shouldn't sync the crash line");
    CheckEquals(1, reports.size(), "The crash line should be synced once");
    if (reports.size() == 1)
        CheckEquals(ASWLog::ErrorKind::SyncFailed, reports[0], "The report should be SyncFailed");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_HandleCrash_ThrowingFormatterGetsTheFixedLayoutLine()
{
    // Arrange
    auto config = MakeCrashTestConfig("throwing.log");
    config.Line.Formatter = std::make_shared<TCrashLineThrowingFormatter>();
    ASWLog::TASWFileLog log;
    CheckTrue(log.Initialize(config), "Initialize should succeed");
    log.LogInfo("entry_1");

    // Act
    ASWLog::HandleCrash("test crash");

    // Assert: the buffered entry is flushed before the line is formatted, so it comes first
    const auto contents = ReadFileText(TestTempDir / "throwing.log");
    CheckStartsWith(contents, "[INFO]: entry_1\n[", "The buffered entry should be flushed before the crash line");
    CheckEndsWith(contents, std::format("][CRITICAL][P:{}][T:{}]: Crash: test crash\n", ASWLog::GetCurrentOSProcessId(),
        ASWLog::GetCurrentOSThreadId()), "The fixed-layout line should replace the line the formatter failed to make");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_HandleCrash_WaitsForQueuedEntries()
{
    // Arrange: the worker pauses after writing entry_1, and entry_2 and entry_3 are queued meanwhile
    std::mutex mutex;
    std::condition_variable changed;
    bool isPausing = false;
    auto config = MakeCrashTestConfig("async.log");
    config.Async.Enabled = true;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Info;
    config.OnLogEntry = [&](const ASWLog::TASWLogRecord& record, std::string_view /*line*/) {
            if (record.Message != "entry_1")
                return;

            {
                std::lock_guard<std::mutex> lock(mutex);
                isPausing = true;
            }

            changed.notify_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        };

    ASWLog::TASWFileLog log;
    CheckTrue(log.Initialize(config), "Initialize should succeed");
    log.LogInfo("entry_1");
    {
        std::unique_lock<std::mutex> lock(mutex);
        CheckTrue(changed.wait_for(lock, WaitTimeout, [&] {
                return isPausing;
            }), "The worker should write entry_1");
    }

    log.LogInfo("entry_2");
    log.LogInfo("entry_3");

    // Act
    ASWLog::HandleCrash("test crash");
    log.Close();

    // Assert
    CheckEquals(std::string("[INFO]: entry_1\n[INFO]: entry_2\n[INFO]: entry_3\n[CRITICAL]: Crash: test crash\n"),
        ReadFileText(TestTempDir / "async.log"), "The queued entries should be written before the crash line");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_HandleCrash_WriteCrashLineFalseOnlyFlushes()
{
    // Arrange
    auto config = MakeCrashTestConfig("no_line.log");
    config.Shutdown.WriteCrashLine = false;
    ASWLog::TASWFileLog log;
    CheckTrue(log.Initialize(config), "Initialize should succeed");
    log.LogInfo("entry_1");

    // Act
    ASWLog::HandleCrash("test crash");

    // Assert
    CheckEquals(std::string("[INFO]: entry_1\n"), ReadFileText(TestTempDir / "no_line.log"),
        "The logger should be flushed, without the crash line");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_HandleCrash_WritesTheBacktraceBeforeTheCrashLine()
{
    // Arrange
    auto config = MakeCrashTestConfig("backtrace.log");
    config.Backtrace.Capacity = 5;
    config.Backtrace.LowestLevel = ASWLog::Level::Debug;
    ASWLog::TASWFileLog log;
    CheckTrue(log.Initialize(config), "Initialize should succeed");
    log.LogDebug("debug_1");
    log.LogInfo("info_1");

    // Act
    ASWLog::HandleCrash("test crash");
    const auto afterCrash = ReadFileText(TestTempDir / "backtrace.log");
    log.LogError("error_1");
    log.Close();

    // Assert
    CheckEquals(std::string("[INFO]: info_1\n[INFO]: Backtrace: the last 1 entry below the minimum level\n[DEBUG]: debug_1\n"
        "[INFO]: Backtrace end\n[CRITICAL]: Crash: test crash\n"), afterCrash,
        "The backtrace should be formatted and written between the flushed entries and the crash line");
    CheckEndsWith(ReadFileText(TestTempDir / "backtrace.log"), "[CRITICAL]: Crash: test crash\n[ERROR]: error_1\n",
        "The written backtrace should be forgotten");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_HandleCrash_WritesTheLineDirectlyWhenTheLockStaysBusy()
{
    // Arrange: a backtrace entry (HandleCrashWhileLocked() holds the logger's lock on another thread meanwhile)
    auto config = MakeCrashTestConfig("busy.log");
    config.Line.Ending = ASWLog::LineEnding::CRLF;
    config.Backtrace.Capacity = 5;
    config.Backtrace.LowestLevel = ASWLog::Level::Debug;
    TLockableFileLog log;
    CheckTrue(log.Initialize(config), "Initialize should succeed");
    log.LogDebug("debug_1");

    // Act
    const auto elapsed = HandleCrashWhileLocked(log, "busy");
    const auto contents = ReadFileText(TestTempDir / "busy.log");

    // Assert
    CheckLessThan(elapsed.count(), 5000, "HandleCrash() should give up waiting for the lock");
    CheckStartsWith(contents, "[", "The crash line should start with the time");
    CheckEndsWith(contents, std::format("][CRITICAL][P:{}][T:{}]: Crash: busy\r\n", ASWLog::GetCurrentOSProcessId(),
        ASWLog::GetCurrentOSThreadId()), "The fixed-layout crash line should be written straight to the file");
    const auto ids = std::format("[P:{}][T:{}]: ", ASWLog::GetCurrentOSProcessId(), ASWLog::GetCurrentOSThreadId());
    const auto beginAt = contents.find("][INFO]" + ids + "Backtrace: the last 1 entry below the minimum level\r\n");
    const auto entryAt = contents.find("][DEBUG]" + ids + "debug_1\r\n");
    const auto endAt = contents.find("][INFO]" + ids + "Backtrace end\r\n");
    CheckTrue(beginAt != std::string::npos && beginAt < entryAt && entryAt < endAt && endAt != std::string::npos,
        "The backtrace should be written straight to the file too, in the fixed layout, before the crash line");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CrashHandler::Test_InstallCrashHandlers_ChainsAndUninstallRestores()
{
    // Arrange: the handlers in place before
    const auto previousTerminate = std::set_terminate(ExitAsPassedOn);
#if defined(_WIN32)
    const auto previousFilter = SetUnhandledExceptionFilter(ExitAsPassedOnFromFilter);
    const auto previousAbortHandler = std::signal(SIGABRT, ExitAsPassedOnFromSignal);
    const auto currentFilter = [] {
            const auto filter = SetUnhandledExceptionFilter(nullptr);
            SetUnhandledExceptionFilter(filter);
            return filter;
        };
    const auto currentAbortHandler = [] {
            const auto handler = std::signal(SIGABRT, SIG_DFL);
            std::signal(SIGABRT, handler);
            return handler;
        };
#else
    struct sigaction passOnAction {};
    passOnAction.sa_sigaction = ExitAsPassedOnFromSignal;
    passOnAction.sa_flags = SA_SIGINFO;
    sigemptyset(&passOnAction.sa_mask);
    struct sigaction previousSegvAction {};
    sigaction(SIGSEGV, &passOnAction, &previousSegvAction);
    const auto isSegvPassOn = [] {
            struct sigaction current {};
            sigaction(SIGSEGV, nullptr, &current);
            return (current.sa_flags & SA_SIGINFO) != 0 && current.sa_sigaction == ExitAsPassedOnFromSignal;
        };
#endif

    // Act
    const bool isInstalled = ASWLog::InstallCrashHandlers();
    const bool isTerminateReplaced = std::get_terminate() != ExitAsPassedOn;
#if defined(_WIN32)
    const bool areOthersReplaced = currentFilter() != ExitAsPassedOnFromFilter && currentAbortHandler() != ExitAsPassedOnFromSignal;
#else
    const bool areOthersReplaced = !isSegvPassOn();
#endif
    const bool isInstalledAgain = ASWLog::InstallCrashHandlers(); // Must not chain to itself
    ASWLog::UninstallCrashHandlers();
    const bool isTerminateRestored = std::get_terminate() == ExitAsPassedOn;
#if defined(_WIN32)
    const bool areOthersRestored = currentFilter() == ExitAsPassedOnFromFilter && currentAbortHandler() == ExitAsPassedOnFromSignal;

    std::signal(SIGABRT, previousAbortHandler);
    SetUnhandledExceptionFilter(previousFilter);
#else
    const bool areOthersRestored = isSegvPassOn();

    sigaction(SIGSEGV, &previousSegvAction, nullptr);
#endif
    std::set_terminate(previousTerminate);

    // Assert
    CheckTrue(isInstalled, "InstallCrashHandlers() should succeed");
    CheckTrue(isInstalledAgain, "Calling it again should succeed");
    CheckTrue(isTerminateReplaced, "It should install a terminate handler");
    CheckTrue(areOthersReplaced, "It should install the fault and abort handlers");
    CheckTrue(isTerminateRestored, "UninstallCrashHandlers() should restore the terminate handler from before");
    CheckTrue(areOthersRestored, "UninstallCrashHandlers() should restore the fault and abort handlers from before");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_CrashHandler)
