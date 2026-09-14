/* **************************************************************************
ASWLog_Types.cpp
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
#include "ASWLog_Types.h"
//---------------------------------------------------------------------------
#include <iomanip>
#include <iostream>
#include <sstream>
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

// Helper to check if two strings match case-insensitively without
// creating a new, modified string allocation on the heap.
bool iequals(std::string_view a, std::string_view b) noexcept
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
        [](char char_a, char char_b)
            {
                // Cast to unsigned char before passing to tolower to avoid UB with negative values
                return std::tolower(static_cast<unsigned char>(char_a)) == std::tolower(static_cast<unsigned char>(char_b));
            });
}

} // namespace

//---------------------------------------------------------------------------

std::optional<Level> Level_FromString(std::string_view str) noexcept
{
    if (iequals(str, "TRACE"))
        return Level::Trace;

    if (iequals(str, "DEBUG"))
        return Level::Debug;

    if (iequals(str, "INFO"))
        return Level::Info;

    if (iequals(str, "WARN") || iequals(str, "WARNING"))
        return Level::Warn;

    if (iequals(str, "ERROR"))
        return Level::Error;

    if (iequals(str, "CRITICAL") || iequals(str, "FATAL"))
        return Level::Critical;

    return std::nullopt; // Return empty optional if the string is not recognized
}

//---------------------------------------------------------------------------

} // namespace ASWLog
