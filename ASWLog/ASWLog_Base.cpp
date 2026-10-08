/* **************************************************************************
ASWLog_Base.cpp
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
#include "ASWLog_Base.h"
//---------------------------------------------------------------------------
// System includes here
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWLog_CrashHandler.h" // For the backtrace marker texts
#include "ASWLog_Utils.h" // For GetCurrentOSProcessId(), GetCurrentOSThreadId()
//---------------------------------------------------------------------------

// A ThreadSanitizer build, whose runtime has the annotation Detail::TMutex uses (declared here as in
// <sanitizer/tsan_interface.h>)
#if defined(__SANITIZE_THREAD__)
#define ASWLOG_THREAD_SANITIZER 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define ASWLOG_THREAD_SANITIZER 1
#endif
#endif

#if defined(ASWLOG_THREAD_SANITIZER)
extern "C" void __tsan_mutex_destroy(void* addr, unsigned flags);
#endif

namespace ASWLog
{

namespace
{

// True while this thread is in TASWLogBase::ReportError()'s call to OnError (or its write to stderr)
thread_local bool IsReportingError = false;

// Sets IsReportingError while alive
class TReportingErrorScope
{
public:
    TReportingErrorScope() noexcept
    {
        IsReportingError = true;
    }

    ~TReportingErrorScope()
    {
        IsReportingError = false;
    }

    TReportingErrorScope(const TReportingErrorScope&) = delete;
    TReportingErrorScope& operator=(const TReportingErrorScope&) = delete;
};

} // namespace

//---------------------------------------------------------------------------

namespace Detail
{

/////////////////////////////////////////////////////////////////////////////
// TMutex
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TMutex::~TMutex()
{
#if defined(ASWLOG_THREAD_SANITIZER)
    // What the pthread_mutex_destroy() that libstdc++ leaves out would tell it (libc++ calls it too, which is harmless)
    __tsan_mutex_destroy(native_handle(), 0);
#endif
}

//---------------------------------------------------------------------------

} // namespace Detail

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWLogBase
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
/*
    TASWLogBase::ApplyBacktraceConfig

    Applies a new backtrace config (see TASWBacktraceConfig), from SetConfig(): a smaller capacity keeps the newest
    entries, 0 forgets them all. Then sets the backtrace's level, and with it the gate level (see PackLevels()).
*/
void TASWLogBase::ApplyBacktraceConfig(const TASWBacktraceConfig& config) noexcept
{
    const bool isOn = config.Capacity > 0 && config.LowestLevel != Level::Off;
    const std::size_t capacity = isOn ? config.Capacity : 0;

    {
        std::lock_guard<std::mutex> lock(m_BacktraceMutex);

        // Oldest first, so the kept part is at the end, and an entry added later goes after the newest
        if (m_Backtrace.size() > capacity || (m_BacktraceOldest != 0 && capacity != m_BacktraceCapacity))
        {
            std::rotate(m_Backtrace.begin(), m_Backtrace.begin() + static_cast<std::ptrdiff_t>(m_BacktraceOldest), m_Backtrace.end());
            m_BacktraceOldest = 0;
        }

        if (m_Backtrace.size() > capacity)
            m_Backtrace.erase(m_Backtrace.begin(), m_Backtrace.end() - static_cast<std::ptrdiff_t>(capacity));

        m_BacktraceCapacity = capacity;
        m_HasBacktrace.store(!m_Backtrace.empty(), std::memory_order_relaxed);
    }

    m_BacktraceDumpLevel.store(config.DumpAtLevel, std::memory_order_relaxed);

    // Keeps the minimum level
    const Level backtraceLevel = isOn ? config.LowestLevel : Level::Off;
    auto levels = m_Levels.load(std::memory_order_relaxed);
    while (!m_Levels.compare_exchange_weak(levels, PackLevels(MinimumLevelOf(levels), backtraceLevel), std::memory_order_relaxed))
    {
    }
}

//---------------------------------------------------------------------------
void TASWLogBase::DumpBacktrace() noexcept
{
    if (!IsEnabled() || !m_HasBacktrace.load(std::memory_order_relaxed))
        return;

    try
    {
        WriteBacktrace();
    }
    catch (...)
    {
        ReportCurrentException("Couldn't write the backtrace");
    }
}

