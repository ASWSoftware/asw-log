/* **************************************************************************
ASWLog_WindowsEventLog.cpp
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
#include "ASWLog_WindowsEventLog.h"
//---------------------------------------------------------------------------
// System includes here
#include <cstdlib>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif
//---------------------------------------------------------------------------
#include "ASWLog_Unicode.h"
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

// The platform has a Windows Event Log to report to
#if defined(_WIN32)
constexpr bool HasEventLog = true;

// The registry key that holds the Application log's sources
constexpr std::wstring_view ApplicationLogKey = L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\";

// A message file that ships with Windows (.NET Framework 4) and shows every event ID 0-65535 as its text ("%1")
constexpr std::wstring_view MessageFile = L"%SystemRoot%\\Microsoft.NET\\Framework\\v4.0.30319\\EventLogMessages.dll";
#else
constexpr bool HasEventLog = false;
#endif

//---------------------------------------------------------------------------

std::string ResolveSource(std::string_view source);
TASWWindowsEventLog::EventType ToEventType(Level level) noexcept;

#if defined(_WIN32)
bool ToSourceKey(std::string_view source, std::wstring& key);
#endif

//---------------------------------------------------------------------------

/*
  ResolveSource

  The event source 'source' names: itself, or the executable's file name without its extension if it is empty
*/
std::string ResolveSource(std::string_view source)
{
    return source.empty() ? PathToUTF8String(GetExecutablePath().stem()) : std::string(source);
}

/*
  ToEventType

  The type of an event for an entry at 'level'
*/
TASWWindowsEventLog::EventType ToEventType(Level level) noexcept
{
    if (level >= Level::Error)
        return TASWWindowsEventLog::EventType::Error;

    if (level == Level::Warn)
        return TASWWindowsEventLog::EventType::Warning;

    return TASWWindowsEventLog::EventType::Information;
}

#if defined(_WIN32)
/*
  ToSourceKey

  Sets 'key' to the registry key (under HKEY_LOCAL_MACHINE) of 'source' (see ResolveSource()); false if the name can't
  be a source's (empty, or holding a '\', which would name a key below another source's)
*/
bool ToSourceKey(std::string_view source, std::wstring& key)
{
    const std::string name = ResolveSource(source);

    if (name.empty() || name.find('\\') != std::string::npos)
        return false;

    key = std::wstring(ApplicationLogKey) + UTF8ToWide(name);

    return true;
}
#endif

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWWindowsEventLog
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWWindowsEventLog::~TASWWindowsEventLog()
{
    Finalize();
}

//---------------------------------------------------------------------------
// Closes the source opened by OpenEventSourceUnlocked(), if any
void TASWWindowsEventLog::CloseEventSource() noexcept
{
#if defined(_WIN32)
    if (m_EventSource != nullptr)
        DeregisterEventSource(static_cast<HANDLE>(m_EventSource));
#endif

    m_EventSource = nullptr;
}

//---------------------------------------------------------------------------
bool TASWWindowsEventLog::CloseUnlocked()
{
    CloseEventSource();
    m_IsOpen.store(false);
    m_IsInitialized.store(false);

    return true;
}

//---------------------------------------------------------------------------
bool TASWWindowsEventLog::FlushUnlocked()
{
    return m_IsOpen.load();
}

//---------------------------------------------------------------------------
TASWWindowsEventLog& TASWWindowsEventLog::GetInstance()
{
    // Never deleted, so the instance stays usable through static destruction. It is only finalized at exit, by a
    // handler registered right after it is created, which runs where a static instance's destructor would have run.
    static TASWWindowsEventLog* const instance = new TASWWindowsEventLog();
    [[maybe_unused]] static const int atExitResult = std::atexit([] {
            instance->Finalize();
        });
    return *instance;
}

//---------------------------------------------------------------------------
bool TASWWindowsEventLog::InitializeUnlocked()
{
    return OpenUnlocked();
}

//---------------------------------------------------------------------------
// Calls OpenEventSourceUnlocked() with the current config's source
bool TASWWindowsEventLog::OpenEventSourceFromConfigUnlocked()
{
    return OpenEventSourceUnlocked(ResolveSource(GetConfigUnlocked().EventLog.Source));
}

//---------------------------------------------------------------------------
bool TASWWindowsEventLog::OpenEventSourceUnlocked(const std::string& source)
{
#if defined(_WIN32)
    const HANDLE eventSource = RegisterEventSourceW(nullptr, UTF8ToWide(source).c_str());

    if (eventSource == nullptr)
    {
        TASWLogError error;
        error.Kind = ErrorKind::OpenFailed;
        error.Code = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        error.Message = "Couldn't open the event source '" + source + "'";
        ReportErrorUnlocked(std::move(error));

        return false;
    }

    CloseEventSource();
    m_EventSource = eventSource;
#else
    static_cast<void>(source);
#endif

    return true;
}

