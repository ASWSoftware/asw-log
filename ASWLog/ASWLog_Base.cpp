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
#include <cstdio>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h" // For GetCurrentOSProcessId(), GetCurrentOSThreadId()
//---------------------------------------------------------------------------

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

/////////////////////////////////////////////////////////////////////////////
// TASWLogBase
/////////////////////////////////////////////////////////////////////////////

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

    std::lock_guard<std::mutex> lock(m_ConfigMutex);
    m_Config.swap(snapshot);
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
void TASWLogBase::Write(const TASWLogRecord& record) noexcept
{
    // Off isn't a severity and a disabled logger writes nothing, even when forced; a forced entry ignores only the
    // minimum level. Checked before stamping, so a filtered entry costs no clock or thread id read.
    const bool isWritten = record.Forced ? record.LogLevel != Level::Off && IsEnabled() : PassesLevelGate(record.LogLevel);
    if (!isWritten)
        return;

    // Stamped now, on the calling thread and before any lock, so the time is the moment of the call
    TASWLogRecord stampedRecord = record;
    StampRecord(stampedRecord);

    // A derived logger's WriteRecord() may throw (e.g. out of memory, or a custom logger's own error); logging must
    // never throw into the application, so the entry is dropped instead
    try
    {
        WriteRecord(stampedRecord);
    }
    catch (...)
    {
        ReportCurrentException("Dropped an entry");
    }
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
