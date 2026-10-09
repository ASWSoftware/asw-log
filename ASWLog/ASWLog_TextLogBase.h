/* **************************************************************************
ASWLog_TextLogBase.h
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

#ifndef ASWLog_TextLogBaseH
#define ASWLog_TextLogBaseH
//---------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWLog_Base.h"
#include "ASWLog_Formatter.h" // For TASWLineConfig::Formatter
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace Detail
{

class TCrashHandling; // See ASWLog_CrashHandler.h
class TCrashText;

} // namespace Detail

/////////////////////////////////////////////////////////////////////////////
// TASWTextLogBase
//
// Base for loggers that write each entry as a line of text, such as TASWFileLog and TASWConsoleLog. It implements
// WriteRecord() once for all of them: the lock, the safety net that keeps logging from throwing into the application,
// the line format, and the OnLogEntry callback, as well as the startup and shutdown lines. (TASWLogBase::Write() has
// already checked the level and stamped the record.)
//
// A derived logger implements the protected hooks for its own output. Each hook whose name ends in "Unlocked" is
// called with m_Mutex held, so it must not call a public method of this logger (which would lock it again), and
// reports a failure with ReportErrorUnlocked(). The derived logger's destructor must call Finalize(), which writes the
// shutdown line and closes the output.
//
// A derived logger that needs work done between entries (e.g. a file logger flushing every FlushInterval) returns how
// often from GetWorkerIntervalUnlocked(): this class then runs a thread of the logger's own that calls
// OnWorkerWakeUnlocked() that often, while the logger is initialized. The same thread writes the queued entries of an
// asynchronous logger (see TASWAsyncConfig), through the same hooks, in batches. Initialize(), Reconfigure(), Open(),
// Close() and Finalize() start, wake or stop it as the config says, after releasing m_Mutex; a thread they stop has
// written what was queued and ended by the time they return. A logger derived from another one (e.g. from
// TASWFileLog) that overrides a hook the worker calls must call Finalize() in its own destructor too, so that the
// worker has stopped before its overrides are destroyed.
//
// While initialized, a logger is on the list that the crash handlers flush (see ASWLog_CrashHandler.h), through
// FlushUnlocked() and WriteLineUnlocked(), or, in a POSIX signal handler, FlushForCrashUnlocked() and
// WriteCrashLineDirect().
//
// To change the line layout, assign a formatter to TASWLogConfig::Line.Formatter (see IASWLogFormatter).
/////////////////////////////////////////////////////////////////////////////
class TASWTextLogBase : public TASWLogBase
{
private:
    typedef TASWLogBase inherited;

    friend class Detail::TCrashHandling;

protected:
    mutable Detail::TMutex m_Mutex; // Serializes the writes, and the derived logger's own state
    std::atomic<bool> m_IsOpen{ false };

private:
    // The work queued while m_Mutex was held (see DeferUnlocked()), waiting to run once it is released. Guarded by
    // m_Mutex; m_HasDeferredWork lets a thread check for it without the lock.
    std::vector<std::function<void()> > m_DeferredWork;
    std::atomic<bool> m_HasDeferredWork{ false };

    // An entry queued by an asynchronous logger (see TASWAsyncConfig), for the worker to write
    struct TQueuedEntry
    {
        // Its Message, Category, Fields and Scope are empty: the copies below hold them, if needed
        TASWLogRecord Record;
        std::string Message; // The record's message, copied only if OnLogEntry will be called
        std::string Category; // The record's category, likewise
        Detail::TOwnedFields Fields; // The record's fields and its scopes', merged, likewise
        std::string Line; // The line to write: formatted, or the raw text
        std::shared_ptr<const TASWLogConfig> CallbackConfig; // Set only if OnLogEntry will be called
        std::uint64_t Sequence = 0;
        bool EndsLine = true; // Line ends with the line ending (see IsFormatted())
        bool MustFlush = false; // At or above TASWAsyncConfig::WaitAtLevel: its caller waits until it is flushed
        bool IsWritten = false;
    };

    // The worker thread (see GetWorkerIntervalUnlocked() and TASWAsyncConfig). m_WorkerMutex guards m_Worker and is
    // held by UpdateWorker() (never on the worker thread) while it starts, stops or joins the worker.
    Detail::TMutex m_WorkerMutex;
    std::thread m_Worker;

    // The queue and the worker's state, guarded by m_QueueMutex. Lock order: m_Mutex before m_QueueMutex.
    Detail::TMutex m_QueueMutex;
    std::condition_variable m_WorkerWakeup; // The worker waits on it: an entry was queued, or a stop or a new interval
    std::condition_variable m_QueueChanged; // Callers wait on it: room in the queue, entries written, async switched
    std::deque<TQueuedEntry> m_Queue;
    // Changed with m_QueueMutex held; atomic so that a crash handler can read them without it
    std::atomic<std::uint64_t> m_LastQueuedSequence{ 0 };
    std::atomic<std::uint64_t> m_LastWrittenSequence{ 0 }; // The worker has written (or dropped) the entries up to here
    std::size_t m_DroppedCount = 0; // Entries dropped (AsyncOverflowPolicy::DropNewest) since the worker last took them
    std::chrono::milliseconds m_WorkerInterval{ 0 }; // GetWorkerIntervalUnlocked(), as UpdateWorker() last read it
    bool m_IsAsyncActive = false; // Entries go to the queue: async is on and the worker runs
    bool m_IsWorkerStopping = false; // The worker writes what is queued, then ends
    bool m_IsWorkerStoppingItself = false; // The stop came from the worker thread itself, so it may be undone there
    // Read without the lock for each entry: set while m_IsAsyncActive, and until the entries queued before it was
    // cleared have been written
    std::atomic<bool> m_IsAsync{ false };

    bool m_IsWritingQueuedEntries = false; // Guarded by m_Mutex (see IsWritingQueuedEntriesUnlocked())

    // This logger's place on the list of loggers a crash flushes, guarded by that list's lock (see
    // Detail::TCrashHandling)
    TASWTextLogBase* m_CrashListPrevious = nullptr;
    TASWTextLogBase* m_CrashListNext = nullptr;
    bool m_IsOnCrashList = false;

    // From the config (see StoreCrashLineSettingsUnlocked()), for a crash handler that doesn't get the lock
    std::atomic<bool> m_CrashLineUsesCRLF{ false };
    std::atomic<bool> m_CrashLineUsesJSON{ false }; // The formatter is a TASWJSONFormatter
    std::atomic<MultilineMode> m_CrashLineMultiline{ MultilineMode::Preserve };
    std::atomic<bool> m_WritesCrashLine{ true };

private:
    // Calls UpdateWorker() when destroyed. Declare it before the TDeferredWorkRunner in a public method that can change
    // whether the worker should run, so that it runs after the lock is released.
    class TWorkerUpdater
    {
    private:
        TASWTextLogBase& m_Log;
        bool m_MustStop;

    public:
        explicit TWorkerUpdater(TASWTextLogBase& log, bool mustStop = false) noexcept
            : m_Log(log),
              m_MustStop(mustStop)
        {
        }

        ~TWorkerUpdater()
        {
            m_Log.UpdateWorker(m_MustStop);
        }

        TWorkerUpdater(const TWorkerUpdater&) = delete;
        TWorkerUpdater& operator=(const TWorkerUpdater&) = delete;
    };

private:
    static std::string FormatEntry(const TASWLogRecord& record, const TASWLogConfig& config);
    static bool IsFormatted(const TASWLogRecord& record, const TASWLogConfig& config) noexcept;

private:
    void AppendCrashLine(Detail::TCrashText& line, const TASWLogRecord& record) const noexcept;
    void DispatchLogCallback(const TASWLogConfig& config, const TASWLogRecord& record, std::string_view formattedLine) const noexcept;
    TASWLogRecord MakeBacktraceMarker(std::string_view message) const noexcept;
    void OnCrash(std::string_view message, bool isInSignalHandler, std::chrono::steady_clock::time_point deadline) noexcept;
    bool QueueRecord(const TASWLogRecord& record);
    void RunDeferredWork() noexcept;
    void RunWorker() noexcept;
    void SetAsyncActive(bool isActive) noexcept; // Holding m_QueueMutex
    void StoreCrashLineSettingsUnlocked() noexcept;
    void UpdateWorker(bool mustStop) noexcept;
    void UpdateWorkerFromWorker(bool mustStop);
    void WaitForQueuedEntries();
    void WriteApplicationInfo();
    template<typename TWriteLine>
    void WriteBacktraceForCrash(const TWriteLine& writeLine, bool mustClear, std::chrono::steady_clock::time_point deadline);
    void WriteBacktraceForCrashDirect(bool mustClear, std::chrono::steady_clock::time_point deadline) noexcept;
    void WriteBacktraceForCrashUnlocked(std::chrono::steady_clock::time_point deadline);
    void WriteDriveInfo();
    void WriteInfoLine(std::string_view message, std::source_location loc = std::source_location::current());
    void WriteInitializationInfo();
    std::string WriteLogEntry(const TASWLogRecord& record);
    void WriteMemoryUsageInfo();
    void WriteOSInfo();
    void WriteQueuedEntries(std::deque<TQueuedEntry>& entries, std::size_t droppedCount) noexcept;
    void WriteSystemMemoryInfo();
    void WriteTimeInfo();

protected:
    // Runs the work queued while m_Mutex was held (see DeferUnlocked()). Declare it before the lock_guard in a method
    // that takes m_Mutex, so that it is destroyed after the lock is released, also when the method returns early or
    // throws.
    class TDeferredWorkRunner
    {
    private:
        TASWTextLogBase& m_Log;

    public:
        explicit TDeferredWorkRunner(TASWTextLogBase& log) noexcept
            : m_Log(log)
        {
        }

        ~TDeferredWorkRunner()
        {
            m_Log.RunDeferredWork();
        }

        TDeferredWorkRunner(const TDeferredWorkRunner&) = delete;
        TDeferredWorkRunner& operator=(const TDeferredWorkRunner&) = delete;
    };

protected:
    // Called after each entry passed to Write(), and after Initialize() wrote the startup lines. Does nothing by
    // default.
    virtual void AfterEntryUnlocked();

    // Called on the worker thread after it has written a batch of queued entries (see TASWAsyncConfig), still holding
    // m_Mutex. 'mustFlush' is true if a caller waits for one of them to be flushed (TASWAsyncConfig::WaitAtLevel). By
    // default, flushes if 'mustFlush'; a file logger also flushes here for the flush modes that flush every entry.
    virtual void AfterQueuedEntriesUnlocked(bool mustFlush);

    // Closes the output. Must clear m_IsOpen and m_IsInitialized.
    virtual bool CloseUnlocked() = 0;

    // Queues 'work' to run once m_Mutex is released, for code holding it (e.g. a hook) that must call into the
    // application, such as a callback that may log again: the method's TDeferredWorkRunner runs it after the unlock.
    // Work runs in the order it was queued, on the thread that took the lock, or on another thread that takes it first
    // (so it must only use what it captures and this logger). An exception from the work is swallowed. Can throw (e.g.
    // out of memory); the work is then not queued.
    void DeferUnlocked(std::function<void()> work);

    // Called before each entry passed to Write(); returns false to drop the entry. By default, true if the logger is
    // initialized and open.
    virtual bool EnsureReadyUnlocked();

    // Writes the shutdown line (if TASWLogConfig::Shutdown.WriteLine) and closes the output, if the logger is
    // initialized, then stops the worker thread. Never throws. Each logger calls it from its destructor, since the hooks
    // can't be called from this class's destructor. Takes the logger off the list the crash handlers flush.
    void Finalize() noexcept;

    // Called by a crash handler in a POSIX signal handler (see ASWLog_CrashHandler.h), holding m_Mutex, before
    // WriteCrashLineDirect(): pushes the written entries out of the output's buffers, e.g. a file's stdio buffer. Must
    // only do async-signal-safe work (no allocation, no locks), and is best effort. Does nothing by default.
    virtual void FlushForCrashUnlocked() noexcept;

    // Called by Flush(): pushes the written entries out of the output's buffers. Returns false if the output isn't open
    // or the flush failed.
    virtual bool FlushUnlocked() = 0;

    // How often the worker thread calls OnWorkerWakeUnlocked(), from the current config; 0 or less = never (the worker
    // then runs only for TASWAsyncConfig::Enabled). Read when Initialize(), Reconfigure(), Open(), Close() or Finalize()
    // decide whether the worker should run (only while the logger is initialized), so a Reconfigure() that changes it
    // takes effect at once. Returns 0 by default.
    virtual std::chrono::milliseconds GetWorkerIntervalUnlocked() const;

    // Called by Initialize() after it has stored the config: prepares and opens the output. Returning false fails
    // Initialize().
    virtual bool InitializeUnlocked() = 0;

    // True while the worker thread writes queued entries (see TASWAsyncConfig), which it flushes as a batch afterwards
    // (see AfterQueuedEntriesUnlocked()), so WriteLineUnlocked() can leave out a flush per entry.
    bool IsWritingQueuedEntriesUnlocked() const noexcept
    {
        return m_IsWritingQueuedEntries;
    }

    // Called on the worker thread, every GetWorkerIntervalUnlocked() (measured from the start of the previous call),
    // e.g. to flush the output. Reports a failure with ReportErrorUnlocked(): the worker passes it on to OnError after
    // the unlock, on its own thread. An exception is caught and reported. Does nothing by default.
    virtual void OnWorkerWakeUnlocked();

    // Opens the output. Must set m_IsOpen and m_IsInitialized.
    virtual bool OpenUnlocked() = 0;

    // Called before each line is formatted and written, including the startup and shutdown lines, with the record's
    // time; returns false to drop the line. E.g. a file logger rotates its file here. Entries stamped by different
    // threads can get the lock out of time order, so 'now' can be a little earlier than for the previous line.
    // Returns true by default.
    virtual bool PrepareWriteUnlocked(std::chrono::system_clock::time_point now);

    // Called by Reconfigure() after it has stored the new config (GetConfigUnlocked()), to apply it to the output, e.g.
    // a file logger opens a new file. 'previous' is the config it replaced. Returning false fails Reconfigure(), but the
    // new config is kept. Returns true by default.
    virtual bool ReconfigureUnlocked(const TASWLogConfig& previous);

    // ReportCurrentException() for code holding m_Mutex (see ReportErrorUnlocked()). Never throws.
    void ReportCurrentExceptionUnlocked(std::string_view action) noexcept;

    // Reports one of this logger's failures from code holding m_Mutex (e.g. a hook): passes it to ReportError(), with
    // the current config, after the lock is released (see DeferUnlocked()). A failure that can't be queued (out of
    // memory) is dropped. Never throws.
    void ReportErrorUnlocked(TASWLogError error) noexcept;

    // Called by a crash handler (see ASWLog_CrashHandler.h) with a finished fixed-layout crash line: writes it straight
    // to the output, bypassing its buffers (e.g. write() on a file's descriptor), and syncs it if the config asks for
    // that. Called in a POSIX signal handler holding m_Mutex, or, if the lock stayed busy, without it (the thread
    // holding it may be the one that crashed), so it must only do async-signal-safe work and must not rely on the
    // state m_Mutex guards being consistent. Does nothing by default.
    virtual void WriteCrashLineDirect(std::string_view line) noexcept;

    // Writes a finished line. 'endsLine' is true if 'line' ends with the line ending (false for a Raw record written
    // as is; see IASWLogFormatter::FormatsRawEntries()).
    virtual void WriteLineUnlocked(Level level, std::string_view line, bool endsLine) = 0;

protected: // TASWLogBase hook
    // Writes the entry under m_Mutex, through the hooks above, then calls OnLogEntry. An asynchronous logger formats the
    // entry and queues it instead (see TASWAsyncConfig).
    void WriteRecord(const TASWLogRecord& record) final;

public:
    // Stops the worker thread if the derived logger's destructor didn't (see Finalize())
    ~TASWTextLogBase() override;

    // A startup line that fails (e.g. its formatter throws) doesn't fail Initialize(): the rest are skipped
    bool Initialize(const TASWLogConfig& config) noexcept final;
    // Waits for the queued entries (see TASWAsyncConfig) and an entry being written, then stores the config and calls
    // ReconfigureUnlocked()
    bool Reconfigure(const TASWLogConfig& config) noexcept final;

    bool Open() noexcept final;
    bool Close() noexcept final;
    bool Flush() noexcept final;
    bool IsOpen() const noexcept final;
};

} // namespace ASWLog

#endif // ASWLog_TextLogBaseH