//---------------------------------------------------------------------------
bool TASWWindowsEventLog::OpenUnlocked()
{
    if (!OpenEventSourceFromConfigUnlocked())
        return false;

    m_IsOpen.store(true);
    m_IsInitialized.store(true);

    return true;
}

//---------------------------------------------------------------------------
bool TASWWindowsEventLog::PrepareWriteUnlocked(std::chrono::system_clock::time_point /*now*/)
{
    // Before the line is formatted, so nothing is formatted where it would be dropped
    return HasEventLog;
}

//---------------------------------------------------------------------------
bool TASWWindowsEventLog::ReconfigureUnlocked(const TASWLogConfig& /*previous*/)
{
    return OpenEventSourceFromConfigUnlocked();
}

//---------------------------------------------------------------------------
bool TASWWindowsEventLog::RegisterSource(std::string_view source) noexcept
{
#if defined(_WIN32)
    try
    {
        std::wstring key;
        HKEY sourceKey = nullptr;

        if (!ToSourceKey(source, key) || RegCreateKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE, nullptr, &sourceKey, nullptr) != ERROR_SUCCESS)
            return false;

        const DWORD typesSupported = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_INFORMATION_TYPE;
        const bool isRegistered = RegSetValueExW(sourceKey, L"EventMessageFile", 0, REG_EXPAND_SZ,
            reinterpret_cast<const BYTE*>(MessageFile.data()), static_cast<DWORD>((MessageFile.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS &&
            RegSetValueExW(sourceKey, L"TypesSupported", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&typesSupported),
            sizeof(typesSupported)) == ERROR_SUCCESS;
        RegCloseKey(sourceKey);

        return isRegistered;
    }
    catch (...)
    {
        return false;
    }
#else
    static_cast<void>(source);
    return false;
#endif
}

//---------------------------------------------------------------------------
void TASWWindowsEventLog::ReportEventUnlocked(EventType type, std::uint16_t eventId, std::string_view message)
{
#if defined(_WIN32)
    if (m_EventSource == nullptr)
        return;

    const std::wstring text = UTF8ToWide(message);
    const wchar_t* strings[] = { text.c_str() };

    if (!ReportEventW(static_cast<HANDLE>(m_EventSource), static_cast<WORD>(type), 0, eventId, nullptr, 1, 0, strings, nullptr))
    {
        TASWLogError error;
        error.Kind = ErrorKind::WriteFailed;
        error.Code = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        error.Message = "Couldn't report an event to the Windows Event Log";
        ReportErrorUnlocked(std::move(error));
    }
#else
    static_cast<void>(type);
    static_cast<void>(eventId);
    static_cast<void>(message);
#endif
}

//---------------------------------------------------------------------------
bool TASWWindowsEventLog::ShouldLog(Level level) const noexcept
{
    return HasEventLog && inherited::ShouldLog(level);
}

//---------------------------------------------------------------------------
bool TASWWindowsEventLog::ShouldLog(const TASWLogRecord& record) const noexcept
{
    return HasEventLog && inherited::ShouldLog(record);
}

//---------------------------------------------------------------------------
bool TASWWindowsEventLog::UnregisterSource(std::string_view source) noexcept
{
#if defined(_WIN32)
    try
    {
        std::wstring key;

        if (!ToSourceKey(source, key))
            return false;

        const LSTATUS status = RegDeleteKeyW(HKEY_LOCAL_MACHINE, key.c_str());

        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }
    catch (...)
    {
        return false;
    }
#else
    static_cast<void>(source);
    return false;
#endif
}

//---------------------------------------------------------------------------
void TASWWindowsEventLog::WriteLineUnlocked(Level level, std::string_view line, bool endsLine)
{
    const auto& config = GetConfigUnlocked();

    // Reported without the line ending the formatted line ends with
    if (endsLine)
    {
        const std::string_view ending = config.Line.Ending == LineEnding::CRLF ? "\r\n" : "\n";
        if (line.ends_with(ending))
            line.remove_suffix(ending.size());
    }

    const auto levelIndex = static_cast<std::size_t>(level);
    const std::uint16_t eventId = config.EventLog.EventIds[levelIndex < LevelCount ? levelIndex : LevelCount - 1];

    if (line.size() <= MaxMessageSize)
    {
        ReportEventUnlocked(ToEventType(level), eventId, line);
        return;
    }

    std::string text(line);
    Detail::CutToFit(text, MaxMessageSize);
    ReportEventUnlocked(ToEventType(level), eventId, text);
}

//---------------------------------------------------------------------------

} // namespace ASWLog