//---------------------------------------------------------------------------
void TASWLogBase::KeepInBacktrace(const TASWLogRecord& record)
{
    std::lock_guard<std::mutex> lock(m_BacktraceMutex);
    if (m_BacktraceCapacity == 0)
        return; // Switched off since the gate let the entry in

    if (m_Backtrace.size() < m_BacktraceCapacity)
    {
        // The texts are copied first, so a failed copy leaves no empty entry behind
        TBacktraceEntry entry{ record, std::string(record.Message), std::string(record.Category) };
        entry.Record.Message = {};
        entry.Record.Category = {};
        m_Backtrace.push_back(std::move(entry));
    }
    else
    {
        // Replaces the oldest, reusing its message's memory. The category is copied first and swapped in last, so a
        // failed copy leaves the oldest entry as it was.
        auto& entry = m_Backtrace[m_BacktraceOldest];
        std::string category(record.Category);
        entry.Message.assign(record.Message);
        entry.Category.swap(category);
        entry.Record = record;
        entry.Record.Message = {};
        entry.Record.Category = {};
        m_BacktraceOldest = (m_BacktraceOldest + 1) % m_BacktraceCapacity;
    }

    m_HasBacktrace.store(true, std::memory_order_relaxed);
}

//---------------------------------------------------------------------------
bool TASWLogBase::KeepsBacktrace() const noexcept
{
    return true;
}

//---------------------------------------------------------------------------
TASWLogError TASWLogBase::MakeExceptionError(std::string_view action)
{
    TASWLogError error;
    error.Kind = ErrorKind::Exception;

    try
    {
        if (const auto exception = std::current_exception())
            std::rethrow_exception(exception);

        error.Message = action;
    }
    catch (const std::exception& exception)
    {
        error.Message = std::format("{}: {}", action, exception.what());
    }
    catch (...)
    {
        error.Message = std::format("{}: unknown exception", action);
    }

    return error;
}

//---------------------------------------------------------------------------
void TASWLogBase::ReportCurrentException(std::string_view action) noexcept
{
    try
    {
        ReportError(MakeExceptionError(action));
    }
    catch (...)
    {
    }
}

//---------------------------------------------------------------------------
void TASWLogBase::ReportError(const TASWLogConfig& config, TASWLogError error) noexcept
{
    // E.g. a handler that logs to this failing logger, which would otherwise report again, and again
    if (IsReportingError)
        return;

    const auto kindIndex = static_cast<std::size_t>(error.Kind);
    if (kindIndex >= m_ErrorReportStates.size())
        return;

    try
    {
        {
            std::lock_guard<std::mutex> lock(m_ErrorReportMutex);
            auto& state = m_ErrorReportStates[kindIndex];
            const auto now = NowUTC();

            // Like the retry delays, a clock that went backwards doesn't hold a report back
            if (state.HasReported && now >= state.LastReport && now - state.LastReport < config.ErrorReportInterval)
            {
                ++state.SuppressedCount;
                return;
            }

            error.SuppressedCount = state.SuppressedCount;
            state.LastReport = now;
            state.SuppressedCount = 0;
            state.HasReported = true;
        }

        TReportingErrorScope reportingScope;
        if (config.OnError != nullptr)
            config.OnError(error);
        else
            WriteErrorToStdErr(error);
    }
    catch (...)
    {
    }
}

//---------------------------------------------------------------------------
void TASWLogBase::ReportError(TASWLogError error) noexcept
{
    ReportError(*GetConfig(), std::move(error));
}

//---------------------------------------------------------------------------
std::shared_ptr<const TASWLogConfig> TASWLogBase::SetConfig(const TASWLogConfig& config)
{
    auto snapshot = std::make_shared<const TASWLogConfig>(config);

    {
        std::lock_guard<std::mutex> lock(m_ConfigMutex);
        m_Config.swap(snapshot);
    }

    if (KeepsBacktrace())
        ApplyBacktraceConfig(config.Backtrace);

    return snapshot;
}

