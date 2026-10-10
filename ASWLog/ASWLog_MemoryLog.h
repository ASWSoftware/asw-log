/* **************************************************************************
ASWLog_MemoryLog.h
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

#ifndef ASWLog_MemoryLogH
#define ASWLog_MemoryLogH
//---------------------------------------------------------------------------
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWLog_TextLogBase.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWMemoryLines struct
//
// The lines TASWMemoryLog::GetLinesSince() returns.
/////////////////////////////////////////////////////////////////////////////
struct TASWMemoryLines
{
    std::vector<std::string> Lines; // Oldest first
    std::uint64_t NextSequence = 1; // The sequence number to pass to the next GetLinesSince()
    std::uint64_t MissedCount = 0; // Lines from the asked sequence number on that are no longer kept (dropped, cleared)
};


/////////////////////////////////////////////////////////////////////////////
// TASWMemoryLog
//
// A text logger that keeps its newest lines in memory instead of writing them out, e.g. for an in-app log viewer,
// for a crash report (see HandleCrash()), or for tests of code that logs. Each line is formatted like a file logger's
// (TASWLogConfig::Line, including its formatter) and kept without its line ending; a LogRaw() entry written as is is
// kept as given. TASWLogConfig::Memory sets how many lines and bytes it keeps; the oldest go first. Like the other text
// loggers, it writes the startup lines (TASWLogConfig::Startup) and the shutdown line, and supports asynchronous
// writing, OnLogEntry, OnBeforeWrite and the backtrace.
//
// Each kept line has a sequence number, from 1, so a viewer can fetch only the new ones, e.g. on a timer:
//     const auto newLines = memoryLog.GetLinesSince(nextSequence);
//     nextSequence = newLines.NextSequence;
//
// Thread-safe. Close() keeps the lines (entries logged while closed are dropped); Clear() forgets them.
/////////////////////////////////////////////////////////////////////////////
class TASWMemoryLog : public TASWTextLogBase
{
private:
    typedef TASWTextLogBase inherited;

private:
    // The kept lines, guarded by m_LinesMutex (lock order: m_Mutex before it), so reading doesn't wait for a write
    mutable Detail::TMutex m_LinesMutex;
    std::deque<std::string> m_Lines; // Oldest first
    std::size_t m_ByteCount = 0; // Of the kept lines' text
    std::uint64_t m_NextSequence = 1; // The next line's; the oldest kept line's is m_NextSequence - m_Lines.size()
    std::size_t m_MaxLines = 0; // TASWMemoryConfig, as last applied; 0 = no limit
    std::size_t m_MaxBytes = 0;

private:
    void ApplyLimitsUnlocked();
    void DropOldestLines() noexcept; // Holding m_LinesMutex

protected:
    bool CloseUnlocked() override;
    bool FlushUnlocked() override;

    std::string_view GetLoggerClassName() const noexcept override
    {
        return "TASWMemoryLog";
    }

    bool InitializeUnlocked() override;
    bool OpenUnlocked() override;
    bool ReconfigureUnlocked(const TASWLogConfig& previous) override;
    void WriteLineUnlocked(Level level, std::string_view line, bool endsLine) override;

public:
    TASWMemoryLog() = default;
    // Writes the shutdown line (see TASWTextLogBase::Finalize())
    ~TASWMemoryLog() override;

    TASWMemoryLog(const TASWMemoryLog&) = delete;
    TASWMemoryLog& operator=(const TASWMemoryLog&) = delete;

    // Forgets the kept lines. The sequence numbers go on (see GetLinesSince()).
    void Clear() noexcept;

    // How many lines are kept
    std::size_t GetLineCount() const noexcept;

    // A copy of the kept lines, oldest first. Throws std::bad_alloc if they can't be copied.
    std::vector<std::string> GetLines() const;

    // A copy of the kept lines whose sequence number is 'sequence' or higher (the first line ever kept is 1), the
    // sequence number to pass next time, and how many lines from 'sequence' on are no longer kept. Throws
    // std::bad_alloc if they can't be copied.
    TASWMemoryLines GetLinesSince(std::uint64_t sequence) const;
};

} // namespace ASWLog

#endif // #ifndef ASWLog_MemoryLogH
