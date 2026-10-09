/* **************************************************************************
ASWLog_CrashHandler.cpp
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
#include "ASWLog_CrashHandler.h"
//---------------------------------------------------------------------------
// System includes here
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <new>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#undef min
#undef max
#else
#include <cerrno>
#include <signal.h>
#include <unistd.h>
#endif
//---------------------------------------------------------------------------
#include "ASWLog_TextLogBase.h"
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

// The longest TASWCrashHandlerOptions::WaitTimeout used, so the deadline arithmetic can't overflow
constexpr std::chrono::milliseconds MaxWaitTimeout = std::chrono::hours(1);

// The loggers a crash flushes (see Detail::TCrashHandling), as an intrusive list, so adding one never allocates
struct TCrashRegistry
{
    std::mutex Mutex;
    TASWTextLogBase* First = nullptr;
};

#if defined(_WIN32)
using TSignalHandler = void (*)(int);
#else
// A fatal signal the handlers catch, and the action that was installed for it before
struct TSignalSlot
{
    int Signal = 0;
    struct sigaction Previous {};
    bool IsInstalled = false;
};

// The alternate signal stack InstallCrashHandlers() gives its thread, so that a stack overflow's SIGSEGV can be handled
alignas(16) char AlternateSignalStack[64 * 1024];
#endif

// What InstallCrashHandlers() installed, and the handlers it replaced. Changed with InstallMutex held; the handlers
// read it.
struct TCrashHandlerState
{
    std::mutex InstallMutex;
    std::atomic<bool> IsActive{ false }; // Installed, and not uninstalled since
    std::atomic<bool> IsCrashHandled{ false }; // A handler (or HandleCrash()) has written the crash to the loggers
    std::atomic<std::int64_t> WaitTimeoutMilliseconds{ 1000 };
    std::terminate_handler PreviousTerminate = nullptr;
    bool IsTerminateInstalled = false;
#if defined(_WIN32)
    LPTOP_LEVEL_EXCEPTION_FILTER PreviousFilter = nullptr;
    bool IsFilterInstalled = false;
    TSignalHandler PreviousAbortHandler = SIG_DFL;
    bool IsAbortHandlerInstalled = false;
#else
    std::array<TSignalSlot, 5> Signals{ { { SIGSEGV }, { SIGBUS }, { SIGFPE }, { SIGILL }, { SIGABRT } } };
    bool IsAlternateStackInstalled = false;
#endif
};

std::chrono::milliseconds ClampWaitTimeout(std::chrono::milliseconds waitTimeout) noexcept
{
    return std::clamp(waitTimeout, std::chrono::milliseconds(0), MaxWaitTimeout);
}

// The state, never destroyed (the loggers' Finalize() at exit, e.g. a GetInstance() logger's, can come after static
// destruction) and built in static storage, so its first use can't fail
TCrashHandlerState& GetCrashHandlerState() noexcept
{
    alignas(TCrashHandlerState) static unsigned char storage[sizeof(TCrashHandlerState)];
    static TCrashHandlerState* const state = new (storage) TCrashHandlerState();
    return *state;
}

// The list of loggers, never destroyed, like the state (see GetCrashHandlerState())
TCrashRegistry& GetCrashRegistry() noexcept
{
    alignas(TCrashRegistry) static unsigned char storage[sizeof(TCrashRegistry)];
    static TCrashRegistry* const registry = new (storage) TCrashRegistry();
    return *registry;
}

// Writes the crash to the loggers if the handlers are active and no handler has written one yet: one crash can reach
// several handlers (e.g. std::terminate()'s default handler calls abort(), which raises SIGABRT).
void HandleFirstCrash(std::string_view message, bool isInSignalHandler) noexcept
{
    auto& state = GetCrashHandlerState();
    if (!state.IsActive.load() || state.IsCrashHandled.exchange(true))
        return;

    const std::chrono::milliseconds waitTimeout(state.WaitTimeoutMilliseconds.load());
    Detail::TCrashHandling::HandleCrashInLoggers(message, isInSignalHandler, waitTimeout);
}

// The std::terminate() handler: writes the crash with the current exception's what(), if any, then calls the handler
// installed before (by default, it calls abort())
[[noreturn]] void OnTerminate() noexcept
{
    Detail::TCrashText message;
    message.Append("Crash: std::terminate() called");

    if (const auto exception = std::current_exception())
    {
        try
        {
            std::rethrow_exception(exception);
        }
        catch (const std::exception& error)
        {
            message.Append(", exception: ");
            message.Append(error.what());
        }
        catch (...)
        {
            message.Append(", exception of an unknown type");
        }
    }

    HandleFirstCrash(message.View(), false);

    const auto previous = GetCrashHandlerState().PreviousTerminate;
    if (previous != nullptr)
        previous();

    std::abort();
}

#if defined(_WIN32)

// The name of a structured exception code, or empty for an unusual one
std::string_view GetExceptionName(DWORD code) noexcept
{
    // Raised for a language exception: by the Delphi RTL (which in a VCL or FMX application also turns a fault such
    // as an access violation into one), and by MSVC for a C++ exception
    constexpr DWORD DelphiExceptionCode = 0x0EEDFADE;
    constexpr DWORD MSVCExceptionCode = 0xE06D7363;

    switch (code)
    {
        case DelphiExceptionCode:
            return "Delphi exception";

        case MSVCExceptionCode:
            return "C++ exception";

        case EXCEPTION_ACCESS_VIOLATION:
            return "access violation";

        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            return "array bounds exceeded";

        case EXCEPTION_BREAKPOINT:
            return "breakpoint";

        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return "datatype misalignment";

        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            return "floating-point division by zero";

        case EXCEPTION_FLT_INVALID_OPERATION:
            return "floating-point invalid operation";

        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "illegal instruction";

        case EXCEPTION_IN_PAGE_ERROR:
            return "in-page error";

        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "integer division by zero";

        case EXCEPTION_INT_OVERFLOW:
            return "integer overflow";

        case EXCEPTION_PRIV_INSTRUCTION:
            return "privileged instruction";

        case EXCEPTION_STACK_OVERFLOW:
            return "stack overflow";

        default:
            return {};
    }
}

// "Crash: unhandled exception 0xC0000005 (access violation writing address 0x...) at 0x..."
void AppendExceptionMessage(Detail::TCrashText& message, const EXCEPTION_RECORD& record) noexcept
{
    message.Append("Crash: unhandled exception ");
    message.AppendHex(record.ExceptionCode, 8);

    const auto name = GetExceptionName(record.ExceptionCode);
    if (!name.empty())
    {
        message.Append(" (");
        message.Append(name);

        // The first parameter tells what the access was: 0 read, 1 write, 8 execute (data execution prevention)
        if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
        {
            const auto access = record.ExceptionInformation[0];
            message.Append(access == 0 ? " reading" : access == 1 ? " writing" : " executing");
            message.Append(" address ");
            message.AppendHex(record.ExceptionInformation[1], 16);
        }

        message.Append(")");
    }

    message.Append(" at ");
    message.AppendHex(reinterpret_cast<std::uintptr_t>(record.ExceptionAddress), 16);
}

// Writes the crash for a structured exception, on a new thread (see OnUnhandledException())
DWORD WINAPI HandleExceptionOnThread(void* parameter)
{
    Detail::TCrashText message;
    AppendExceptionMessage(message, *static_cast<const EXCEPTION_RECORD*>(parameter));
    HandleFirstCrash(message.View(), false);

    return 0;
}

// The SIGABRT handler (abort(), a failed assert(), std::terminate()'s default handler). The C library sets SIGABRT
// back to its default before calling it, and ends the process once it returns.
void OnAbortSignal(int signalNumber)
{
    HandleFirstCrash("Crash: SIGABRT (abort() called)", false);

    const auto previous = GetCrashHandlerState().PreviousAbortHandler;
    if (previous != nullptr && previous != SIG_DFL && previous != SIG_IGN && previous != SIG_ERR)
        previous(signalNumber);
}

// The unhandled exception filter: writes the crash, then passes the exception on to the filter installed before, or
// to the system (an error report, then the process ends)
LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* exceptionInfo)
{
    auto& state = GetCrashHandlerState();

    if (exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr)
    {
        const auto& record = *exceptionInfo->ExceptionRecord;
        if (record.ExceptionCode == EXCEPTION_STACK_OVERFLOW)
        {
            // This thread has hardly any stack left, so another one does the work, while this one waits a little
            // longer than the wait timeout for it. It gets a copy of the record, which it may still read after this
            // returns.
            static EXCEPTION_RECORD stackOverflowRecord;
            stackOverflowRecord = record;

            const auto thread = CreateThread(nullptr, 0, HandleExceptionOnThread, &stackOverflowRecord, 0, nullptr);
            if (thread != nullptr)
            {
                const auto waitMilliseconds = state.WaitTimeoutMilliseconds.load() + 1000;
                WaitForSingleObject(thread, static_cast<DWORD>(waitMilliseconds));
                CloseHandle(thread);
            }
        }
        else
        {
            Detail::TCrashText message;
            AppendExceptionMessage(message, record);
            HandleFirstCrash(message.View(), false);
        }
    }

    return state.PreviousFilter != nullptr ? state.PreviousFilter(exceptionInfo) : EXCEPTION_CONTINUE_SEARCH;
}

#else

std::string_view GetSignalName(int signalNumber) noexcept
{
    switch (signalNumber)
    {
        case SIGABRT:
            return "SIGABRT";

        case SIGBUS:
            return "SIGBUS";

        case SIGFPE:
            return "SIGFPE";

        case SIGILL:
            return "SIGILL";

        case SIGSEGV:
            return "SIGSEGV";

        default:
            return "signal";
    }
}

// The fatal signal handler: writes the crash with async-signal-safe work only (see ASWLog_CrashHandler.h), then
// passes the signal on to the action installed before it, or the default one (e.g. a core dump)
void OnFatalSignal(int signalNumber, siginfo_t* info, void* /*context*/)
{
    const int savedErrno = errno;
    auto& state = GetCrashHandlerState();

    Detail::TCrashText message;
    message.Append("Crash: ");
    message.Append(GetSignalName(signalNumber));

    if (signalNumber == SIGABRT)
    {
        message.Append(" (abort() called)");
    }
    else if (info != nullptr && info->si_code > 0)
    {
        message.Append(" (address ");
        message.AppendHex(reinterpret_cast<std::uintptr_t>(info->si_addr), 16);
        message.Append(")");
    }

    HandleFirstCrash(message.View(), true);

    const auto slot = std::find_if(state.Signals.begin(), state.Signals.end(), [signalNumber](const TSignalSlot& candidate) {
                return candidate.Signal == signalNumber;
            });

    if (slot != state.Signals.end())
        sigaction(signalNumber, &slot->Previous, nullptr);
    else
        signal(signalNumber, SIG_DFL);

    errno = savedErrno;

    // A fault (si_code above 0) happens again when this returns, and reaches that action then. A signal that was
    // sent (raise(), abort(), kill()) is sent again: blocked while this runs, it arrives once this returns.
    if (info == nullptr || info->si_code <= 0)
        raise(signalNumber);
}

