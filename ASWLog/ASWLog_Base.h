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
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWLog_Interface.h"
#include "ASWLog_Version.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------
// Internals of the loggers. Not part of the public interface.
//---------------------------------------------------------------------------
namespace Detail
{

/////////////////////////////////////////////////////////////////////////////
// TMutex
//
// A std::mutex that tells ThreadSanitizer when it is destroyed, for the loggers' own locks. libstdc++'s std::mutex
// never destroys its pthread mutex, so ThreadSanitizer would take a later mutex at the same address (e.g. another
// logger's, on a reused stack) for this one, and report lock-order inversions between unrelated loggers. Locked as a
// std::mutex. Its destructor does nothing in other builds.
/////////////////////////////////////////////////////////////////////////////
class TMutex : public std::mutex
{
public:
    TMutex() = default;
    ~TMutex();
};

} // namespace Detail

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
    // The levels packed into m_Levels: the gate level Write() checks first (bits 0-7), the minimum level (8-15) and the
    // backtrace's lowest level (16-23, Off without a backtrace). The gate is the lower of the two, so an entry the
    // backtrace keeps gets past it, except that a minimum of Off lets nothing in.
    static constexpr std::uint32_t PackLevels(Level minimumLevel, Level backtraceLevel) noexcept
    {
        const Level gateLevel = minimumLevel == Level::Off || backtraceLevel >= minimumLevel ? minimumLevel : backtraceLevel;
        return static_cast<std::uint32_t>(gateLevel) | (static_cast<std::uint32_t>(minimumLevel) << 8) |
            (static_cast<std::uint32_t>(backtraceLevel) << 16);
    }

    static constexpr Level GateLevelOf(std::uint32_t levels) noexcept
    {
        return static_cast<Level>(levels & 0xFF);
    }

    static constexpr Level MinimumLevelOf(std::uint32_t levels) noexcept
    {
        return static_cast<Level>((levels >> 8) & 0xFF);
    }

    static constexpr Level BacktraceLevelOf(std::uint32_t levels) noexcept
    {
        return static_cast<Level>((levels >> 16) & 0xFF);
    }

private:
    // See PackLevels(). One atomic, so that SetMinimumLevel() and a new backtrace config can't leave a gate level that
    // matches neither; read once by each Write().
    std::atomic<std::uint32_t> m_Levels{ PackLevels(Level::Info, Level::Off) };
    std::atomic<bool> m_IsEnabled{ true };

    // An entry the backtrace keeps (see TASWBacktraceConfig): the record, with its message and category copied (the
    // record's Message and Category are empty)
    struct TBacktraceEntry
    {
        TASWLogRecord Record;
        std::string Message;
        std::string Category;
    };

    // The backtrace, guarded by m_BacktraceMutex: at most m_BacktraceCapacity entries, the oldest at m_BacktraceOldest
    // (0 until it is full, then a new entry replaces it). Lock order: a derived logger's own lock before this one.
    mutable Detail::TMutex m_BacktraceMutex;
    std::vector<TBacktraceEntry> m_Backtrace;
    std::size_t m_BacktraceOldest = 0;
    std::size_t m_BacktraceCapacity = 0;
    std::atomic<bool> m_HasBacktrace{ false }; // m_Backtrace isn't empty; read without the lock for each written entry
    std::atomic<Level> m_BacktraceDumpLevel{ Level::Off }; // TASWBacktraceConfig::DumpAtLevel

    // The config, an immutable snapshot that SetConfig() replaces as a whole (never null). The pointer is changed with
    // m_ConfigMutex held, by a thread that also holds the derived logger's own lock; GetConfig() reads it with
    // m_ConfigMutex held, and the derived logger with its own lock held (see GetConfigUnlocked()).
    mutable Detail::TMutex m_ConfigMutex;
    std::shared_ptr<const TASWLogConfig> m_Config{ std::make_shared<const TASWLogConfig>() };

    // When ReportError() last reported each ErrorKind, and how many it left out since (see
    // TASWLogConfig::ErrorReportInterval). Guarded by m_ErrorReportMutex.
    struct TErrorReportState
    {
        std::chrono::system_clock::time_point LastReport{};
        std::size_t SuppressedCount = 0;
        bool HasReported = false;
    };

    Detail::TMutex m_ErrorReportMutex;
    std::array<TErrorReportState, ErrorKindCount> m_ErrorReportStates{};

