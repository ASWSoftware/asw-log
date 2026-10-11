/* **************************************************************************
ASWLog_Types.cpp
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
#include "ASWLog_Types.h"
//---------------------------------------------------------------------------
#include <algorithm>
#include <format>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
std::optional<AsyncOverflowPolicy> AsyncOverflowPolicy_FromString(std::string_view str) noexcept
{
    if (Detail::EqualsIgnoringCase(str, "BLOCK"))
        return AsyncOverflowPolicy::Block;

    if (Detail::EqualsIgnoringCase(str, "DROP_NEWEST") || Detail::EqualsIgnoringCase(str, "DROPNEWEST"))
        return AsyncOverflowPolicy::DropNewest;

    return std::nullopt;
}

//---------------------------------------------------------------------------
std::optional<ColorMode> ColorMode_FromString(std::string_view str) noexcept
{
    if (Detail::EqualsIgnoringCase(str, "AUTO"))
        return ColorMode::Auto;

    if (Detail::EqualsIgnoringCase(str, "ALWAYS"))
        return ColorMode::Always;

    if (Detail::EqualsIgnoringCase(str, "NEVER"))
        return ColorMode::Never;

    return std::nullopt; // Return empty optional if the string is not recognized
}

//---------------------------------------------------------------------------
std::optional<FlushMode> FlushMode_FromString(std::string_view str) noexcept
{
    if (Detail::EqualsIgnoringCase(str, "EVERY_WRITE") || Detail::EqualsIgnoringCase(str, "EVERYWRITE"))
        return FlushMode::EveryWrite;

    if (Detail::EqualsIgnoringCase(str, "ON_NEW_LINE") || Detail::EqualsIgnoringCase(str, "ONNEWLINE"))
        return FlushMode::OnNewLine;

    if (Detail::EqualsIgnoringCase(str, "MANUAL"))
        return FlushMode::Manual;

    if (Detail::EqualsIgnoringCase(str, "PERIODIC"))
        return FlushMode::Periodic;

    return std::nullopt;
}

//---------------------------------------------------------------------------
std::optional<Level> Level_FromString(std::string_view str) noexcept
{
    if (Detail::EqualsIgnoringCase(str, "TRACE"))
        return Level::Trace;

    if (Detail::EqualsIgnoringCase(str, "DEBUG"))
        return Level::Debug;

    if (Detail::EqualsIgnoringCase(str, "INFO"))
        return Level::Info;

    if (Detail::EqualsIgnoringCase(str, "WARN") || Detail::EqualsIgnoringCase(str, "WARNING"))
        return Level::Warn;

    if (Detail::EqualsIgnoringCase(str, "ERROR"))
        return Level::Error;

    if (Detail::EqualsIgnoringCase(str, "CRITICAL") || Detail::EqualsIgnoringCase(str, "FATAL"))
        return Level::Critical;

    if (Detail::EqualsIgnoringCase(str, "OFF") || Detail::EqualsIgnoringCase(str, "NONE"))
        return Level::Off;

    return std::nullopt; // Return empty optional if the string is not recognized
}

//---------------------------------------------------------------------------
std::optional<LineEnding> LineEnding_FromString(std::string_view str) noexcept
{
    if (Detail::EqualsIgnoringCase(str, "LF") || Detail::EqualsIgnoringCase(str, "LINUX"))
        return LineEnding::LF;

    if (Detail::EqualsIgnoringCase(str, "CRLF") || Detail::EqualsIgnoringCase(str, "WINDOWS"))
        return LineEnding::CRLF;

    return std::nullopt; // Return empty optional if the string is not recognized
}

//---------------------------------------------------------------------------
std::optional<MultilineMode> MultilineMode_FromString(std::string_view str) noexcept
{
    if (Detail::EqualsIgnoringCase(str, "PRESERVE"))
        return MultilineMode::Preserve;

    if (Detail::EqualsIgnoringCase(str, "INDENT"))
        return MultilineMode::Indent;

    if (Detail::EqualsIgnoringCase(str, "ESCAPE"))
        return MultilineMode::Escape;

    return std::nullopt;
}

//---------------------------------------------------------------------------
std::optional<SyslogFacility> SyslogFacility_FromString(std::string_view str) noexcept
{
    for (const auto facility : { SyslogFacility::User, SyslogFacility::Daemon, SyslogFacility::Local0, SyslogFacility::Local1, SyslogFacility::Local2, SyslogFacility::Local3, SyslogFacility::Local4, SyslogFacility::Local5, SyslogFacility::Local6, SyslogFacility::Local7 })
    {
        if (Detail::EqualsIgnoringCase(str, SyslogFacility_ToString(facility)))
            return facility;
    }

    return std::nullopt;
}

//---------------------------------------------------------------------------
std::optional<TimePrecision> TimePrecision_FromString(std::string_view str) noexcept
{
    if (Detail::EqualsIgnoringCase(str, "MILLISECONDS") || Detail::EqualsIgnoringCase(str, "MS"))
        return TimePrecision::Milliseconds;

    if (Detail::EqualsIgnoringCase(str, "MICROSECONDS") || Detail::EqualsIgnoringCase(str, "US"))
        return TimePrecision::Microseconds;

    if (Detail::EqualsIgnoringCase(str, "NANOSECONDS") || Detail::EqualsIgnoringCase(str, "NS"))
        return TimePrecision::Nanoseconds;

    return std::nullopt;
}

//---------------------------------------------------------------------------
std::optional<TimeZone> TimeZone_FromString(std::string_view str) noexcept
{
    if (Detail::EqualsIgnoringCase(str, "UTC"))
        return TimeZone::UTC;

    if (Detail::EqualsIgnoringCase(str, "LOCAL"))
        return TimeZone::Local;

    return std::nullopt;
}

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWLogError
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
std::string TASWLogError::ToString() const
{
    std::string text(ErrorKind_ToString(Kind));
    text += ": ";
    text += Message;

    if (!Path.empty())
        text += std::format(" '{}'", PathToUTF8String(Path));

    if (Code)
        text += std::format(": {}", Code.message());

    if (SuppressedCount > 0)
        text += std::format(" ({} more not reported)", SuppressedCount);

    return text;
}

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWPendingEntry
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWPendingEntry::TASWPendingEntry(const TASWLogRecord& record) noexcept
    : m_Record(record)
{
}

//---------------------------------------------------------------------------
const TASWLogValue* TASWPendingEntry::FindField(std::string_view key) const noexcept
{
    const TASWLogValue* value = nullptr;
    m_Record.ForEachField([key, &value](const TASWLogField& field) {
            if (field.Key == key)
                value = &field.Value;
        });

    return value;
}

//---------------------------------------------------------------------------
const TASWLogRecord& TASWPendingEntry::GetRecord() const noexcept
{
    return m_Record;
}

//---------------------------------------------------------------------------
void TASWPendingEntry::RemoveField(std::string_view key)
{
    if (FindField(key) == nullptr)
        return;

    std::vector<TASWLogField> fields;
    m_Record.ForEachField([key, &fields](const TASWLogField& field) {
            if (field.Key != key)
                fields.push_back(field);
        });

    ReplaceFields(fields);
}

//---------------------------------------------------------------------------
/*
    TASWPendingEntry::ReplaceFields

    Makes 'fields' (copied; they may refer to the current copies) the entry's only fields, in place of its own and its
    scopes'. The copies they replace are kept until the entry ends, so a ForEachField() under way can finish.
*/
void TASWPendingEntry::ReplaceFields(std::span<const TASWLogField> fields)
{
    Detail::TOwnedFields replacement;
    replacement.Assign(fields);

    if (!m_Fields.Get().empty())
        m_ReplacedFields.push_back(std::move(m_Fields));

    m_Fields = std::move(replacement);
    m_FieldsView = m_Fields.Get();
    m_Record.Fields = &m_FieldsView;
    m_Record.Scope = nullptr;
}

//---------------------------------------------------------------------------
void TASWPendingEntry::SetField(std::string_view key, const TASWLogValue& value)
{
    std::vector<TASWLogField> fields;
    bool isReplaced = false;
    m_Record.ForEachField([key, &value, &fields, &isReplaced](const TASWLogField& field) {
            if (field.Key == key)
            {
                fields.push_back(TASWLogField{ field.Key, value });
                isReplaced = true;
            }
            else
            {
                fields.push_back(field);
            }
        });

    if (!isReplaced)
        fields.push_back(TASWLogField{ key, value });

    ReplaceFields(fields);
}

//---------------------------------------------------------------------------
void TASWPendingEntry::SetMessage(std::string message) noexcept
{
    m_Message = std::move(message);
    m_Record.Message = m_Message;
}

//---------------------------------------------------------------------------

} // namespace ASWLog