#endif

} // namespace

//---------------------------------------------------------------------------
void HandleCrash(std::string_view reason, const TASWCrashHandlerOptions& options) noexcept
{
    // The handlers write nothing after this, so a crash that the application's own crash reporter reports through
    // here and that also reaches them gets one line
    GetCrashHandlerState().IsCrashHandled.store(true);

    Detail::TCrashText message;
    message.Append("Crash: ");
    message.Append(reason);
    Detail::TCrashHandling::HandleCrashInLoggers(message.View(), false, options.WaitTimeout);
}

//---------------------------------------------------------------------------
bool InstallCrashHandlers(const TASWCrashHandlerOptions& options) noexcept
{
    try
    {
        GetCrashRegistry(); // Built now, rather than first in a crash

        auto& state = GetCrashHandlerState();
        std::lock_guard<std::mutex> lock(state.InstallMutex);
        state.WaitTimeoutMilliseconds.store(ClampWaitTimeout(options.WaitTimeout).count());
        bool isInstalled = true;

        if (!state.IsTerminateInstalled)
        {
            state.PreviousTerminate = std::set_terminate(OnTerminate);
            state.IsTerminateInstalled = true;
        }

#if defined(_WIN32)
        if (!state.IsFilterInstalled)
        {
            state.PreviousFilter = SetUnhandledExceptionFilter(OnUnhandledException);
            state.IsFilterInstalled = true;
        }

        if (!state.IsAbortHandlerInstalled)
        {
            const auto previous = std::signal(SIGABRT, OnAbortSignal);
            if (previous != SIG_ERR)
            {
                state.PreviousAbortHandler = previous;
                state.IsAbortHandlerInstalled = true;
            }
            else
            {
                isInstalled = false;
            }
        }
#else
        // Only if the thread has none yet (e.g. a sanitizer may have given it one)
        stack_t currentStack{};
        if (!state.IsAlternateStackInstalled && sigaltstack(nullptr, &currentStack) == 0 && (currentStack.ss_flags & SS_DISABLE) != 0)
        {
            stack_t alternateStack{};
            alternateStack.ss_sp = AlternateSignalStack;
            alternateStack.ss_size = sizeof(AlternateSignalStack);
            if (sigaltstack(&alternateStack, nullptr) == 0)
                state.IsAlternateStackInstalled = true;
        }

        for (auto& slot : state.Signals)
        {
            if (slot.IsInstalled)
                continue;

            struct sigaction action {};
            action.sa_sigaction = OnFatalSignal;
            action.sa_flags = SA_SIGINFO | SA_ONSTACK;
            sigemptyset(&action.sa_mask);
            if (sigaction(slot.Signal, &action, &slot.Previous) == 0)
                slot.IsInstalled = true;
            else
                isInstalled = false;
        }
#endif

        state.IsActive.store(true);

        return isInstalled;
    }
    catch (...)
    {
        return false;
    }
}

