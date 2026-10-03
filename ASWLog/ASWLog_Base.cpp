/* **************************************************************************
ASWLog_Base.cpp
Author: Anthony S. West - ASW Software

See header for info.

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

//---------------------------------------------------------------------------
// Module header
#include "ASWLog_Base.h"
//---------------------------------------------------------------------------
// System includes here
#include <iomanip>
#include <iostream>
#include <sstream>
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h" // For GetCurrentOSProcessId(), GetCurrentOSThreadId()
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWLogBase
/////////////////////////////////////////////////////////////////////////////

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
    }
}

//---------------------------------------------------------------------------

} // namespace ASWLog
