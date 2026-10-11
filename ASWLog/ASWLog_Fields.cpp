/* **************************************************************************
ASWLog_Fields.cpp
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
#include "ASWLog_Fields.h"
//---------------------------------------------------------------------------
// System includes here
#include <atomic>
#include <charconv>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

// The calling thread's innermost scope. Constant-initialized (no dynamic initialization on a thread's first use), so a
// crash handler can read it, even in a POSIX signal handler.
thread_local const TASWLogScope* CurrentScope = nullptr;

// How many scopes exist, on all threads. GetCurrent() reads CurrentScope only while there are some: reading a
// thread_local costs a library call with some compilers (MinGW's GCC emulates thread-local storage), which added ~65 ns
// to every written entry. A thread sees its own scopes counted, which is all GetCurrent() needs, so relaxed is enough.
std::atomic<std::size_t> LiveScopeCount{ 0 };

//---------------------------------------------------------------------------

void AppendFieldValue(std::string& output, const TASWLogValue& value, bool isJSON);

//---------------------------------------------------------------------------

/*
  AppendFieldValue

  Appends a field's value: text as a JSON string (in the text layouts too), anything else as Detail::WriteScalarValue()
  writes it
*/
void AppendFieldValue(std::string& output, const TASWLogValue& value, bool isJSON)
{
    if (value.GetKind() == ValueKind::Text)
    {
        JSON::AppendString(output, value.GetText());
        return;
    }

    char buffer[Detail::ScalarValueBufferSize];
    output.append(Detail::WriteScalarValue(buffer, value, isJSON));
}

} // namespace

//---------------------------------------------------------------------------

namespace Detail
{

/////////////////////////////////////////////////////////////////////////////
// TOwnedFields
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TOwnedFields::TOwnedFields(const TOwnedFields& other)
{
    Assign(other.Get());
}

//---------------------------------------------------------------------------
TOwnedFields& TOwnedFields::operator=(const TOwnedFields& other)
{
    if (this != &other)
        Assign(other.Get());

    return *this;
}

//---------------------------------------------------------------------------
void TOwnedFields::Assign(std::span<const TASWLogField> fields)
{
    std::size_t textSize = 0;
    for (const auto& field : fields)
    {
        textSize += field.Key.size();
        if (field.Value.GetKind() == ValueKind::Text)
            textSize += field.Value.GetText().size();
    }

    // Built aside and swapped in, so a failed allocation leaves the fields as they were (and 'fields' may be this
    // object's own)
    std::vector<char> text;
    text.reserve(textSize);
    std::vector<TASWLogField> copies;
    copies.reserve(fields.size());

    // 'text' never grows past its reserve, so the views into it stay valid
    const auto copyText = [&text](std::string_view source) {
            const std::size_t start = text.size();
            text.insert(text.end(), source.begin(), source.end());

            return std::string_view(text.data() + start, source.size());
        };

    for (const auto& field : fields)
    {
        TASWLogField copy{ copyText(field.Key), field.Value };
        if (field.Value.GetKind() == ValueKind::Text)
            copy.Value = TASWLogValue(copyText(field.Value.GetText()));

        copies.push_back(copy);
    }

    m_Text.swap(text);
    m_Fields.swap(copies);
}

//---------------------------------------------------------------------------
void TOwnedFields::AssignMerged(const TASWLogScope* scope, std::span<const TASWLogField> fields)
{
    if (scope == nullptr && fields.empty())
    {
        Clear();
        return;
    }

    std::vector<TASWLogField> merged;
    ForEachLogField(scope, fields, [&merged](const TASWLogField& field) {
                merged.push_back(field);
            });
    Assign(merged);
}

//---------------------------------------------------------------------------
void TOwnedFields::Clear() noexcept
{
    m_Fields.clear();
    m_Text.clear();
}

//---------------------------------------------------------------------------
std::span<const TASWLogField> TOwnedFields::Get() const noexcept
{
    return m_Fields;
}

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// Free functions
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
void AppendFieldsJSON(std::string& output, const TASWLogScope* scope, std::span<const TASWLogField> fields)
{
    const std::size_t start = output.size();
    bool hasField = false;

    output += "\"fields\":{";
    ForEachLogField(scope, fields, [&output, &hasField](const TASWLogField& field) {
                if (hasField)
                    output += ',';

                JSON::AppendString(output, field.Key);
                output += ':';
                AppendFieldValue(output, field.Value, true);
                hasField = true;
            });

    if (!hasField)
    {
        output.resize(start);
        return;
    }

    output += "},";
}

//---------------------------------------------------------------------------
bool AppendFieldsText(std::string& output, const TASWLogScope* scope, std::span<const TASWLogField> fields,
    std::string_view prefix, std::string_view suffix)
{
    bool hasField = false;

    ForEachLogField(scope, fields, [&output, &hasField, prefix](const TASWLogField& field) {
                output.append(hasField ? std::string_view(" ") : prefix);
                output.append(field.Key);
                output += '=';
                AppendFieldValue(output, field.Value, false);
                hasField = true;
            });

    if (hasField)
        output.append(suffix);

    return hasField;
}

//---------------------------------------------------------------------------
std::string_view WriteScalarValue(char (& buffer)[ScalarValueBufferSize], const TASWLogValue& value, bool isJSON) noexcept
{
    char* const end = buffer + ScalarValueBufferSize;
    std::to_chars_result result{ buffer, std::errc() };

    switch (value.GetKind())
    {
        case ValueKind::Int:
            result = std::to_chars(buffer, end, value.GetInt());
            break;

        case ValueKind::UInt:
            result = std::to_chars(buffer, end, value.GetUInt());
            break;

        case ValueKind::Double:
        {
            const double number = value.GetDouble();
            if (isJSON && !std::isfinite(number))
                return std::isnan(number) ? "\"NaN\"" : (number > 0 ? "\"Infinity\"" : "\"-Infinity\"");

            result = std::to_chars(buffer, end, number);
            break;
        }

        case ValueKind::Bool:
            return value.GetBool() ? "true" : "false";

        case ValueKind::Text:
            break;
    }

    return std::string_view(buffer, static_cast<std::size_t>(result.ptr - buffer));
}

//---------------------------------------------------------------------------

} // namespace Detail

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWLogContext
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
std::span<const TASWLogField> TASWLogContext::GetFields() const noexcept
{
    return m_Fields.Get();
}

