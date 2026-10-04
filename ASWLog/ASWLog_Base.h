/* **************************************************************************
ASWLog_Base.h
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

#ifndef ASWLog_BaseH
#define ASWLog_BaseH
//---------------------------------------------------------------------------
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <format>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_Interface.h"
#include "ASWLog_Version.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWLogBase
//
// Holds common base methods for log descendants.
/////////////////////////////////////////////////////////////////////////////
class TASWLogBase : public IASWLog
{
private:
    typedef IASWLog inherited;

private:
    std::atomic<Level> m_MinimumLevel{ Level::Info };
    std::atomic<bool> m_IsEnabled{ true };

    // The config, an immutable snapshot that SetConfig() replaces as a whole (never null). The pointer is changed with
    // m_ConfigMutex held, by a thread that also holds the derived logger's own lock; GetConfig() reads it with
    // m_ConfigMutex held, and the derived logger with its own lock held (see GetConfigUnlocked()).
    mutable std::mutex m_ConfigMutex;
    std::shared_ptr<const TASWLogConfig> m_Config{ std::make_shared<const TASWLogConfig>() };

    // When ReportError() last reported each ErrorKind, and how many it left out since (see
    // TASWLogConfig::ErrorReportInterval). Guarded by m_ErrorReportMutex.
    struct TErrorReportState
    {
        std::chrono::system_clock::time_point LastReport{};
        std::size_t SuppressedCount = 0;
        bool HasReported = false;
    };

    std::mutex m_ErrorReportMutex;
    std::array<TErrorReportState, ErrorKindCount> m_ErrorReportStates{};

private:
    void WriteErrorToStdErr(const TASWLogError& error) const;

protected:
    std::atomic<bool> m_IsInitialized{ false };

protected:
    // The current config as a snapshot that stays valid after the lock is released, e.g. to call OnLogEntry outside
    // it. Same locking rule as GetConfigUnlocked().
    std::shared_ptr<const TASWLogConfig> GetConfigSnapshotUnlocked() const noexcept
    {
        return m_Config;
    }

    // The current config, for a derived logger's own code. Call it only while holding the lock under which the logger
    // calls SetConfig() (e.g. TASWTextLogBase's m_Mutex), so the config can't be replaced meanwhile. Elsewhere, use
    // GetConfig().
    const TASWLogConfig& GetConfigUnlocked() const noexcept
    {
        return *m_Config;
    }

    // Pure virtual helper so the base class knows what implementation name to print
    virtual std::string_view GetLoggerClassName() const noexcept = 0;

    // An ErrorKind::Exception error for the exception being handled, so call it in a catch block: 'action' (e.g.
    // "Dropped an entry") followed by the exception's what(). Can throw (e.g. out of memory).
    static TASWLogError MakeExceptionError(std::string_view action);

    // This logger's own level check for a non-forced entry, without locking: enabled, 'level' isn't Off, and it meets
    // the minimum level. Non-virtual, so Write() can check it without another virtual call (see ShouldLog()).
    bool PassesLevelGate(Level level) const noexcept
    {
        return m_IsEnabled.load(std::memory_order_relaxed) && level != Level::Off &&
            level >= m_MinimumLevel.load(std::memory_order_relaxed);
    }

    // The current time, used for log line timestamps, daily rolling, and backup file names. Override it to control
    // the logger's clock, e.g. in tests. A system_clock time point has no time zone (it counts from the UTC epoch).
    virtual std::chrono::system_clock::time_point NowUTC() const noexcept
    {
        return std::chrono::system_clock::now();
    }

    // Reports the exception being handled through ReportError() (see MakeExceptionError()), so call it in a catch
    // block. Never throws.
    void ReportCurrentException(std::string_view action) noexcept;

    // Reports one of this logger's failures to config.OnError, or to stderr if that is empty, unless this logger
    // reported its kind less than config.ErrorReportInterval ago (it is then counted in the next report's
    // SuppressedCount). 'config' is the snapshot in force when the failure happened. Call it without holding the
    // derived logger's own lock, since OnError may call back into the logger. A failure caused by OnError itself, on
    // its own thread, isn't reported. Never throws.
    void ReportError(const TASWLogConfig& config, TASWLogError error) noexcept;
    // The same, with the current config (see GetConfig())
    void ReportError(TASWLogError error) noexcept;

    // Replaces the config with a snapshot of 'config' (see GetConfig()), for Initialize() and Reconfigure(). The caller
    // must hold the derived logger's own lock (see GetConfigUnlocked()). Returns the previous config, so the caller can
    // release it after its lock: destroying it can run other code (e.g. the destructors of OnLogEntry's captures).
    // Throws std::bad_alloc if the snapshot can't be made; the config is then unchanged.
    std::shared_ptr<const TASWLogConfig> SetConfig(const TASWLogConfig& config);

    // Fills in the record's Timestamp (from NowUTC()), ProcessId and ThreadId if they are zero, keeping those already
    // set. Write() calls it; a logger that writes entries of its own (e.g. startup lines) can too.
    void StampRecord(TASWLogRecord& record) const noexcept;

    // Called by Write() with each entry this logger writes: it passed the enabled, Level::Off and minimum level checks
    // (see Write()), and has its Timestamp, ProcessId and ThreadId filled in. Called on the logging thread. May throw:
    // Write() catches the exception and drops the entry, so it never reaches the application.
    virtual void WriteRecord(const TASWLogRecord& record) = 0;

public:
    std::string_view GetVersionStr() const noexcept final
    {
        return Version; // See ASWLog_Version.h
    }

    std::string GetFullVersionStr() const final
    {
        return std::format("{} - Base version {}", GetLoggerClassName(), GetVersionStr());
    }

    std::shared_ptr<const TASWLogConfig> GetConfig() const noexcept final
    {
        std::lock_guard<std::mutex> lock(m_ConfigMutex);
        return m_Config;
    }

    bool IsEnabled() const noexcept final
    {
        return m_IsEnabled.load(std::memory_order_relaxed);
    }

    void SetEnabled(bool enabled) noexcept final
    {
        m_IsEnabled.store(enabled, std::memory_order_relaxed);
    }

    // Lock-free runtime level gate. Initialize() seeds this from the config's InitialMinimumLevel
    // (Reconfigure() doesn't); afterward this atomic is the authoritative value used by Write()
    // to skip locking entirely for filtered entries.
    Level GetMinimumLevel() const noexcept final
    {
        return m_MinimumLevel.load(std::memory_order_relaxed);
    }

    void SetMinimumLevel(Level level) noexcept final
    {
        m_MinimumLevel.store(level, std::memory_order_relaxed);
    }

    bool ShouldLog(Level level) const noexcept override
    {
        return PassesLevelGate(level);
    }

public:
    // Applies this logger's checks (enabled, not Level::Off, and the minimum level unless record.Forced), then stamps
    // the record (see StampRecord()) and passes it to WriteRecord(). A filtered entry is never stamped. If
    // WriteRecord() throws, the entry is dropped and reported (see ReportError()).
    void Write(const TASWLogRecord& record) noexcept final;
};

} // namespace ASWLog

#endif // ASWLog_BaseH
