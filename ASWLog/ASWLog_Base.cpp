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

namespace ASWLog
{

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWLogBase
/////////////////////////////////////////////////////////////////////////////

// --- Normally methods are alpha-order - these 'Log...' methods are grouped in the order of the 'Level' enum. ---

//---------------------------------------------------------------------------
void TASWLogBase::LogTrace(std::string_view msg, std::source_location loc)
{
    Log(Level::Trace, msg, loc);
}
//---------------------------------------------------------------------------
void TASWLogBase::LogDebug(std::string_view msg, std::source_location loc)
{
    Log(Level::Debug, msg, loc);
}
//---------------------------------------------------------------------------
void TASWLogBase::LogInfo(std::string_view msg, std::source_location loc)
{
    Log(Level::Info, msg, loc);
}
//---------------------------------------------------------------------------
void TASWLogBase::LogWarn(std::string_view msg, std::source_location loc)
{
    Log(Level::Warn, msg, loc);
}
//---------------------------------------------------------------------------
void TASWLogBase::LogError(std::string_view msg, std::source_location loc)
{
    Log(Level::Error, msg, loc);
}
//---------------------------------------------------------------------------
void TASWLogBase::LogCritical(std::string_view msg, std::source_location loc)
{
    Log(Level::Critical, msg, loc);
}
//---------------------------------------------------------------------------

// --- End grouping for 'Log...' methods ---

} // namespace ASWLog