//---------------------------------------------------------------------------
void UninstallCrashHandlers() noexcept
{
    try
    {
        auto& state = GetCrashHandlerState();
        std::lock_guard<std::mutex> lock(state.InstallMutex);
        state.IsActive.store(false);

        // A handler replaced since stays: the one that replaced it may pass crashes on to it
        if (state.IsTerminateInstalled && std::get_terminate() == OnTerminate)
        {
            std::set_terminate(state.PreviousTerminate);
            state.IsTerminateInstalled = false;
        }

#if defined(_WIN32)
        if (state.IsFilterInstalled)
        {
            const auto current = SetUnhandledExceptionFilter(state.PreviousFilter);
            if (current == OnUnhandledException)
                state.IsFilterInstalled = false;
            else
                SetUnhandledExceptionFilter(current);
        }

        if (state.IsAbortHandlerInstalled)
        {
            const auto current = std::signal(SIGABRT, state.PreviousAbortHandler);
            if (current == OnAbortSignal)
                state.IsAbortHandlerInstalled = false;
            else if (current != SIG_ERR)
                std::signal(SIGABRT, current);
        }
#else
        for (auto& slot : state.Signals)
        {
            struct sigaction current {};
            if (slot.IsInstalled && sigaction(slot.Signal, nullptr, &current) == 0 && (current.sa_flags & SA_SIGINFO) != 0 &&
                current.sa_sigaction == OnFatalSignal && sigaction(slot.Signal, &slot.Previous, nullptr) == 0)
            {
                slot.IsInstalled = false;
            }
        }

        // Only on the thread that got it, and not while it is in use
        stack_t currentStack{};
        if (state.IsAlternateStackInstalled && sigaltstack(nullptr, &currentStack) == 0 &&
            currentStack.ss_sp == AlternateSignalStack && (currentStack.ss_flags & SS_ONSTACK) == 0)
        {
            stack_t disabledStack{};
            disabledStack.ss_flags = SS_DISABLE;
            if (sigaltstack(&disabledStack, nullptr) == 0)
                state.IsAlternateStackInstalled = false;
        }
#endif
    }
    catch (...)
    {
    }
}