//---------------------------------------------------------------------------
bool TASWLogContext::IsEmpty() const noexcept
{
    return m_Fields.Get().empty();
}

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWLogScope
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWLogScope::TASWLogScope(std::initializer_list<TASWLogField> fields) noexcept
    : m_Parent(CurrentScope)
{
    try
    {
        m_Fields.Assign(std::span<const TASWLogField>(fields.begin(), fields.size()));
    }
    catch (...)
    {
    }

    LiveScopeCount.fetch_add(1, std::memory_order_relaxed);
    CurrentScope = this;
}

//---------------------------------------------------------------------------
TASWLogScope::TASWLogScope(const TASWLogContext& context, std::initializer_list<TASWLogField> fields) noexcept
    : m_Parent(CurrentScope)
{
    try
    {
        // The context's fields first, so that 'fields' come later and replace those with the same key
        std::vector<TASWLogField> combined(context.GetFields().begin(), context.GetFields().end());
        combined.insert(combined.end(), fields.begin(), fields.end());
        m_Fields.Assign(combined);
    }
    catch (...)
    {
    }

    LiveScopeCount.fetch_add(1, std::memory_order_relaxed);
    CurrentScope = this;
}

//---------------------------------------------------------------------------
TASWLogScope::~TASWLogScope()
{
    // Ended out of order, a scope leaves the inner one current rather than reinstating one that has ended
    if (CurrentScope == this)
        CurrentScope = m_Parent;

    LiveScopeCount.fetch_sub(1, std::memory_order_relaxed);
}

//---------------------------------------------------------------------------
TASWLogContext TASWLogScope::Capture() noexcept
{
    TASWLogContext context;

    try
    {
        context.m_Fields.AssignMerged(CurrentScope, {});
    }
    catch (...)
    {
        context.m_Fields.Clear();
    }

    return context;
}

//---------------------------------------------------------------------------
const TASWLogScope* TASWLogScope::GetCurrent() noexcept
{
    if (LiveScopeCount.load(std::memory_order_relaxed) == 0)
        return nullptr;

    return CurrentScope;
}

//---------------------------------------------------------------------------
std::span<const TASWLogField> TASWLogScope::GetFields() const noexcept
{
    return m_Fields.Get();
}

//---------------------------------------------------------------------------
const TASWLogScope* TASWLogScope::GetParent() const noexcept
{
    return m_Parent;
}

//---------------------------------------------------------------------------

} // namespace ASWLog
