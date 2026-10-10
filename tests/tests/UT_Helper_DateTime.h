/* **************************************************************************
UT_Helper_DateTime.h
Author: Anthony S. West - ASW Software

Date and time helpers shared by the unit tests.

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

#ifndef UT_Helper_DateTimeH
#define UT_Helper_DateTimeH
//---------------------------------------------------------------------------
#include <string>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TScopedTimeZone
//
// While alive, sets the C runtime's local time zone through the TZ environment variable (POSIX format, e.g. "EST5EDT",
// which the Windows C runtime also reads), then restores the previous value. For the tests of local time.
///////////////////////////////////////////////////////////////////////////
class TScopedTimeZone
{
private:
    bool m_HadValue = false;
    std::string m_PreviousValue;

private:
    // Sets TZ to 'value', or removes it if 'value' is null, and makes the C runtime read it again
    static void Apply(const char* value);

public:
    explicit TScopedTimeZone(const char* timeZone);
    ~TScopedTimeZone();

    TScopedTimeZone(const TScopedTimeZone&) = delete;
    TScopedTimeZone& operator=(const TScopedTimeZone&) = delete;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef UT_Helper_DateTimeH