//---------------------------------------------------------------------------

namespace Detail
{

/////////////////////////////////////////////////////////////////////////////
// TCrashText
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
void TCrashText::Append(std::string_view text) noexcept
{
    const auto count = std::min(text.size(), Capacity - m_Size);
    std::copy_n(text.data(), count, m_Text + m_Size);
    m_Size += count;
}

//---------------------------------------------------------------------------
void TCrashText::AppendDecimal(std::uint64_t value, int minDigits) noexcept
{
    char digits[20];
    const auto result = std::to_chars(digits, digits + sizeof(digits), value);
    const auto count = static_cast<int>(result.ptr - digits);

    for (int padding = count; padding < minDigits; ++padding)
        Append("0");

    Append(std::string_view(digits, static_cast<std::size_t>(count)));
}

//---------------------------------------------------------------------------
void TCrashText::AppendHex(std::uint64_t value, int minDigits) noexcept
{
    constexpr std::string_view HexDigits = "0123456789ABCDEF";
    char digits[16];
    std::size_t count = 0;

    // From the last digit back, at least one and at most all 16
    do
    {
        digits[sizeof(digits) - 1 - count] = HexDigits[value & 0xF];
        value >>= 4;
        ++count;
    } while (value != 0);

    while (count < sizeof(digits) && count < static_cast<std::size_t>(std::max(minDigits, 0)))
    {
        digits[sizeof(digits) - 1 - count] = '0';
        ++count;
    }

    Append("0x");
    Append(std::string_view(digits + sizeof(digits) - count, count));
}

//---------------------------------------------------------------------------
void TCrashText::AppendISO8601(std::chrono::system_clock::time_point time) noexcept
{
    // Doesn't allocate (unlike Time::ToISO8601String())
    char buffer[Time::ISO8601BufferSize];
    Append(std::string_view(buffer, Time::WriteISO8601(buffer, time)));
}

//---------------------------------------------------------------------------
void TCrashText::AppendJSONString(std::string_view text, std::size_t reserved) noexcept
{
    // The text ends before the closing quote and 'reserved'
    const std::size_t limit = Capacity - std::min(Capacity, reserved + 1);
    char escape[6];
    std::size_t index = 0;

    Append("\"");

    while (index < text.size())
    {
        const auto piece = NextJSONPiece(text, index, escape);
        const std::size_t room = limit > m_Size ? limit - m_Size : 0;
        if (piece.Text.size() <= room)
        {
            Append(piece.Text);
            continue;
        }

        // A run of plain characters is cut between UTF-8 sequences; an escape or U+FFFD is left out whole
        if (!piece.IsEscape)
        {
            std::size_t count = room;
            while (count > 0 && (static_cast<unsigned char>(piece.Text[count]) & 0xC0) == 0x80)
                --count;

            Append(piece.Text.substr(0, count));
        }

        break;
    }

    Append("\"");
}

//---------------------------------------------------------------------------
std::size_t TCrashText::GetSize() const noexcept
{
    return m_Size;
}

//---------------------------------------------------------------------------
void TCrashText::Truncate(std::size_t size) noexcept
{
    m_Size = std::min(m_Size, size);
}

//---------------------------------------------------------------------------
std::string_view TCrashText::View() const noexcept
{
    return std::string_view(m_Text, m_Size);
}

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TCrashHandling
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
void TCrashHandling::HandleCrashInLoggers(std::string_view message, bool isInSignalHandler,
    std::chrono::milliseconds waitTimeout) noexcept
{
    const auto deadline = std::chrono::steady_clock::now() + ClampWaitTimeout(waitTimeout);
    auto& registry = GetCrashRegistry();

    // Polled, since the crashing thread may hold it (e.g. it crashed while a logger was being initialized)
    while (!registry.Mutex.try_lock())
    {
        if (!PauseBeforeDeadline(deadline))
            return;
    }

    std::lock_guard<std::mutex> lock(registry.Mutex, std::adopt_lock);
    for (auto* log = registry.First; log != nullptr; log = log->m_CrashListNext)
        log->OnCrash(message, isInSignalHandler, deadline);
}

//---------------------------------------------------------------------------
void TCrashHandling::Register(TASWTextLogBase& log) noexcept
{
    auto& registry = GetCrashRegistry();
    std::lock_guard<std::mutex> lock(registry.Mutex);

    if (log.m_IsOnCrashList)
        return;

    log.m_CrashListPrevious = nullptr;
    log.m_CrashListNext = registry.First;

    if (registry.First != nullptr)
        registry.First->m_CrashListPrevious = &log;

    registry.First = &log;
    log.m_IsOnCrashList = true;
}

//---------------------------------------------------------------------------
void TCrashHandling::Unregister(TASWTextLogBase& log) noexcept
{
    auto& registry = GetCrashRegistry();
    std::lock_guard<std::mutex> lock(registry.Mutex);

    if (!log.m_IsOnCrashList)
        return;

    if (log.m_CrashListPrevious != nullptr)
        log.m_CrashListPrevious->m_CrashListNext = log.m_CrashListNext;
    else
        registry.First = log.m_CrashListNext;

    if (log.m_CrashListNext != nullptr)
        log.m_CrashListNext->m_CrashListPrevious = log.m_CrashListPrevious;

    log.m_CrashListPrevious = nullptr;
    log.m_CrashListNext = nullptr;
    log.m_IsOnCrashList = false;
}

//---------------------------------------------------------------------------
void AppendBacktraceBeginText(TCrashText& text, std::size_t count) noexcept
{
    text.Append("Backtrace: the last ");
    text.AppendDecimal(count);
    text.Append(count == 1 ? " entry" : " entries");
    text.Append(" below the minimum level");
}

//---------------------------------------------------------------------------
void AppendCrashJSONLine(TCrashText& line, const TASWLogRecord& record, bool usesCRLF) noexcept
{
    // The room kept after the category: the ids, "raw", an empty message, the closing brace and the line ending
    constexpr std::size_t CategoryReserved = 64;
    const std::string_view ending = usesCRLF ? "\r\n" : "\n";

    char digits[24];
    const auto sinceEpoch = std::chrono::floor<std::chrono::milliseconds>(record.Timestamp.time_since_epoch());
    const auto result = std::to_chars(digits, digits + sizeof(digits), static_cast<std::int64_t>(sinceEpoch.count()));

    line.Append("{\"time\":\"");
    line.AppendISO8601(record.Timestamp);
    line.Append("\",\"epoch_ms\":");
    line.Append(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
    line.Append(",\"level\":\"");
    line.Append(Level_ToString(record.LogLevel));
    line.Append("\"");

    if (!record.Category.empty())
    {
        line.Append(",\"category\":");
        line.AppendJSONString(record.Category, CategoryReserved);
    }

    line.Append(",\"pid\":");
    line.AppendDecimal(record.ProcessId);
    line.Append(",\"tid\":");
    line.AppendDecimal(record.ThreadId);

    if (record.Raw)
        line.Append(",\"raw\":true");

    line.Append(",\"message\":");
    line.AppendJSONString(record.Message, 1 + ending.size());
    line.Append("}");
    line.Append(ending);
}

//---------------------------------------------------------------------------
void AppendCrashLine(TCrashText& line, const TASWLogRecord& record, bool usesCRLF) noexcept
{
    const std::string_view ending = usesCRLF ? "\r\n" : "\n";

    line.Append("[");
    line.AppendISO8601(record.Timestamp);
    line.Append("][");
    line.Append(Level_ToString(record.LogLevel));

    if (!record.Category.empty())
    {
        line.Append("][");
        line.Append(record.Category);
    }

    line.Append("][P:");
    line.AppendDecimal(record.ProcessId);
    line.Append("][T:");
    line.AppendDecimal(record.ThreadId);
    line.Append("]: ");
    line.Append(record.Message);

    line.Truncate(TCrashText::Capacity - ending.size());
    line.Append(ending);
}

//---------------------------------------------------------------------------
bool PauseBeforeDeadline(std::chrono::steady_clock::time_point deadline) noexcept
{
    if (std::chrono::steady_clock::now() >= deadline)
        return false;

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return true;
}

//---------------------------------------------------------------------------

} // namespace Detail

} // namespace ASWLog
