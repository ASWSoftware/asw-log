/* **************************************************************************
ASWLog_DebuggerLog.cpp
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
#include "ASWLog_DebuggerLog.h"
//---------------------------------------------------------------------------
// System includes here
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif
//---------------------------------------------------------------------------
#include "ASWLog_CrashHandler.h"
#include "ASWLog_Unicode.h"
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

// The platform has a debugger output to write to
#if defined(_WIN32)
constexpr bool HasDebuggerOutput = true;
#else
constexpr bool HasDebuggerOutput = false;
#endif

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWDebuggerLog
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWDebuggerLog::~TASWDebuggerLog()
{
    Finalize();
}

//---------------------------------------------------------------------------
bool TASWDebuggerLog::CloseUnlocked()
{
    m_IsOpen.store(false);
    m_IsInitialized.store(false);

    return true;
}

//---------------------------------------------------------------------------
bool TASWDebuggerLog::FlushUnlocked()
{
    return m_IsOpen.load();
}

//---------------------------------------------------------------------------
TASWDebuggerLog& TASWDebuggerLog::GetInstance()
{
    // Never deleted, so the instance stays usable through static destruction. It is only finalized at exit, by a
    // handler registered right after it is created, which runs where a static instance's destructor would have run.
    static TASWDebuggerLog* const instance = new TASWDebuggerLog();
    [[maybe_unused]] static const int atExitResult = std::atexit([] {
            instance->Finalize();
        });
    return *instance;
}

//---------------------------------------------------------------------------
bool TASWDebuggerLog::InitializeUnlocked()
{
    return OpenUnlocked();
}

//---------------------------------------------------------------------------
bool TASWDebuggerLog::OpenUnlocked()
{
    m_IsOpen.store(true);
    m_IsInitialized.store(true);

    return true;
}

//---------------------------------------------------------------------------
bool TASWDebuggerLog::PrepareWriteUnlocked(std::chrono::system_clock::time_point /*now*/)
{
    // Before the line is formatted, so nothing is formatted where it would be dropped
    return HasDebuggerOutput;
}

//---------------------------------------------------------------------------
bool TASWDebuggerLog::ShouldLog(Level level) const noexcept
{
    return HasDebuggerOutput && inherited::ShouldLog(level);
}

//---------------------------------------------------------------------------
bool TASWDebuggerLog::ShouldLog(const TASWLogRecord& record) const noexcept
{
    return HasDebuggerOutput && inherited::ShouldLog(record);
}

//---------------------------------------------------------------------------
void TASWDebuggerLog::WriteCrashLineDirect(std::string_view line) noexcept
{
#if defined(_WIN32)
    // Converted in a buffer on the stack, since the heap may be damaged in a crash
    wchar_t wide[Detail::TCrashText::Capacity + 1];
    const int size = static_cast<int>(line.size() < Detail::TCrashText::Capacity ? line.size() : Detail::TCrashText::Capacity);
    const int wideSize = size > 0 ? MultiByteToWideChar(CP_UTF8, 0, line.data(), size, wide, static_cast<int>(Detail::TCrashText::Capacity)) : 0;
    wide[wideSize > 0 ? wideSize : 0] = L'\0';
    OutputDebugStringW(wide);
#else
    static_cast<void>(line);
#endif
}

//---------------------------------------------------------------------------
void TASWDebuggerLog::WriteLineUnlocked(Level /*level*/, std::string_view line, bool /*endsLine*/)
{
#if defined(_WIN32)
    OutputDebugStringW(UTF8ToWide(line).c_str());
#else
    static_cast<void>(line);
#endif
}

//---------------------------------------------------------------------------

} // namespace ASWLog
