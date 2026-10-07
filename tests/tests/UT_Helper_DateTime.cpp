/* **************************************************************************
UT_Helper_DateTime.cpp
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
#include "UT_Helper_DateTime.h"
//---------------------------------------------------------------------------
// System includes here
#include <cstdlib>
#include <ctime>

#if defined(_WIN32)
#include <windows.h>
#endif
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TScopedTimeZone
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TScopedTimeZone::TScopedTimeZone(const char* timeZone)
{
#if defined(_WIN32)
    // _putenv_s also updates the process environment, which GetEnvironmentVariableA reads (avoids getenv(), which
    // MSVC warns about)
    char buffer[256]{};
    const DWORD length = GetEnvironmentVariableA("TZ", buffer, sizeof(buffer));
    m_HadValue = length > 0 && length < sizeof(buffer);
    if (m_HadValue)
        m_PreviousValue.assign(buffer, length);
#else
    const char* previous = std::getenv("TZ");
    m_HadValue = previous != nullptr;
    if (m_HadValue)
        m_PreviousValue = previous;
#endif
    Apply(timeZone);
}
//---------------------------------------------------------------------------
TScopedTimeZone::~TScopedTimeZone()
{
    Apply(m_HadValue ? m_PreviousValue.c_str() : nullptr);
}
//---------------------------------------------------------------------------
void TScopedTimeZone::Apply(const char* value)
{
#if defined(_WIN32)
    _putenv_s("TZ", value != nullptr ? value : ""); // An empty value removes the variable
    _tzset();

    // Once the Windows C runtime has read a system time zone without daylight saving time (e.g. UTC on CI machines, or
    // Arizona), it keeps that zone's daylight saving bias of 0 even when TZ then names a zone that has daylight saving
    // time, so summer would be flagged as daylight saving time without moving the clock. TZ can't give the bias, so
    // set the usual hour.
    int hasDaylightSavingTime = 0;
    if (value != nullptr && _get_daylight(&hasDaylightSavingTime) == 0 && hasDaylightSavingTime != 0)
    {
        // __dstbias() is deprecated in favor of _get_dstbias(), which can't set it.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        *__dstbias() = -3600;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    }
#else
    if (value != nullptr)
        setenv("TZ", value, 1);
    else
        unsetenv("TZ");
    tzset();
#endif
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests
