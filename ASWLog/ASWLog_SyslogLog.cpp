/* **************************************************************************
ASWLog_SyslogLog.cpp
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
#include "ASWLog_SyslogLog.h"
//---------------------------------------------------------------------------
// System includes here
#include <cstdlib>

#if !defined(_WIN32)
#include <limits>
#include <mutex>
#include <utility>

#include <syslog.h>
#endif
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

// The platform has a syslog to send to
#if defined(_WIN32)
constexpr bool HasSyslog = false;
#else
constexpr bool HasSyslog = true;
#endif

// The severities of RFC 5424 that the levels map to, the same values as <syslog.h>'s LOG_CRIT to LOG_DEBUG
constexpr int SeverityCritical = 2;
constexpr int SeverityError    = 3;
constexpr int SeverityWarning  = 4;
constexpr int SeverityInfo     = 6;
constexpr int SeverityDebug    = 7;

#if !defined(_WIN32)
static_assert(SeverityCritical == LOG_CRIT && SeverityError == LOG_ERR && SeverityWarning == LOG_WARNING);
static_assert(SeverityInfo == LOG_INFO && SeverityDebug == LOG_DEBUG);
static_assert(LOG_USER == static_cast<int>(SyslogFacility::User) << 3);
static_assert(LOG_DAEMON == static_cast<int>(SyslogFacility::Daemon) << 3);
static_assert(LOG_LOCAL0 == static_cast<int>(SyslogFacility::Local0) << 3);
static_assert(LOG_LOCAL7 == static_cast<int>(SyslogFacility::Local7) << 3);
#endif

//---------------------------------------------------------------------------

int ToSyslogFacility(SyslogFacility facility) noexcept;
int ToSyslogPriority(Level level, SyslogFacility facility) noexcept;

//---------------------------------------------------------------------------

/*
  ToSyslogFacility

  The facility as syslog() and openlog() take it, e.g. LOG_USER
*/
int ToSyslogFacility(SyslogFacility facility) noexcept
{
    return static_cast<int>(facility) << 3;
}

/*
  ToSyslogPriority

  The priority syslog() takes for an entry at 'level': the facility and the level's severity combined
*/
int ToSyslogPriority(Level level, SyslogFacility facility) noexcept
{
    int severity = SeverityDebug; // Trace and Debug (an entry at Off is never written)

    switch (level)
    {
        case Level::Info:
            severity = SeverityInfo;
            break;

        case Level::Warn:
            severity = SeverityWarning;
            break;

        case Level::Error:
            severity = SeverityError;
            break;

        case Level::Critical:
            severity = SeverityCritical;
            break;

        default:
            break;
    }

    return ToSyslogFacility(facility) | severity;
}

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWSyslogLog
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWSyslogLog::~TASWSyslogLog()
{
    Finalize();
}

//---------------------------------------------------------------------------
bool TASWSyslogLog::CloseUnlocked()
{
    m_IsOpen.store(false);
    m_IsInitialized.store(false);

    return true;
}

//---------------------------------------------------------------------------
bool TASWSyslogLog::FlushUnlocked()
{
    return m_IsOpen.load();
}

//---------------------------------------------------------------------------
TASWSyslogLog& TASWSyslogLog::GetInstance()
{
    // Never deleted, so the instance stays usable through static destruction. It is only finalized at exit, by a
    // handler registered right after it is created, which runs where a static instance's destructor would have run.
    static TASWSyslogLog* const instance = new TASWSyslogLog();
    [[maybe_unused]] static const int atExitResult = std::atexit([] {
            instance->Finalize();
        });
    return *instance;
}

//---------------------------------------------------------------------------
bool TASWSyslogLog::InitializeUnlocked()
{
    OpenSyslogFromConfigUnlocked();

    return OpenUnlocked();
}

//---------------------------------------------------------------------------
bool TASWSyslogLog::OpenUnlocked()
{
    m_IsOpen.store(true);
    m_IsInitialized.store(true);

    return true;
}

//---------------------------------------------------------------------------
// Calls OpenSyslogUnlocked() with the current config's Syslog settings
void TASWSyslogLog::OpenSyslogFromConfigUnlocked()
{
    const auto& config = GetConfigUnlocked().Syslog;
    const std::string ident = config.Ident.empty() ? PathToUTF8String(GetExecutablePath().filename()) : config.Ident;

    OpenSyslogUnlocked(ident, config.Facility);
}

//---------------------------------------------------------------------------
void TASWSyslogLog::OpenSyslogUnlocked(const std::string& ident, SyslogFacility facility)
{
#if defined(_WIN32)
    static_cast<void>(ident);
    static_cast<void>(facility);
#else
    // openlog() keeps the pointer, so the ident is copied and the copy kept until the next call here replaces it. The C
    // library uses the ident only while it holds its syslog lock, as openlog() does, so once openlog() has returned
    // with the new copy, the previous one is no longer used. The current copy and the lock are never destroyed, so
    // syslog() can still use the ident at exit.
    static std::mutex* const identMutex = new std::mutex();
    static std::string* identCopy = nullptr;

    auto* newCopy = new std::string(ident);
    std::lock_guard<std::mutex> lock(*identMutex);
    openlog(newCopy->c_str(), LOG_PID, ToSyslogFacility(facility));
    delete std::exchange(identCopy, newCopy);
#endif
}

//---------------------------------------------------------------------------
bool TASWSyslogLog::PrepareWriteUnlocked(std::chrono::system_clock::time_point /*now*/)
{
    // Before the line is formatted, so nothing is formatted where it would be dropped
    return HasSyslog;
}

//---------------------------------------------------------------------------
bool TASWSyslogLog::ReconfigureUnlocked(const TASWLogConfig& /*previous*/)
{
    OpenSyslogFromConfigUnlocked();

    return true;
}

//---------------------------------------------------------------------------
void TASWSyslogLog::SendToSyslogUnlocked(int priority, std::string_view message)
{
#if defined(_WIN32)
    static_cast<void>(priority);
    static_cast<void>(message);
#else
    // As "%.*s", so the message isn't read as a format string and needs no terminating null
    constexpr std::size_t maxSize = static_cast<std::size_t>(std::numeric_limits<int>::max());
    const int size = static_cast<int>(message.size() < maxSize ? message.size() : maxSize);
    syslog(priority, "%.*s", size, message.data());
#endif
}

//---------------------------------------------------------------------------
bool TASWSyslogLog::ShouldLog(Level level) const noexcept
{
    return HasSyslog && inherited::ShouldLog(level);
}

//---------------------------------------------------------------------------
bool TASWSyslogLog::ShouldLog(const TASWLogRecord& record) const noexcept
{
    return HasSyslog && inherited::ShouldLog(record);
}

//---------------------------------------------------------------------------
void TASWSyslogLog::WriteLineUnlocked(Level level, std::string_view line, bool endsLine)
{
    const auto& config = GetConfigUnlocked();

    // Sent without the line ending the formatted line ends with
    if (endsLine)
    {
        const std::string_view ending = config.Line.Ending == LineEnding::CRLF ? "\r\n" : "\n";
        if (line.ends_with(ending))
            line.remove_suffix(ending.size());
    }

    SendToSyslogUnlocked(ToSyslogPriority(level, config.Syslog.Facility), line);
}

//---------------------------------------------------------------------------

} // namespace ASWLog
