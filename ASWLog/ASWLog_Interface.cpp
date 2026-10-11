/* **************************************************************************
ASWLog_Interface.cpp
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
#include "ASWLog_Interface.h"
//---------------------------------------------------------------------------
// System includes here
#include <span>
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// IASWLog
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
void IASWLog::WriteWide(Level level, TASWWideText message, const std::initializer_list<TASWLogField>* fields,
    std::source_location loc, bool raw, bool forced) noexcept
{
    TASWLogRecord record = MakeRecord(level, {}, loc, raw, forced);
    if (!ShouldLog(record))
        return;

    std::string utf8;
    try
    {
        utf8 = message.ToUTF8();
    }
    catch (...)
    {
        return; // Out of memory: the entry is dropped, as Write() drops one it can't write
    }

    record.Message = utf8;

    std::span<const TASWLogField> ownFields;
    if (fields != nullptr)
    {
        ownFields = std::span<const TASWLogField>(fields->begin(), fields->size());
        record.Fields = &ownFields;
    }

    Write(record);
}

//---------------------------------------------------------------------------

} // namespace ASWLog
