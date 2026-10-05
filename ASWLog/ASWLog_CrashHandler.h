/* **************************************************************************
ASWLog_CrashHandler.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Requires C++ 20 or higher.

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

#pragma once

#ifndef ASWLog_CrashHandlerH
#define ASWLog_CrashHandlerH
//---------------------------------------------------------------------------
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_Types.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

class TASWTextLogBase;

/////////////////////////////////////////////////////////////////////////////
// TASWCrashHandlerOptions
//
// Settings for InstallCrashHandlers() and HandleCrash().
/////////////////////////////////////////////////////////////////////////////
struct TASWCrashHandlerOptions
{
    // How long a crash may wait in all, across every logger: for an asynchronous logger's thread to write the entries
    // queued before the crash (see TASWAsyncConfig), and for a logger's lock that another thread holds. Once it has
    // passed, the remaining loggers get only their crash line, written straight to their output. At most an hour.
    std::chrono::milliseconds WaitTimeout = std::chrono::seconds(1);
};

//---------------------------------------------------------------------------
// Crash handling
//
// When the application crashes, every initialized text logger (TASWTextLogBase: file, console, custom; a multi-log
// through its loggers) is flushed, after an asynchronous logger's thread has written what was queued, and gets a
// Critical "Crash: <reason>" line, unless its TASWShutdownConfig::WriteCrashLine is false or it is disabled
// (SetEnabled(false)). With the default FlushMode::EveryWrite every entry is in the file already, so the crash line
// is what is added; with the other flush modes and with asynchronous writing, the entries still in memory are saved
// too. A file logger syncs the line to disk if TASWFileConfig::SyncToDiskAtLevel is Critical or lower.
//
// All of this is best effort, since a crashed process may be in any state:
// - The line is formatted with the logger's formatter where that is reasonably safe (std::terminate, a Windows
//   unhandled exception, abort() on Windows, HandleCrash()). The buffered entries are flushed first, so a damaged heap
//   costs at most the line.
// - A POSIX signal handler may only do async-signal-safe work, so there the line has a fixed layout,
//   "[<UTC time>][CRITICAL][P:<pid>][T:<tid>]: Crash: <reason>", built without allocating and written straight to the
//   file. The buffered entries are flushed only if the logger's lock is free (fflush isn't on POSIX's list of
//   async-signal-safe functions, but no other thread can use the file then).
// - If a logger's lock stays busy until WaitTimeout has passed (e.g. the crash happened while that thread was
//   writing an entry), its buffered entries are lost and only the fixed-layout line is written, straight to the file.
// - Not handled: a crash while a debugger is attached (Windows passes the exception to the debugger instead), fast
//   fail (__fastfail, e.g. the /GS buffer check, control flow guard and the default invalid parameter handler on
//   Windows), a stack overflow on a POSIX thread other than the one that called InstallCrashHandlers() (only that one
//   gets an alternate signal stack), the process being killed (SIGKILL, TerminateProcess(), SIGTERM, Ctrl+C), and a
//   crash in a logger's own hooks while its lock is held (only the fixed-layout line is written then).
// - A logger with File.AutoOpenClosePerWrite is closed between entries, so a POSIX signal handler can't write its
//   crash line (it doesn't open files); the other handlers reopen it.
// - In a C++Builder application that uses the Delphi RTL (VCL, FMX), the RTL turns a fault into a Delphi exception
//   first, so the line names that ("unhandled exception 0x0EEDFADE (Delphi exception)"), and some faults (seen: a stack
//   overflow, and an access violation in a formatter that a logger called) end the process before any unhandled
//   exception filter runs. A VCL application usually reports exceptions in its event handlers itself (TApplication::
//   OnException), where it can log them.
//---------------------------------------------------------------------------

// Writes "Crash: <reason>" to every initialized text logger and flushes them, as a crash handler does (see above), in
// a normal (not POSIX signal handler) context. For an application whose own crash reporter (e.g. madExcept, EurekaLog,
// Crashpad) handles crashes: call it from there, instead of InstallCrashHandlers() or for the crashes it doesn't
// handle. Every call writes a line, but the handlers InstallCrashHandlers() installs write nothing after it, so a
// crash that both report gets one line. Never throws.
void HandleCrash(std::string_view reason, const TASWCrashHandlerOptions& options = {}) noexcept;

// Installs the process-wide crash handlers: std::set_terminate() (std::terminate() logs the current exception's what(),
// if any), SIGABRT (abort(), a failed assert()), and the fatal faults: SetUnhandledExceptionFilter() on Windows
// (access violation, stack overflow, ...), sigaction() for SIGSEGV, SIGBUS, SIGFPE and SIGILL on POSIX (with an
// alternate signal stack for the calling thread, so a stack overflow on it is handled too). Each handler writes the
// crash to the loggers (see above) once per process, then passes the crash on to the handler installed before it, or
// to the system's default, so the process still ends as it would have, e.g. with a core dump or an error report.
// Calling it again only applies 'options'. Returns false if a handler couldn't be installed (the others are). Call it
// early, e.g. after the loggers are initialized, on the main thread. Never throws.
bool InstallCrashHandlers(const TASWCrashHandlerOptions& options = {}) noexcept;

// Restores the handlers that were installed before InstallCrashHandlers(), e.g. before a DLL that installed them is
// unloaded. A handler that was replaced since then (e.g. by a crash reporter that chains to it) stays in place, but
// does nothing but pass the crash on. Never throws.
void UninstallCrashHandlers() noexcept;

//---------------------------------------------------------------------------
// Internals of the crash handling, used by TASWTextLogBase and the handlers. Not part of the public interface.
//---------------------------------------------------------------------------
namespace Detail
{

/////////////////////////////////////////////////////////////////////////////
// TCrashText
//
// Text built in a fixed buffer, without allocating, so it can be built in a signal handler. Text that doesn't fit is
// cut off.
/////////////////////////////////////////////////////////////////////////////
class TCrashText
{
public:
    static constexpr std::size_t Capacity = 1024;

private:
    char m_Text[Capacity];
    std::size_t m_Size = 0;

public:
    void Append(std::string_view text) noexcept;
    void AppendDecimal(std::uint64_t value, int minDigits = 1) noexcept; // Padded with leading zeros to 'minDigits'
    void AppendHex(std::uint64_t value, int minDigits) noexcept; // "0x" and at least 'minDigits' upper case digits
    void AppendISO8601(std::chrono::system_clock::time_point time) noexcept; // "YYYY-MM-DDTHH:mm:ss.mmmZ"
    std::size_t GetSize() const noexcept;
    void Truncate(std::size_t size) noexcept; // Cuts the text to 'size' characters, if it is longer
    std::string_view View() const noexcept;
};


/////////////////////////////////////////////////////////////////////////////
// TCrashHandling
//
// The list of initialized text loggers that a crash flushes, and the crash handling for all of them. A friend of
// TASWTextLogBase.
/////////////////////////////////////////////////////////////////////////////
class TCrashHandling
{
public:
    // Flushes every listed logger and writes 'message' to it (see TASWTextLogBase::OnCrash()), within 'waitTimeout' in
    // all. 'isInSignalHandler' is true in a POSIX signal handler: only async-signal-safe work, and the fixed-layout
    // line. Never throws.
    static void HandleCrashInLoggers(std::string_view message, bool isInSignalHandler, std::chrono::milliseconds waitTimeout) noexcept;
    // Adds a logger to the list, if it isn't on it (TASWTextLogBase::Initialize()), or takes it off (Finalize() and
    // the destructor). The list's lock is held while a crash is handled, so a logger can't be destroyed meanwhile.
    static void Register(TASWTextLogBase& log) noexcept;
    static void Unregister(TASWTextLogBase& log) noexcept;
};

// Appends the fixed-layout crash line for 'record' (see the crash handling notes above), with a CRLF or LF ending. A
// message too long for the buffer is cut off, but the line always ends with the line ending.
void AppendCrashLine(TCrashText& line, const TASWLogRecord& record, bool usesCRLF) noexcept;

// Pauses briefly (about a millisecond) and returns true, or returns false at once if 'deadline' has passed. For the
// polling waits of a crash handler, which can't wait on a condition variable in a signal handler.
bool PauseBeforeDeadline(std::chrono::steady_clock::time_point deadline) noexcept;

} // namespace Detail

} // namespace ASWLog

#endif // ASWLog_CrashHandlerH
