/* **************************************************************************
ASWLog_MultiLog.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Requires C++ 20 or higher.

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

#pragma once

#ifndef ASWLog_MultiLogH
#define ASWLog_MultiLogH
//---------------------------------------------------------------------------
#include <cstddef>
#include <mutex>
#include <source_location>
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWLog_Base.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWMultiLog
//
// Fans each entry out to several independently-owned IASWLog sinks (e.g. a
// TASWFileLog and a TASWConsoleLog), passing the same record, stamped once,
// to each sink's Write(). Registered sinks are non-owning pointers; the caller is
// responsible for their lifetime, which is typically an existing singleton
// (TASWFileLog::GetInstance()) or a longer-lived instance owned elsewhere.
//
// This class's own runtime level (seeded from GetConfig().InitialMinimumLevel
// at Initialize() time, read/write via SetMinimumLevel()/GetMinimumLevel())
// acts as an optional composite-level pre-filter gate, checked before fanning
// Log()/LogRaw() out (default Trace = no extra filtering); LogForce()/
// LogForceRaw() bypass it, matching force semantics elsewhere. Each sink
// still applies its own level independently. SetEnabled(false) on the
// composite stops all fan-out, forced entries included. All other
// fields on this class's config (rotation, retention, banners, OnLogEntry,
// etc.) are inert, since the composite performs no I/O of its own.
/////////////////////////////////////////////////////////////////////////////
class TASWMultiLog : public TASWLogBase
{
private:
    typedef TASWLogBase inherited;

private:
    std::vector<IASWLog*> m_Sinks;
    mutable std::mutex m_ListMutex; // Protects only the sink list; each sink manages its own internal thread-safety.
    // Serializes Initialize()/Close() changes to m_IsInitialized and m_Config. Never held while calling into a sink,
    // which may call back into this logger (e.g. from an OnLogEntry callback).
    std::mutex m_StateMutex;

private:
    template<typename TLogCall>
    void FanOut(const TLogCall& logCall) const noexcept;
    std::vector<IASWLog*> SnapshotSinks() const;

protected:
    std::string_view GetLoggerClassName() const noexcept final
    {
        return "TASWMultiLog";
    }

    // Passes the record, stamped once by Write(), to every registered sink's Write(), so they all show the same time
    void WriteRecord(const TASWLogRecord& record) override;

public:
    TASWMultiLog() = default;
    ~TASWMultiLog() override = default;

    // Registers a sink for fan-out. Returns false if `logger` is this composite itself or is
    // already registered. Returns true if newly added.
    bool AddLogger(IASWLog& logger);

    [[nodiscard]] bool Contains(const IASWLog& logger) const noexcept;

    [[nodiscard]] std::size_t GetLoggerCount() const noexcept;
    [[nodiscard]] std::vector<IASWLog*> GetLoggers() const;

    std::size_t RemoveAllLoggers() noexcept;
    bool RemoveLogger(IASWLog& logger) noexcept;

    // Fans out to every registered sink, tolerating sinks that are already initialized/open (their own Initialize()
    // may correctly return false in that case, e.g. a singleton initialized elsewhere before being added here) rather
    // than treating that as a failure. Returns true only if every sink ends up initialized or open. Returns false without
    // doing anything if this composite is already initialized; after Close() it can be initialized again. Thread-safe.
    bool Initialize(const TASWLogConfig& config) override;

    bool Open() override;
    bool Close() override;
    // Flushes every registered sink, even if one fails, whether or not this composite is enabled. Returns true only if
    // every sink's Flush() succeeds (vacuously true if none are registered).
    bool Flush() noexcept override;
    bool IsOpen() const noexcept override; // True if every registered sink reports open (vacuously true if none are registered).

    // True if this composite's own gate passes (enabled, not Off, minimum level) and at least one registered sink's
    // ShouldLog() is true (false if none are registered).
    bool ShouldLog(Level level) const noexcept override;

};

} // namespace ASWLog

#endif // ASWLog_MultiLogH
