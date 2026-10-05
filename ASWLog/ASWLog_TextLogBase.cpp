/* **************************************************************************
ASWLog_TextLogBase.cpp
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
#include "ASWLog_TextLogBase.h"
//---------------------------------------------------------------------------
// System includes here
#include <algorithm>
#include <format>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

// The logger whose worker thread this is (see TASWTextLogBase::RunWorker()), or null on any other thread. On its own
// worker, a logger never waits for the queue or the worker, which would wait for itself.
thread_local const TASWTextLogBase* CurrentWorkerLog = nullptr;

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWTextLogBase
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWTextLogBase::~TASWTextLogBase()
{
    // Normally Finalize() has stopped it. Otherwise the derived logger is already destroyed, so the worker must not wake
    // again.
    UpdateWorker(true);
}

//---------------------------------------------------------------------------
void TASWTextLogBase::AfterEntryUnlocked()
{
}

//---------------------------------------------------------------------------
void TASWTextLogBase::AfterQueuedEntriesUnlocked(bool mustFlush)
{
    if (mustFlush)
        FlushUnlocked();
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::Close() noexcept
{
    try
    {
        TWorkerUpdater workerUpdater(*this);
        WaitForQueuedEntries();
        TDeferredWorkRunner deferredWorkRunner(*this);
        std::lock_guard<std::mutex> lock(m_Mutex);
        return CloseUnlocked();
    }
    catch (...)
    {
        return false;
    }
}

//---------------------------------------------------------------------------
void TASWTextLogBase::DeferUnlocked(std::function<void()> work)
{
    m_DeferredWork.push_back(std::move(work));
    m_HasDeferredWork.store(true, std::memory_order_relaxed);
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::DispatchLogCallback

    Calls the OnLogEntry of 'config', the snapshot WriteRecord() took when it wrote the entry. Called after m_Mutex
    has been released, so a callback that logs again (or reconfigures the logger) doesn't deadlock.
*/
void TASWTextLogBase::DispatchLogCallback(const TASWLogConfig& config, const TASWLogRecord& record,
    std::string_view formattedLine) const noexcept
{
    try
    {
        config.OnLogEntry(record, formattedLine);
    }
    catch (...)
    {
    }
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::EnsureReadyUnlocked()
{
    return m_IsInitialized.load(std::memory_order_acquire) && m_IsOpen.load(std::memory_order_acquire);
}

//---------------------------------------------------------------------------
void TASWTextLogBase::Finalize() noexcept
{
    // Called from the destructors, where an exception would terminate the program
    try
    {
        // Stops the worker, which must not outlive the derived logger, also if the logger is still initialized because
        // an exception skipped CloseUnlocked()
        TWorkerUpdater workerUpdater(*this, true);
        WaitForQueuedEntries(); // So the shutdown line comes after them
        TDeferredWorkRunner deferredWorkRunner(*this);
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_IsInitialized.load(std::memory_order_acquire))
            return;

        // EnsureReadyUnlocked() reopens an output that is closed between entries (e.g. a file with
        // AutoOpenClosePerWrite)
        if (GetConfigUnlocked().Shutdown.WriteLine && EnsureReadyUnlocked())
        {
            std::string msg = "Logger shutdown: " + Time::ToISO8601String(NowUTC());

            if (!GetConfigUnlocked().Shutdown.Banner.empty())
                msg += ", " + GetConfigUnlocked().Shutdown.Banner;

            WriteInfoLine(msg);
        }

        CloseUnlocked();
    }
    catch (...)
    {
        ReportCurrentException("Couldn't shut down cleanly");
    }
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::Flush() noexcept
{
    try
    {
        WaitForQueuedEntries();
        TDeferredWorkRunner deferredWorkRunner(*this);
        std::lock_guard<std::mutex> lock(m_Mutex);
        return FlushUnlocked();
    }
    catch (...)
    {
        return false;
    }
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::FormatEntry

    The line written for 'record' with 'config': its message as is for a Raw record, otherwise the formatter's line
    followed by the line ending.
*/
std::string TASWTextLogBase::FormatEntry(const TASWLogRecord& record, const TASWLogConfig& config)
{
    std::string line;

    if (record.Raw)
    {
        line.reserve(record.Message.size() + 2);
        line.append(record.Message);

        return line;
    }

    // Without a formatter, calls the built-in layout directly (no default formatter object, which could be destroyed
    // at exit before a never-destroyed singleton logger writes its shutdown line)
    const auto* formatter = config.Line.Formatter.get();
    line = formatter != nullptr ? formatter->Format(record, config) : TASWTextFormatter::FormatLine(record, config);

    if (config.Line.Ending == LineEnding::CRLF)
        line += "\r\n";
    else
        line += '\n';

    return line;
}

//---------------------------------------------------------------------------
std::chrono::milliseconds TASWTextLogBase::GetWorkerIntervalUnlocked() const
{
    return std::chrono::milliseconds(0);
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::Initialize(const TASWLogConfig& config) noexcept
{
    try
    {
        TWorkerUpdater workerUpdater(*this);
        TDeferredWorkRunner deferredWorkRunner(*this);
        std::shared_ptr<const TASWLogConfig> previousConfig; // Released after the lock (see SetConfig())
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_IsInitialized.load(std::memory_order_acquire))
        {
            return false;
        }

        previousConfig = SetConfig(config);
        SetMinimumLevel(config.InitialMinimumLevel);

        if (!InitializeUnlocked())
        {
            return false;
        }

        // The output is ready, so the startup lines are best effort: if one throws (e.g. a formatter that throws, or
        // out of memory while gathering the system info), the rest are skipped but the logger is still initialized
        try
        {
            if (!GetConfigUnlocked().Startup.Banner.empty())
            {
                WriteInfoLine(GetConfigUnlocked().Startup.Banner);
            }

            WriteInitializationInfo();
        }
        catch (...)
        {
            ReportCurrentExceptionUnlocked("Skipped the startup lines");
        }

        AfterEntryUnlocked();

        return true;
    }
    catch (...)
    {
        return false;
    }
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::IsOpen() const noexcept
{
    return m_IsOpen.load(std::memory_order_acquire);
}

//---------------------------------------------------------------------------
void TASWTextLogBase::OnWorkerWakeUnlocked()
{
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::Open() noexcept
{
    try
    {
        TWorkerUpdater workerUpdater(*this);
        TDeferredWorkRunner deferredWorkRunner(*this);
        std::lock_guard<std::mutex> lock(m_Mutex);
        return OpenUnlocked();
    }
    catch (...)
    {
        return false;
    }
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::PrepareWriteUnlocked(std::chrono::system_clock::time_point /*now*/)
{
    return true;
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::QueueRecord

    For an asynchronous logger (see TASWAsyncConfig): formats the entry without m_Mutex, from the current config, and
    queues it for the worker. A full queue makes the caller wait for room, or drops the entry (DropNewest; counted for
    the worker to report), except for an entry at or above WaitAtLevel, whose caller also waits until it is written and
    flushed. On the worker thread itself (e.g. OnLogEntry logging again), the entry is queued without waiting. Returns
    false, after the entries queued before have been written, if async has been switched off meanwhile: the caller then
    writes the entry itself.
*/
bool TASWTextLogBase::QueueRecord(const TASWLogRecord& record)
{
    const bool isOnWorker = CurrentWorkerLog == this;
    const auto config = GetConfig();

    TQueuedEntry entry;
    entry.Line = FormatEntry(record, *config);
    entry.Record = record;
    entry.Record.Message = {};
    entry.MustFlush = record.LogLevel >= config->Async.WaitAtLevel;

    if (config->OnLogEntry != nullptr && record.LogLevel >= config->OnLogEntryMinimumLevel)
    {
        entry.Message = record.Message;
        entry.CallbackConfig = config;
    }

    const bool mustFlush = entry.MustFlush;
    const auto capacity = std::max<std::size_t>(1, config->Async.QueueCapacity);
    const bool mayDrop = config->Async.OverflowPolicy == AsyncOverflowPolicy::DropNewest && !mustFlush;

    std::unique_lock<std::mutex> lock(m_QueueMutex);

    if (!isOnWorker)
    {
        if (mayDrop && m_IsAsyncActive && m_Queue.size() >= capacity)
        {
            ++m_DroppedCount;
            return true;
        }

        m_QueueChanged.wait(lock, [this, capacity] {
                return !m_IsAsyncActive || m_Queue.size() < capacity;
            });
    }

    if (!m_IsAsyncActive)
    {
        // Switched to synchronous writing: the entries queued before this one are written first, to keep the order
        if (!isOnWorker)
        {
            m_QueueChanged.wait(lock, [this] {
                    return m_LastWrittenSequence >= m_LastQueuedSequence;
                });
        }

        return false;
    }

    // Counted only once queued: a sequence number that never reaches the queue would keep WaitForQueuedEntries() waiting
    const auto sequence = m_LastQueuedSequence + 1;
    entry.Sequence = sequence;
    m_Queue.push_back(std::move(entry));
    m_LastQueuedSequence = sequence;
    m_WorkerWakeup.notify_one();

    if (mustFlush && !isOnWorker)
    {
        m_QueueChanged.wait(lock, [this, sequence] {
                return m_LastWrittenSequence >= sequence;
            });
    }

    return true;
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::Reconfigure(const TASWLogConfig& config) noexcept
{
    try
    {
        TWorkerUpdater workerUpdater(*this);
        WaitForQueuedEntries(); // Written with the config they were formatted with (e.g. to the old file)
        TDeferredWorkRunner deferredWorkRunner(*this);
        std::shared_ptr<const TASWLogConfig> previousConfig; // Released after the lock (see SetConfig())
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_IsInitialized.load(std::memory_order_acquire))
            return false;

        previousConfig = SetConfig(config);
        return ReconfigureUnlocked(*previousConfig);
    }
    catch (...)
    {
        return false;
    }
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::ReconfigureUnlocked(const TASWLogConfig& /*previous*/)
{
    return true;
}

//---------------------------------------------------------------------------
void TASWTextLogBase::ReportCurrentExceptionUnlocked(std::string_view action) noexcept
{
    try
    {
        ReportErrorUnlocked(MakeExceptionError(action));
    }
    catch (...)
    {
    }
}

//---------------------------------------------------------------------------
void TASWTextLogBase::ReportErrorUnlocked(TASWLogError error) noexcept
{
    try
    {
        DeferUnlocked([this, config = GetConfigSnapshotUnlocked(), error = std::move(error)]() mutable {
                ReportError(*config, std::move(error));
            });
    }
    catch (...)
    {
    }
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::RunDeferredWork

    Runs the work queued by DeferUnlocked(), after m_Mutex has been released (see TDeferredWorkRunner), so it may call
    into the application, which may log again or call the logger. Any thread may run another thread's work: whichever
    takes it from the queue first.
*/
void TASWTextLogBase::RunDeferredWork() noexcept
{
    // Checked without the lock, so a method that queued nothing (the usual case) doesn't take m_Mutex again. Work
    // queued by this thread is always seen here.
    if (!m_HasDeferredWork.load(std::memory_order_relaxed))
        return;

    decltype(m_DeferredWork) deferredWork; // Released after the lock: destroying it can run other code (see SetConfig())
    try
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        deferredWork.swap(m_DeferredWork);
        m_HasDeferredWork.store(false, std::memory_order_relaxed);
    }
    catch (...)
    {
        return;
    }

    for (auto& work : deferredWork)
    {
        try
        {
            work();
        }
        catch (...)
        {
        }
    }
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::RunWorker

    The worker thread. Writes the queued entries of an asynchronous logger in batches (see WriteQueuedEntries()), and
    calls OnWorkerWakeUnlocked() under m_Mutex every m_WorkerInterval, until UpdateWorker() stops it: it then writes
    what is still queued and ends. It waits on m_QueueMutex, not m_Mutex, so queuing an entry never waits for a write.
*/
void TASWTextLogBase::RunWorker() noexcept
{
    // Waits at most this long at a time, so that a very long interval can't overflow the clock arithmetic
    constexpr std::chrono::milliseconds MaxWait = std::chrono::hours(1);

    CurrentWorkerLog = this;
    auto lastWake = std::chrono::steady_clock::now();
    for (;;)
    {
        try
        {
            std::deque<TQueuedEntry> entries;
            std::size_t droppedCount = 0;
            bool isStopping = false;
            bool isWakeDue = false;

            {
                std::unique_lock<std::mutex> lock(m_QueueMutex);
                for (;;)
                {
                    isStopping = m_IsWorkerStopping;
                    const auto interval = m_WorkerInterval;
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - lastWake);
                    isWakeDue = interval.count() > 0 && elapsed >= interval;
                    if (isStopping || isWakeDue || !m_Queue.empty() || m_DroppedCount > 0)
                        break;

                    if (interval.count() > 0)
                        m_WorkerWakeup.wait_for(lock, std::min(interval - elapsed, MaxWait));
                    else
                        m_WorkerWakeup.wait(lock); // Until an entry is queued, a stop, or a new interval
                }

                // Takes the drop count with the entries: every entry it counts was dropped after these were queued
                entries.swap(m_Queue);
                droppedCount = m_DroppedCount;
                m_DroppedCount = 0;
                m_QueueChanged.notify_all(); // Room in the queue
            }

            if (!entries.empty() || droppedCount > 0)
                WriteQueuedEntries(entries, droppedCount);

            if (isWakeDue)
            {
                lastWake = std::chrono::steady_clock::now();
                TDeferredWorkRunner deferredWorkRunner(*this);
                std::lock_guard<std::mutex> lock(m_Mutex);
                OnWorkerWakeUnlocked();
            }

            if (isStopping)
                return;
        }
        catch (...)
        {
            ReportCurrentException("Couldn't do the worker thread's work");
        }
    }
}

//---------------------------------------------------------------------------
void TASWTextLogBase::SetAsyncActive(bool isActive) noexcept
{
    m_IsAsyncActive = isActive;

    // When switched off, m_IsAsync stays set until the queued entries are written (see WriteQueuedEntries()), so that
    // a caller doesn't write its entry before them
    if (isActive || m_LastWrittenSequence >= m_LastQueuedSequence)
        m_IsAsync.store(isActive, std::memory_order_release);

    m_QueueChanged.notify_all(); // Callers waiting for room re-check
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::UpdateWorker

    Starts, wakes or stops the worker thread, as the logger's state says: it runs while the logger is initialized and
    either GetWorkerIntervalUnlocked() is above 0 or TASWAsyncConfig::Enabled is set, unless 'mustStop' (Finalize() and
    the destructor). Called without m_Mutex, after the method that changed the state has released it.

    A worker being replaced is joined before the new one starts (it writes what is still queued first), so entries are
    never written by two workers out of order. m_WorkerMutex is held meanwhile, which the worker never takes: on the
    worker thread itself (e.g. an OnError handler that calls Close()), UpdateWorkerFromWorker() only changes the state,
    since the worker can't wait for or join itself.
*/
void TASWTextLogBase::UpdateWorker(bool mustStop) noexcept
{
    try
    {
        if (CurrentWorkerLog == this)
        {
            UpdateWorkerFromWorker(mustStop);
            return;
        }

        std::lock_guard<std::mutex> workerLock(m_WorkerMutex);
        for (;;)
        {
            bool isWanted = false;
            bool isAsync = false;
            std::chrono::milliseconds interval{ 0 };
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                if (!mustStop && m_IsInitialized.load(std::memory_order_acquire))
                {
                    interval = GetWorkerIntervalUnlocked();
                    isAsync = GetConfigUnlocked().Async.Enabled;
                    isWanted = interval.count() > 0 || isAsync;
                }

                std::lock_guard<std::mutex> queueLock(m_QueueMutex);
                if (m_Worker.joinable() && !m_IsWorkerStopping && isWanted)
                {
                    // Keep it, with the new settings
                    m_WorkerInterval = interval;
                    SetAsyncActive(isAsync);
                    m_WorkerWakeup.notify_all();
                    return;
                }

                if (m_Worker.joinable())
                {
                    // Stop it: it writes what is queued, then ends. Entries logged meanwhile are written by their
                    // callers, once the queued ones are written (see QueueRecord()).
                    m_IsWorkerStopping = true;
                    m_IsWorkerStoppingItself = false;
                    SetAsyncActive(false);
                    m_WorkerWakeup.notify_all();
                }
                else if (!isWanted)
                {
                    return;
                }
                else
                {
                    m_IsWorkerStopping = false;
                    m_WorkerInterval = interval;
                }
            }

            if (m_Worker.joinable())
            {
                // Then decides again, since the state may have changed meanwhile (e.g. its OnError handler reopened
                // the logger)
                m_Worker.join();
                continue;
            }

            m_Worker = std::thread(&TASWTextLogBase::RunWorker, this);

            std::lock_guard<std::mutex> queueLock(m_QueueMutex);
            SetAsyncActive(isAsync);
            return;
        }
    }
    catch (...)
    {
        ReportCurrentException("Couldn't start or stop the worker thread");
    }
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::UpdateWorkerFromWorker

    UpdateWorker() on the worker thread itself, e.g. from an OnError handler that calls Close() or Open(). It can't
    join or replace the worker it runs on, so it only tells it to stop once back in its loop, or, if it is wanted again
    after it stopped itself, to go on. A stop from another thread, which is joining it, stands. The worker's
    std::thread is joined by the next UpdateWorker() on another thread, at the latest Finalize().
*/
void TASWTextLogBase::UpdateWorkerFromWorker(bool mustStop)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    std::chrono::milliseconds interval{ 0 };
    bool isAsync = false;

    if (!mustStop && m_IsInitialized.load(std::memory_order_acquire))
    {
        interval = GetWorkerIntervalUnlocked();
        isAsync = GetConfigUnlocked().Async.Enabled;
    }

    std::lock_guard<std::mutex> queueLock(m_QueueMutex);

    if (interval.count() > 0 || isAsync)
    {
        if (m_IsWorkerStopping && m_IsWorkerStoppingItself)
            m_IsWorkerStopping = false;

        if (!m_IsWorkerStopping)
        {
            m_WorkerInterval = interval;
            SetAsyncActive(isAsync);
        }
    }
    else if (!m_IsWorkerStopping)
    {
        m_IsWorkerStopping = true;
        m_IsWorkerStoppingItself = true;
        SetAsyncActive(false);
    }

    m_WorkerWakeup.notify_all();
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::WaitForQueuedEntries

    Waits until the entries queued so far (see TASWAsyncConfig) have been written, so that what the caller does next
    (flush, close, reconfigure, the shutdown line) comes after them. Doesn't wait on the worker thread itself, which
    writes them.
*/
void TASWTextLogBase::WaitForQueuedEntries()
{
    if (CurrentWorkerLog == this)
        return;

    std::unique_lock<std::mutex> lock(m_QueueMutex);
    const auto sequence = m_LastQueuedSequence;
    m_QueueChanged.wait(lock, [this, sequence] {
            return m_LastWrittenSequence >= sequence;
        });
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteApplicationInfo()
{
    auto applicationInfo = std::format("app_exe='{}', app_target=", PathToUTF8String(GetExecutablePath()));

#if defined(_WIN64)
    applicationInfo += "Win64";
#elif defined(_WIN32)
    applicationInfo += "Win32";
#elif defined(__linux__) && defined(__x86_64__)
    applicationInfo += "Linux64";
#elif defined(__linux__) && defined(__aarch64__)
    applicationInfo += "LinuxARM64";
#elif defined(__linux__)
    applicationInfo += "Linux32";
#else
#error "ASWLog: Unrecognized target platform in WriteApplicationInfo()"
#endif

    if (GetConfigUnlocked().Startup.WriteCommandLine)
        applicationInfo += std::format(", command_line='{}'", GetCommandLineString());

    WriteInfoLine(std::format("App: {}", applicationInfo));
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteDriveInfo()
{
    WriteInfoLine(std::format("Drive: {}", GetDriveInfoString()));
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::WriteInfoLine

    Writes one of this logger's own Info lines (startup, banner or shutdown), stamped now, with the caller's location.
*/
void TASWTextLogBase::WriteInfoLine(std::string_view message, std::source_location loc)
{
    TASWLogRecord record;
    record.LogLevel = Level::Info;
    record.Message = message;
    record.Location = loc;
    StampRecord(record);
    WriteLogEntry(record);
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteInitializationInfo()
{
    if (GetConfigUnlocked().Startup.WriteTimeInfo)
        WriteTimeInfo();

    if (GetConfigUnlocked().Startup.WriteOSInfo)
        WriteOSInfo();

    if (GetConfigUnlocked().Startup.WriteDriveInfo)
        WriteDriveInfo();

    if (GetConfigUnlocked().Startup.WriteSystemMemoryInfo)
        WriteSystemMemoryInfo();

    if (GetConfigUnlocked().Startup.WriteApplicationInfo)
        WriteApplicationInfo();

    if (GetConfigUnlocked().Startup.WriteMemoryUsage)
        WriteMemoryUsageInfo();
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::WriteLogEntry

    Formats and writes one stamped entry, unless the logger is disabled, its level is filtered out (and it isn't
    forced), or PrepareWriteUnlocked() drops it. Returns the line written, or an empty string if nothing was written.
    The startup and shutdown lines come through here too, so they aren't written while the logger is disabled.
*/
std::string TASWTextLogBase::WriteLogEntry(const TASWLogRecord& record)
{
    if (!IsEnabled() || (!record.Forced && record.LogLevel < GetMinimumLevel()))
        return {};

    if (!PrepareWriteUnlocked(record.Timestamp))
        return {};

    auto line = FormatEntry(record, GetConfigUnlocked());
    WriteLineUnlocked(record.LogLevel, line, !record.Raw);
    return line;
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteMemoryUsageInfo()
{
    WriteInfoLine(std::format("App Memory: {}", GetMemoryUsageString()));
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteOSInfo()
{
    WriteInfoLine(std::format("OS: {}", GetOSInfoString()));
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::WriteQueuedEntries

    On the worker thread: writes queued entries in order under m_Mutex, through the same hooks as a synchronous entry,
    then, if the full queue dropped entries, a forced Warn line saying how many (also reported as
    ErrorKind::EntriesDropped), then lets AfterQueuedEntriesUnlocked() flush them. Then marks them written, which
    releases the callers waiting for them, and calls OnLogEntry for each one written, outside the lock.
*/
void TASWTextLogBase::WriteQueuedEntries(std::deque<TQueuedEntry>& entries, std::size_t droppedCount) noexcept
{
    try
    {
        TDeferredWorkRunner deferredWorkRunner(*this);
        std::lock_guard<std::mutex> lock(m_Mutex);
        bool mustFlush = false;
        m_IsWritingQueuedEntries = true;

        try
        {
            for (auto& entry : entries)
            {
                if (!EnsureReadyUnlocked() || !PrepareWriteUnlocked(entry.Record.Timestamp))
                    continue;

                WriteLineUnlocked(entry.Record.LogLevel, entry.Line, !entry.Record.Raw);
                AfterEntryUnlocked();
                entry.IsWritten = true;
                mustFlush = mustFlush || entry.MustFlush;
            }

            if (droppedCount > 0)
            {
                const auto message = std::format("Dropped {} {}: the asynchronous queue was full", droppedCount,
                    droppedCount == 1 ? "entry" : "entries");

                TASWLogError error;
                error.Kind = ErrorKind::EntriesDropped;
                error.Message = message;
                ReportErrorUnlocked(std::move(error));

                if (EnsureReadyUnlocked())
                {
                    TASWLogRecord record;
                    record.LogLevel = Level::Warn;
                    record.Message = message;
                    record.Forced = true;
                    StampRecord(record);
                    WriteLogEntry(record);
                    AfterEntryUnlocked();
                }
            }
        }
        catch (...)
        {
            ReportCurrentExceptionUnlocked("Dropped queued entries");
        }

        m_IsWritingQueuedEntries = false;
        AfterQueuedEntriesUnlocked(mustFlush);
    }
    catch (...)
    {
        ReportCurrentException("Couldn't write queued entries");
    }

    try
    {
        std::lock_guard<std::mutex> queueLock(m_QueueMutex);

        if (!entries.empty())
            m_LastWrittenSequence = std::max(m_LastWrittenSequence, entries.back().Sequence);

        // Async was switched off, and this was the last of the queue: entries go straight to the output again
        if (!m_IsAsyncActive && m_LastWrittenSequence >= m_LastQueuedSequence)
            m_IsAsync.store(false, std::memory_order_release);

        m_QueueChanged.notify_all();
    }
    catch (...)
    {
    }

    for (auto& entry : entries)
    {
        if (!entry.IsWritten || entry.CallbackConfig == nullptr)
            continue;

        auto record = entry.Record;
        record.Message = entry.Message;
        DispatchLogCallback(*entry.CallbackConfig, record, entry.Line);
    }
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::WriteRecord

    Writes one entry passed to Write(), then calls OnLogEntry if it's set and the record's level meets
    OnLogEntryMinimumLevel, then reports the failures the hooks found meanwhile. If writing throws (e.g. out of memory,
    or a formatter that throws), TASWLogBase::Write() drops and reports the entry. An asynchronous logger queues the
    entry instead (see QueueRecord()).
*/
void TASWTextLogBase::WriteRecord(const TASWLogRecord& record)
{
    // Acquire: once it reads false after async was on, the queued entries have been written (see WriteQueuedEntries())
    if (m_IsAsync.load(std::memory_order_acquire) && QueueRecord(record))
        return;

    TDeferredWorkRunner deferredWorkRunner(*this);
    std::string writtenLine;
    // Set only if OnLogEntry is called: the callback runs outside the lock, from the config the entry was written with
    std::shared_ptr<const TASWLogConfig> callbackConfig;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!EnsureReadyUnlocked())
            return;

        writtenLine = WriteLogEntry(record);

        AfterEntryUnlocked();

        const auto& config = GetConfigUnlocked();
        if (!writtenLine.empty() && config.OnLogEntry != nullptr && record.LogLevel >= config.OnLogEntryMinimumLevel)
            callbackConfig = GetConfigSnapshotUnlocked();
    }

    if (callbackConfig != nullptr)
        DispatchLogCallback(*callbackConfig, record, writtenLine);
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteSystemMemoryInfo()
{
    WriteInfoLine(std::format("System memory: {}", GetSystemMemoryUsageString()));
}

//---------------------------------------------------------------------------
void TASWTextLogBase::WriteTimeInfo()
{
    WriteInfoLine(std::format("Time: {}", GetTimeInfoString()));
}

//---------------------------------------------------------------------------

} // namespace ASWLog
