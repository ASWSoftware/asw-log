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
void TASWTextLogBase::AppendLineEnding(std::string& line) const
{
    if (GetConfigUnlocked().Line.Ending == LineEnding::CRLF)
        line += "\r\n";
    else
        line += '\n';
}

//---------------------------------------------------------------------------
bool TASWTextLogBase::Close() noexcept
{
    try
    {
        TWorkerUpdater workerUpdater(*this);
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
bool TASWTextLogBase::Reconfigure(const TASWLogConfig& config) noexcept
{
    try
    {
        TWorkerUpdater workerUpdater(*this);
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

    The worker thread: calls OnWorkerWakeUnlocked() under m_Mutex every GetWorkerIntervalUnlocked(), until
    UpdateWorker() changes m_WorkerGeneration from 'generation'. The interval is read again before each wait, and
    UpdateWorker() wakes the worker to read it after a Reconfigure(). Each wake ends like a public method: the lock is
    released, then the work queued meanwhile (e.g. the report of a failed flush) runs, on this thread.
*/
void TASWTextLogBase::RunWorker(std::uint64_t generation) noexcept
{
    // Waits at most this long at a time, so that a very long interval can't overflow the clock arithmetic
    constexpr std::chrono::milliseconds MaxWait = std::chrono::hours(1);

    auto lastWake = std::chrono::steady_clock::now();
    for (;;)
    {
        try
        {
            TDeferredWorkRunner deferredWorkRunner(*this);
            std::unique_lock<std::mutex> lock(m_Mutex);
            for (;;)
            {
                if (m_WorkerGeneration != generation)
                    return;

                const auto interval = GetWorkerIntervalUnlocked();
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - lastWake);
                if (interval.count() <= 0)
                    m_WorkerWakeup.wait(lock); // Until stopped, or woken to read a new interval
                else if (elapsed < interval)
                    m_WorkerWakeup.wait_for(lock, std::min(interval - elapsed, MaxWait));
                else
                    break;
            }

            lastWake = std::chrono::steady_clock::now();
            OnWorkerWakeUnlocked();
        }
        catch (...)
        {
            ReportCurrentException("Couldn't do the worker thread's work");
        }
    }
}

//---------------------------------------------------------------------------
/*
    TASWTextLogBase::UpdateWorker

    Starts, wakes or stops the worker thread, as the logger's state says: it runs while the logger is initialized and
    GetWorkerIntervalUnlocked() is above 0, unless 'mustStop' (Finalize() and the destructor). Called without m_Mutex,
    after the method that changed the state has released it.

    A stopped worker is joined after m_WorkerMutex is released, since it may be running work that calls this logger
    (e.g. an OnError handler that calls Reconfigure(), which comes here and takes m_WorkerMutex). Called on the worker
    thread itself (from such work), it can't join it: it only tells it to stop, and the next call from another thread
    joins it, at the latest Finalize().
*/
void TASWTextLogBase::UpdateWorker(bool mustStop) noexcept
{
    std::thread stoppedWorker; // Joined at the end, without the locks
    try
    {
        std::lock_guard<std::mutex> workerLock(m_WorkerMutex);
        const bool isOnWorker = m_Worker.get_id() == std::this_thread::get_id();
        bool isWanted = false;
        std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            isWanted = !mustStop && m_IsInitialized.load(std::memory_order_acquire) && GetWorkerIntervalUnlocked().count() > 0;

            // Keep the worker, and wake it to read its interval again. On the worker thread, also if an earlier call on
            // this thread stopped it: it checks that only once back in its loop, so it hasn't ended yet.
            const bool isRunning = m_Worker.joinable() && m_WorkerGeneration == m_WorkerStartGeneration;
            if (isWanted && (isRunning || isOnWorker))
            {
                m_WorkerGeneration = m_WorkerStartGeneration;
                m_WorkerWakeup.notify_all();
                return;
            }

            if (!isWanted && !m_Worker.joinable())
                return;

            // Stops the worker, if there is one: it ends when it next checks its generation
            generation = ++m_WorkerGeneration;
            m_WorkerWakeup.notify_all();
        }

        if (isOnWorker)
            return;

        stoppedWorker = std::move(m_Worker);
        if (isWanted)
        {
            m_Worker = std::thread(&TASWTextLogBase::RunWorker, this, generation);
            m_WorkerStartGeneration = generation;
        }
    }
    catch (...)
    {
        ReportCurrentException("Couldn't start the worker thread");
    }

    if (stoppedWorker.joinable())
    {
        try
        {
            stoppedWorker.join();
        }
        catch (...)
        {
            stoppedWorker.detach(); // Rather than terminate the program when it is destroyed
        }
    }
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

    std::string line;
    if (record.Raw)
    {
        line.reserve(record.Message.size() + 2);
        line.append(record.Message);
    }
    else
    {
        // Without a formatter, calls the built-in layout directly (no default formatter object, which could be
        // destroyed at exit before a never-destroyed singleton logger writes its shutdown line)
        const auto& config = GetConfigUnlocked();
        const auto* formatter = config.Line.Formatter.get();
        line = formatter != nullptr ? formatter->Format(record, config) : TASWTextFormatter::FormatLine(record, config);
        AppendLineEnding(line);
    }

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
    TASWTextLogBase::WriteRecord

    Writes one entry passed to Write(), then calls OnLogEntry if it's set and the record's level meets
    OnLogEntryMinimumLevel, then reports the failures the hooks found meanwhile. If writing throws (e.g. out of memory,
    or a formatter that throws), TASWLogBase::Write() drops and reports the entry.
*/
void TASWTextLogBase::WriteRecord(const TASWLogRecord& record)
{
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