private:
    void ApplyBacktraceConfig(const TASWBacktraceConfig& config) noexcept;
    void KeepInBacktrace(const TASWLogRecord& record);
    std::vector<TBacktraceEntry> TakeBacktrace() noexcept;
    void WriteBacktrace();
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

    // The minimum level 'record' needs: its category's level if set (see TASWLogRecord::CategoryLevel), else this
    // logger's
    Level GetMinimumLevelFor(const TASWLogRecord& record) const noexcept
    {
        return record.CategoryLevel ? *record.CategoryLevel : GetMinimumLevel();
    }

    // Whether this logger keeps a backtrace when its config asks for one (see TASWBacktraceConfig), read by SetConfig().
    // True by default; a logger that passes its entries on to other loggers, which keep their own (a multi-log),
    // returns false, and its gate is then its minimum level alone.
    virtual bool KeepsBacktrace() const noexcept;

    // An ErrorKind::Exception error for the exception being handled, so call it in a catch block: 'action' (e.g.
    // "Dropped an entry") followed by the exception's what(). Can throw (e.g. out of memory).
    static TASWLogError MakeExceptionError(std::string_view action);

    // This logger's own level check for a non-forced entry, without locking: enabled, 'level' isn't Off, and it meets
    // the minimum level or the backtrace keeps it (see TASWBacktraceConfig). Non-virtual, so it costs no virtual call
    // (see ShouldLog()).
    bool PassesLevelGate(Level level) const noexcept
    {
        return m_IsEnabled.load(std::memory_order_relaxed) && level != Level::Off &&
            level >= GateLevelOf(m_Levels.load(std::memory_order_relaxed));
    }

    // This logger's check for 'record', as Write() makes it (see ShouldLog(const TASWLogRecord&)): a forced entry
    // needs only to be enabled and not Off; a category's level, if set, takes the minimum level's place, and the
    // backtrace's level still lets in what it keeps.
    bool PassesLevelGate(const TASWLogRecord& record) const noexcept
    {
        if (record.Forced)
            return record.LogLevel != Level::Off && IsEnabled();

        if (!record.CategoryLevel)
            return PassesLevelGate(record.LogLevel);

        const Level backtraceLevel = BacktraceLevelOf(m_Levels.load(std::memory_order_relaxed));

        return m_IsEnabled.load(std::memory_order_relaxed) && record.LogLevel != Level::Off &&
            record.LogLevel >= GateLevelOf(PackLevels(*record.CategoryLevel, backtraceLevel));
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

    // For a crash handler (see TASWTextLogBase): if the backtrace's lock is free, calls visit(record, index, count) for
    // each kept entry, oldest first ('record' is marked Forced, and its Message and Category refer to the kept copies,
    // valid during the call), then forgets them if 'mustClear', and returns true. Returns false at once if the lock is
    // busy (e.g. the crashed thread holds it). Allocates nothing itself, except that clearing frees the copies, so with
    // a visitor that doesn't allocate and 'mustClear' false, it can run in a signal handler.
    template<typename TVisit>
    bool VisitBacktraceForCrash(TVisit&& visit, bool mustClear)
    {
        std::unique_lock<std::mutex> lock(m_BacktraceMutex, std::try_to_lock);
        if (!lock.owns_lock())
            return false;

        const auto count = m_Backtrace.size();
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto& entry = m_Backtrace[(m_BacktraceOldest + index) % count];
            TASWLogRecord record = entry.Record;
            record.Message = entry.Message;
            record.Category = entry.Category;
            record.Forced = true;
            visit(static_cast<const TASWLogRecord&>(record), index, count);
        }

        if (mustClear)
        {
            m_Backtrace.clear();
            m_BacktraceOldest = 0;
            m_HasBacktrace.store(false, std::memory_order_relaxed);
        }

        return true;
    }

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

    // Writes and forgets the backtrace, through WriteRecord() like any entry (see IASWLog::DumpBacktrace()). Not final:
    // a multi-log passes it on instead.
    void DumpBacktrace() noexcept override;

    // Lock-free runtime level gate. Initialize() seeds this from the config's InitialMinimumLevel
    // (Reconfigure() doesn't); afterward this atomic is the authoritative value used by Write()
    // to skip locking entirely for filtered entries.
    Level GetMinimumLevel() const noexcept final
    {
        return MinimumLevelOf(m_Levels.load(std::memory_order_relaxed));
    }

    void SetMinimumLevel(Level level) noexcept final
    {
        // Keeps the backtrace's level, and recomputes the gate from both
        auto levels = m_Levels.load(std::memory_order_relaxed);
        while (!m_Levels.compare_exchange_weak(levels, PackLevels(level, BacktraceLevelOf(levels)), std::memory_order_relaxed))
        {
        }
    }

    bool ShouldLog(Level level) const noexcept override
    {
        return PassesLevelGate(level);
    }

    bool ShouldLog(const TASWLogRecord& record) const noexcept override
    {
        return PassesLevelGate(record);
    }

public:
    // Applies this logger's checks (enabled, not Level::Off, and the minimum level, or record.CategoryLevel if set,
    // unless record.Forced), then stamps the record (see StampRecord()) and passes it to WriteRecord(). A filtered
    // entry is never stamped. An entry below that minimum level that the backtrace keeps (see TASWBacktraceConfig) is
    // stamped and kept instead; one at or above its DumpAtLevel first passes the kept entries to WriteRecord(), between
    // the two marker lines. If WriteRecord() throws, the entry is dropped and reported (see ReportError()).
    void Write(const TASWLogRecord& record) noexcept final;
};

} // namespace ASWLog

#endif // ASWLog_BaseH
