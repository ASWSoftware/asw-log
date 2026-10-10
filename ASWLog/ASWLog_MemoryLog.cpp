/* **************************************************************************
ASWLog_MemoryLog.cpp
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
#include "ASWLog_MemoryLog.h"
//---------------------------------------------------------------------------
// System includes here
#include <algorithm>
#include <mutex>
#include <utility>
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWMemoryLog
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWMemoryLog::~TASWMemoryLog()
{
    Finalize();
}

//---------------------------------------------------------------------------
/*
    TASWMemoryLog::ApplyLimitsUnlocked

    Takes the limits from the current config (TASWLogConfig::Memory), dropping the oldest lines that no longer fit
*/
void TASWMemoryLog::ApplyLimitsUnlocked()
{
    const auto& config = GetConfigUnlocked().Memory;
    std::lock_guard<std::mutex> lock(m_LinesMutex);
    m_MaxLines = config.MaxLines;
    m_MaxBytes = config.MaxBytes;
    DropOldestLines();
}

//---------------------------------------------------------------------------
void TASWMemoryLog::Clear() noexcept
{
    std::lock_guard<std::mutex> lock(m_LinesMutex);
    m_Lines.clear();
    m_ByteCount = 0;
}

//---------------------------------------------------------------------------
bool TASWMemoryLog::CloseUnlocked()
{
    m_IsOpen.store(false);
    m_IsInitialized.store(false);

    return true;
}

//---------------------------------------------------------------------------
// Drops the oldest lines until the kept ones fit the limits
void TASWMemoryLog::DropOldestLines() noexcept
{
    while (!m_Lines.empty() && ((m_MaxLines > 0 && m_Lines.size() > m_MaxLines) || (m_MaxBytes > 0 && m_ByteCount > m_MaxBytes)))
    {
        m_ByteCount -= m_Lines.front().size();
        m_Lines.pop_front();
    }
}

//---------------------------------------------------------------------------
bool TASWMemoryLog::FlushUnlocked()
{
    return m_IsOpen.load();
}

//---------------------------------------------------------------------------
std::size_t TASWMemoryLog::GetLineCount() const noexcept
{
    std::lock_guard<std::mutex> lock(m_LinesMutex);
    return m_Lines.size();
}

//---------------------------------------------------------------------------
std::vector<std::string> TASWMemoryLog::GetLines() const
{
    std::lock_guard<std::mutex> lock(m_LinesMutex);
    return std::vector<std::string>(m_Lines.begin(), m_Lines.end());
}

//---------------------------------------------------------------------------
TASWMemoryLines TASWMemoryLog::GetLinesSince(std::uint64_t sequence) const
{
    TASWMemoryLines result;
    std::lock_guard<std::mutex> lock(m_LinesMutex);

    // The first line ever kept is 1
    const std::uint64_t first = std::max<std::uint64_t>(sequence, 1);
    const std::uint64_t oldestKept = m_NextSequence - m_Lines.size();
    result.NextSequence = m_NextSequence;

    if (first < oldestKept)
        result.MissedCount = oldestKept - first;

    if (first < m_NextSequence)
    {
        const auto skipped = static_cast<std::size_t>(std::max(first, oldestKept) - oldestKept);
        result.Lines.assign(m_Lines.begin() + static_cast<std::ptrdiff_t>(skipped), m_Lines.end());
    }

    return result;
}

//---------------------------------------------------------------------------
bool TASWMemoryLog::InitializeUnlocked()
{
    ApplyLimitsUnlocked();

    return OpenUnlocked();
}

//---------------------------------------------------------------------------
bool TASWMemoryLog::OpenUnlocked()
{
    m_IsOpen.store(true);
    m_IsInitialized.store(true);

    return true;
}

//---------------------------------------------------------------------------
bool TASWMemoryLog::ReconfigureUnlocked(const TASWLogConfig& /*previous*/)
{
    ApplyLimitsUnlocked();

    return true;
}

//---------------------------------------------------------------------------
void TASWMemoryLog::WriteLineUnlocked(Level /*level*/, std::string_view line, bool endsLine)
{
    // Kept without the line ending the formatted line ends with
    if (endsLine)
    {
        const std::string_view ending = GetConfigUnlocked().Line.Ending == LineEnding::CRLF ? "\r\n" : "\n";
        if (line.ends_with(ending))
            line.remove_suffix(ending.size());
    }

    // Made before taking the lock; a failed allocation drops the entry (see TASWTextLogBase::WriteRecord())
    std::string text(line);
    std::lock_guard<std::mutex> lock(m_LinesMutex);

    if (m_MaxBytes > 0)
        Detail::CutToFit(text, m_MaxBytes);

    const std::size_t size = text.size();
    m_Lines.push_back(std::move(text));
    m_ByteCount += size;
    ++m_NextSequence;
    DropOldestLines();
}

//---------------------------------------------------------------------------

} // namespace ASWLog
