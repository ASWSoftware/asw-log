/* **************************************************************************
ASWLog_Limiter.h
Author: Anthony S. West - ASW Software

A logging library.

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

#ifndef ASWLog_LimiterH
#define ASWLog_LimiterH
//---------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
//---------------------------------------------------------------------------
#include "ASWLog_Fields.h"
#include "ASWLog_Types.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWLogLimiter
//
// Decides which of the calls at one place in the code may log, so that an entry that can repeat often (e.g. in a loop,
// or for every failed retry) doesn't flood the log: only the first call (Once()), every nth call (EveryN()), or at
// most one call per interval (EveryInterval()). Lock-free and thread-safe: calls from several threads share one
// limiter, and exactly the calls it allows are allowed. Usually kept in a static at the place it limits, which is what
// the ASWLOG_ONCE(), ASWLOG_EVERY_N() and ASWLOG_EVERY_INTERVAL() macros below do; it can also be used on its own:
//
//     static ASWLog::TASWLogLimiter limiter = ASWLog::TASWLogLimiter::EveryInterval(std::chrono::seconds(5));
//     if (log.ShouldLog(ASWLog::Level::Warn) && limiter.Allow())
//         log.LogWarnFmt("Retrying {}", host);
//
// Check the level first, as above, so that calls the logger would drop don't count against the limit.
/////////////////////////////////////////////////////////////////////////////
class TASWLogLimiter
{
private:
    enum class Kind
    {
        Once,
        EveryN,
        EveryInterval,
    };

    Kind m_Kind;
    std::uint64_t m_N; // EveryN's n
    std::chrono::steady_clock::rep m_IntervalTicks; // EveryInterval's interval, in steady_clock ticks
    std::atomic<std::uint64_t> m_CallCount{ 0 }; // Once and EveryN: the calls so far
    // EveryInterval: the time (in steady_clock ticks) from which the next call is allowed, and the calls refused since
    // the last one allowed
    std::atomic<std::chrono::steady_clock::rep> m_NextAllowedTicks{ std::numeric_limits<std::chrono::steady_clock::rep>::min() };
    std::atomic<std::uint64_t> m_RefusedCount{ 0 };

    constexpr TASWLogLimiter(Kind kind, std::uint64_t n, std::chrono::steady_clock::rep intervalTicks) noexcept
        : m_Kind(kind),
          m_N(n),
          m_IntervalTicks(intervalTicks)
    {
    }

public: // Static methods
    // Allows at most one call per 'interval', measured on std::chrono::steady_clock from the call it allowed last: the
    // first call, then the first one after the interval has passed. An interval of 0 or less allows every call.
    [[nodiscard]] static constexpr TASWLogLimiter EveryInterval(std::chrono::steady_clock::duration interval) noexcept
    {
        return TASWLogLimiter(Kind::EveryInterval, 0, interval.count());
    }

    // Allows every nth call: the 1st, the n+1st, the 2n+1st, and so on. An n of 0 or 1 allows every call.
    [[nodiscard]] static constexpr TASWLogLimiter EveryN(std::uint64_t n) noexcept
    {
        return TASWLogLimiter(Kind::EveryN, n > 0 ? n : 1, 0);
    }

    // Allows the first call only
    [[nodiscard]] static constexpr TASWLogLimiter Once() noexcept
    {
        return TASWLogLimiter(Kind::Once, 0, 0);
    }

public:
    // Not copyable (the factories above are used to initialize one in place, e.g. 'static auto l = EveryN(10);')
    TASWLogLimiter(const TASWLogLimiter&) = delete;
    TASWLogLimiter& operator=(const TASWLogLimiter&) = delete;

    // True if this call may log
    bool Allow() noexcept;
    // The same; when it allows the call, 'refusedCount' gets how many calls it refused since the one it allowed before
    // (0 for the first, and always 0 for Once()), otherwise 0. With EveryInterval(), a call refused on another thread
    // at the moment this one is allowed may be counted here or with the next allowed call (each refused call once).
    bool Allow(std::uint64_t& refusedCount) noexcept;
    // The same at the time 'now' (for EveryInterval(); the others ignore it), e.g. for tests
    bool Allow(std::uint64_t& refusedCount, std::chrono::steady_clock::time_point now) noexcept;
};

} // namespace ASWLog

//---------------------------------------------------------------------------
// Rate-limited logging at one place in the code. Each macro keeps a static TASWLogLimiter where it is written, and
// logs through logger.LogFmt(level, ...), so the arguments after 'level' are those of LogFmt(): a format string and its
// arguments, optionally preceded by fields, e.g.
//
//     ASWLOG_ONCE(log, ASWLog::Level::Warn, "Config file {} not found, using defaults", path);
//     ASWLOG_EVERY_N(log, 1000, ASWLog::Level::Debug, "Processed {} items", count);
//     ASWLOG_EVERY_INTERVAL(log, std::chrono::seconds(5), ASWLog::Level::Warn, {{"host", host}}, "Retrying");
//
// The message is a format string, so write a literal brace as {{ or }}. The entry keeps the caller's source location.
// Only calls the logger would write count against the limit: ShouldLog(level) is checked first, so a call the level
// filters out costs what a filtered LogFmt() call does, and e.g. ASWLOG_ONCE() at Debug logs the first time the
// logger's level lets Debug through. The arguments are only formatted for an entry that is written.
// ASWLOG_EVERY_INTERVAL() adds a field "suppressed" with the number of calls it skipped since its previous entry, if
// any. 'n' and 'interval' are read the first time the macro runs. In a template, each instantiation has a limiter of
// its own.
//---------------------------------------------------------------------------

// Logs the first time it would be written
#define ASWLOG_ONCE(logger, level, ...) \
        do \
        { \
            static ::ASWLog::TASWLogLimiter aswlogLimiter = ::ASWLog::TASWLogLimiter::Once(); \
            auto& aswlogLogger = (logger); \
            const ::ASWLog::Level aswlogLevel = (level); \
            if (aswlogLogger.ShouldLog(aswlogLevel) && aswlogLimiter.Allow()) \
                aswlogLogger.LogFmt(aswlogLevel, __VA_ARGS__); \
        } \
        while (false)

// Logs every nth time it would be written, starting with the first
#define ASWLOG_EVERY_N(logger, n, level, ...) \
        do \
        { \
            static ::ASWLog::TASWLogLimiter aswlogLimiter = ::ASWLog::TASWLogLimiter::EveryN(n); \
            auto& aswlogLogger = (logger); \
            const ::ASWLog::Level aswlogLevel = (level); \
            if (aswlogLogger.ShouldLog(aswlogLevel) && aswlogLimiter.Allow()) \
                aswlogLogger.LogFmt(aswlogLevel, __VA_ARGS__); \
        } \
        while (false)

// Logs at most once per 'interval' (a std::chrono duration), with a "suppressed" field after skipped calls
#define ASWLOG_EVERY_INTERVAL(logger, interval, level, ...) \
        do \
        { \
            static ::ASWLog::TASWLogLimiter aswlogLimiter = ::ASWLog::TASWLogLimiter::EveryInterval(interval); \
            auto& aswlogLogger = (logger); \
            const ::ASWLog::Level aswlogLevel = (level); \
            ::std::uint64_t aswlogSuppressed = 0; \
            if (aswlogLogger.ShouldLog(aswlogLevel) && aswlogLimiter.Allow(aswlogSuppressed)) \
            { \
                if (aswlogSuppressed == 0) \
                { \
                    aswlogLogger.LogFmt(aswlogLevel, __VA_ARGS__); \
                } \
                else \
                { \
                    const ::ASWLog::TASWLogScope aswlogScope{ { "suppressed", aswlogSuppressed } }; \
                    aswlogLogger.LogFmt(aswlogLevel, __VA_ARGS__); \
                } \
            } \
        } \
        while (false)

//---------------------------------------------------------------------------
#endif // #ifndef ASWLog_LimiterH