//---------------------------------------------------------------------------
void TASWLogBase::StampRecord(TASWLogRecord& record) const noexcept
{
    if (record.Timestamp == std::chrono::system_clock::time_point{})
        record.Timestamp = NowUTC();

    if (record.ProcessId == 0)
        record.ProcessId = GetCurrentOSProcessId();

    if (record.ThreadId == 0)
        record.ThreadId = GetCurrentOSThreadId();
}

//---------------------------------------------------------------------------
/*
    TASWLogBase::TakeBacktrace

    The kept entries, oldest first, which the backtrace forgets. Moves the whole buffer out, so it can't fail.
*/
std::vector<TASWLogBase::TBacktraceEntry> TASWLogBase::TakeBacktrace() noexcept
{
    std::vector<TBacktraceEntry> entries;
    std::lock_guard<std::mutex> lock(m_BacktraceMutex);
    std::rotate(m_Backtrace.begin(), m_Backtrace.begin() + static_cast<std::ptrdiff_t>(m_BacktraceOldest), m_Backtrace.end());
    entries.swap(m_Backtrace);
    m_BacktraceOldest = 0;
    m_HasBacktrace.store(false, std::memory_order_relaxed);
    return entries;
}

//---------------------------------------------------------------------------
void TASWLogBase::Write(const TASWLogRecord& record) noexcept
{
    // Off isn't a severity and a disabled logger writes nothing, even when forced; a forced entry ignores only the
    // minimum level. Checked before stamping, so a filtered entry costs no clock or thread id read.
    if (!PassesLevelGate(record))
        return;

    // Stamped now, on the calling thread and before any lock, so the time is the moment of the call
    TASWLogRecord stampedRecord = record;
    StampRecord(stampedRecord);

    // A derived logger's WriteRecord() may throw (e.g. out of memory, or a custom logger's own error); logging must
    // never throw into the application, so the entry is dropped instead
    try
    {
        // Let in only for the backtrace. Read again: if the minimum level changed since the gate, the entry is kept or
        // written as if it had been logged just before or after the change.
        if (!record.Forced && record.LogLevel < GetMinimumLevelFor(record))
        {
            KeepInBacktrace(stampedRecord);
            return;
        }

        if (m_HasBacktrace.load(std::memory_order_relaxed) && record.LogLevel >= m_BacktraceDumpLevel.load(std::memory_order_relaxed))
            WriteBacktrace();

        WriteRecord(stampedRecord);
    }
    catch (...)
    {
        ReportCurrentException("Dropped an entry");
    }
}

//---------------------------------------------------------------------------
/*
    TASWLogBase::WriteBacktrace

    Passes the kept entries to WriteRecord(), oldest first and Forced (so they pass the derived logger's own minimum
    level check), between a "Backtrace: the last N entries below the minimum level" and a "Backtrace end" line (forced
    Info records), then forgets them. On the calling thread, so an asynchronous logger queues them in order.
*/
void TASWLogBase::WriteBacktrace()
{
    auto entries = TakeBacktrace();
    if (entries.empty())
        return;

    const auto writeMarker = [this](std::string_view message, std::source_location loc = std::source_location::current()) {
            TASWLogRecord marker;
            marker.LogLevel = Level::Info;
            marker.Message = message;
            marker.Location = loc;
            marker.Forced = true;
            StampRecord(marker);
            WriteRecord(marker);
        };

    Detail::TCrashText beginText;
    Detail::AppendBacktraceBeginText(beginText, entries.size());
    writeMarker(beginText.View());

    for (const auto& entry : entries)
    {
        auto record = entry.Record;
        record.Message = entry.Message;
        record.Category = entry.Category;
        record.Forced = true;
        WriteRecord(record);
    }

    writeMarker(Detail::BacktraceEndText);
}

//---------------------------------------------------------------------------
/*
    TASWLogBase::WriteErrorToStdErr

    Reports a failure when TASWLogConfig::OnError isn't set. Uses the C stream, which stays usable at exit, when the
    singleton loggers are finalized.
*/
void TASWLogBase::WriteErrorToStdErr(const TASWLogError& error) const
{
    const auto line = std::format("ASWLog {}: {}\n", GetLoggerClassName(), error.ToString());
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fflush(stderr);
}

//---------------------------------------------------------------------------

} // namespace ASWLog
