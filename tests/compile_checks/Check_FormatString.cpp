/* **************************************************************************
Check_FormatString.cpp
Author: Anthony S. West - ASW Software

Compile-time check of the *Fmt format strings, which a unit test can't show: a format string that doesn't match its
arguments must fail to compile. tests/cmake/CMakeLists.txt compiles this file (without linking) once per
ASWLOG_CHECK_CASE value when the tests are configured: case 0 must compile (the control, so a broken setup can't pass
as a rejected format string) and every other case must not.

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

#include <string>
//---------------------------------------------------------------------------
#include "ASWLog_Interface.h"
//---------------------------------------------------------------------------

#if !defined(ASWLOG_CHECK_CASE)
#error Define ASWLOG_CHECK_CASE
#endif

void LogFormattedEntries(ASWLog::IASWLog& logger)
{
#if ASWLOG_CHECK_CASE == 0
    // Matching format strings, and a runtime one wrapped in RuntimeFormat()
    const std::string runtimeFormat = "{} {}";
    logger.LogInfoFmt("{} {}", 1, "text");
    logger.LogFmt(ASWLog::Level::Warn, "{:d}", 2);
    logger.LogErrorFmt(ASWLog::RuntimeFormat(runtimeFormat), 3, 4);
#elif ASWLOG_CHECK_CASE == 1
    logger.LogInfoFmt("{} {}", 1); // Too few arguments
#elif ASWLOG_CHECK_CASE == 2
    logger.LogFmt(ASWLog::Level::Warn, "{:d}", "text"); // Integer format spec for a string
#elif ASWLOG_CHECK_CASE == 3
    const std::string runtimeFormat = "{} {}";
    logger.LogErrorFmt(runtimeFormat, 3, 4); // A runtime format string without RuntimeFormat()
#else
#error Unknown ASWLOG_CHECK_CASE
#endif
}
