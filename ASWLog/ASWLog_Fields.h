/* **************************************************************************
ASWLog_Fields.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Source for the ASWLog structured fields and scoped context.

Requires C++ 20 or higher.

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

#pragma once

#ifndef ASWLog_FieldsH
#define ASWLog_FieldsH
//---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
//---------------------------------------------------------------------------

namespace ASWLog
{

class TASWLogContext;
class TASWLogScope;

//---------------------------------------------------------------------------

/*
  ValueKind enum

  What a field's value holds (see TASWLogValue).
*/
enum class ValueKind : std::uint8_t
{
    Int, // std::int64_t, from any signed integer
    UInt, // std::uint64_t, from any unsigned integer
    Double, // From any floating-point type
    Bool,
    Text, // A view of the caller's text
};

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWLogValue
//
// A field's value: a signed or unsigned integer, a double, a bool, or text. Made implicitly from those types, so a
// field reads {"orderId", 17}, {"total", 9.99}, {"cached", true} or {"user", userName}. Text is a view of the caller's
// characters (a string literal, std::string or std::string_view), not a copy, so making one never allocates; a logger
// copies what it keeps beyond the call. Characters (char, wchar_t, ...), enums, pointers other than to text, and null
// pointers aren't accepted; turn other types into a number or text first (e.g. with std::format).
/////////////////////////////////////////////////////////////////////////////
class TASWLogValue
{
private:
    template<typename T>
    static constexpr bool IsCharacter = std::is_same_v<T, char> || std::is_same_v<T, wchar_t> || std::is_same_v<T, char8_t> ||
        std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>;

    union
    {
        std::int64_t m_Int;
        std::uint64_t m_UInt;
        double m_Double;
        bool m_Bool;
        const char* m_TextData;
    };

    std::size_t m_TextSize = 0;
    ValueKind m_Kind;

public:
    constexpr TASWLogValue() noexcept
        : m_Int(0),
          m_Kind(ValueKind::Int)
    {
    }

    template<typename T>
    requires(std::is_integral_v<T> && std::is_signed_v<T> && !IsCharacter<T>)
    constexpr TASWLogValue(T value) noexcept
        : m_Int(value),
          m_Kind(ValueKind::Int)
    {
    }

    template<typename T>
    requires(std::is_integral_v<T> && std::is_unsigned_v<T> && !std::is_same_v<T, bool> && !IsCharacter<T>)
    constexpr TASWLogValue(T value) noexcept
        : m_UInt(value),
          m_Kind(ValueKind::UInt)
    {
    }

    template<typename T>
    requires std::is_floating_point_v<T>
    constexpr TASWLogValue(T value) noexcept
        : m_Double(static_cast<double>(value)),
          m_Kind(ValueKind::Double)
    {
    }

    // A template, so that only a bool matches it: a pointer or an enum would otherwise become a bool
    template<typename T>
    requires std::is_same_v<T, bool>
    constexpr TASWLogValue(T value) noexcept
        : m_Bool(value),
          m_Kind(ValueKind::Bool)
    {
    }

    constexpr TASWLogValue(std::string_view value) noexcept
        : m_TextData(value.data()),
          m_TextSize(value.size()),
          m_Kind(ValueKind::Text)
    {
    }

    // Without it, a string literal would become a bool
    constexpr TASWLogValue(const char* value) noexcept
        : TASWLogValue(std::string_view(value != nullptr ? value : ""))
    {
    }

    TASWLogValue(const std::string& value) noexcept
        : TASWLogValue(std::string_view(value))
    {
    }

    template<typename T>
    requires IsCharacter<T>
    TASWLogValue(T) = delete; // A character: use text ("x") or a number (static_cast<int>(c))

    TASWLogValue(std::nullptr_t) = delete;

    // The value, for the kind GetKind() returns
    [[nodiscard]] constexpr bool GetBool() const noexcept
    {
        return m_Bool;
    }

    [[nodiscard]] constexpr double GetDouble() const noexcept
    {
        return m_Double;
    }

    [[nodiscard]] constexpr std::int64_t GetInt() const noexcept
    {
        return m_Int;
    }

    [[nodiscard]] constexpr ValueKind GetKind() const noexcept
    {
        return m_Kind;
    }

    [[nodiscard]] constexpr std::string_view GetText() const noexcept
    {
        return std::string_view(m_TextData, m_TextSize);
    }

    [[nodiscard]] constexpr std::uint64_t GetUInt() const noexcept
    {
        return m_UInt;
    }
};


/////////////////////////////////////////////////////////////////////////////
// TASWLogField
//
// A named value attached to a log entry, by the logging call (e.g. logger.LogInfo("Order placed", {{"orderId", 17}}))
// or by a TASWLogScope. Like the value's text, the key is a view of the caller's characters.
/////////////////////////////////////////////////////////////////////////////
struct TASWLogField
{
    std::string_view Key;
    TASWLogValue Value;
};

//---------------------------------------------------------------------------
// Internals, used by the classes below and the loggers. Not part of the public interface.
//---------------------------------------------------------------------------
namespace Detail
{

// The size of the buffer WriteScalarValue() writes to
inline constexpr std::size_t ScalarValueBufferSize = 32;

//---------------------------------------------------------------------------

/*
    AppendFieldsJSON

    Appends the fields ForEachLogField() gives for 'scope' and 'fields' as a JSON object's member,
    "fields":{"<key>":<value>,...}, followed by a comma, or nothing if there are none. Texts are JSON strings, other
    values as WriteScalarValue() writes them. In its own source file, so the formatters don't inline it into their
    usual path, which has no fields.
*/
void AppendFieldsJSON(std::string& output, const TASWLogScope* scope, std::span<const TASWLogField> fields);

/*
    AppendFieldsText

    Appends the fields ForEachLogField() gives for 'scope' and 'fields' as the text layouts write them,
    <key>=<value> separated by spaces, texts in double quotes and escaped like JSON strings (so a value can't break the
    line or the brackets around it), other values as WriteScalarValue() writes them, between 'prefix' and 'suffix'.
    Appends nothing, and returns false, if there are none. In its own source file, like AppendFieldsJSON().
*/
bool AppendFieldsText(std::string& output, const TASWLogScope* scope, std::span<const TASWLogField> fields,
    std::string_view prefix = {}, std::string_view suffix = {});

/*
    WriteScalarValue

    Writes a value that isn't text into 'buffer' and returns it: an integer in decimal, a double in the shortest form
    that reads back the same (e.g. 9.99), a bool as true or false. For JSON ('isJSON'), a NaN or infinite double, which
    a JSON number can't hold, is written as the string "NaN", "Infinity" or "-Infinity", quotes included. Allocates
    nothing.
*/
[[nodiscard]] std::string_view WriteScalarValue(char (& buffer)[ScalarValueBufferSize], const TASWLogValue& value,
    bool isJSON) noexcept;

/////////////////////////////////////////////////////////////////////////////
// TOwnedFields
//
// A copy of fields that owns their keys and texts (the fields' views refer to its own buffer), for the scopes, a
// captured context, and a logger that keeps an entry beyond the call (the backtrace, an asynchronous OnLogEntry).
/////////////////////////////////////////////////////////////////////////////
class TOwnedFields
{
private:
    std::vector<char> m_Text; // Moving it keeps its buffer, so the views stay valid
    std::vector<TASWLogField> m_Fields;

public:
    TOwnedFields() = default;
    TOwnedFields(const TOwnedFields& other);
    TOwnedFields(TOwnedFields&& other) noexcept = default;
    TOwnedFields& operator=(const TOwnedFields& other);
    TOwnedFields& operator=(TOwnedFields&& other) noexcept = default;

    // Replaces the fields with copies of 'fields'. Throws std::bad_alloc if they can't be copied; the fields are
    // then unchanged.
    void Assign(std::span<const TASWLogField> fields);
    // Replaces the fields with copies of those ForEachLogField() gives for 'scope' and 'fields', each key once, so the
    // copy no longer depends on the thread's scopes. Throws like Assign().
    void AssignMerged(const TASWLogScope* scope, std::span<const TASWLogField> fields);
    void Clear() noexcept;
    [[nodiscard]] std::span<const TASWLogField> Get() const noexcept;
};

// Calls visit(field) for each field of 'scope' and the scopes it is in, from the outermost one in, then for each of
// 'fields', duplicate keys included, until visit returns false; returns false if it did. Allocates nothing.
template<typename TVisit>
bool VisitAllLogFields(const TASWLogScope* scope, std::span<const TASWLogField> fields, TVisit& visit);

} // namespace Detail

//---------------------------------------------------------------------------

/*
    ForEachLogField

    Calls visit(field) once for each key among the fields of 'scope' and the scopes it is in, and 'fields' (an entry's
    own fields, which come last): in the order the keys first appear, from the outermost scope in, each with the value
    of its last appearance, so an inner scope's value beats an outer one's, and the entry's own value beats both.
    Allocates nothing, so a crash handler can use it too. TASWLogRecord::ForEachField() calls it for an entry.
*/
template<typename TVisit>
void ForEachLogField(const TASWLogScope* scope, std::span<const TASWLogField> fields, TVisit&& visit);


/////////////////////////////////////////////////////////////////////////////
// TASWLogContext
//
// A copy of a thread's scope fields (see TASWLogScope::Capture()), to apply on another thread with
// TASWLogScope scope(context);. It owns its keys and texts, so it can be copied and kept as long as needed.
/////////////////////////////////////////////////////////////////////////////
class TASWLogContext
{
private:
    friend class TASWLogScope;

    Detail::TOwnedFields m_Fields; // Each key once, in the order ForEachLogField() gives them

public:
    // Its fields, each key once
    [[nodiscard]] std::span<const TASWLogField> GetFields() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
};


/////////////////////////////////////////////////////////////////////////////
// TASWLogScope
//
// Fields that every entry logged on this thread carries while the scope exists, e.g. at the top of a request handler:
//     ASWLog::TASWLogScope scope{{"requestId", requestId}};
// Every entry the thread logs until the scope ends has requestId, in the helper functions it calls too, so one
// request's entries can be picked out of interleaved traffic. Scopes nest: an inner scope's fields add to the outer
// ones', and replace those with the same key (see ForEachLogField()). They don't cross threads; to take them to
// another thread (e.g. a thread pool), Capture() them and open a scope from the copy there:
//     auto context = ASWLog::TASWLogScope::Capture();
//     pool.Submit([context] { ASWLog::TASWLogScope scope(context); ... });
// An asynchronous logger keeps the fields an entry had when it was logged.
//
// A scope copies its keys and texts, so they may be temporaries. It belongs to the thread that made it and must end
// on it, in the reverse order of making (as local variables do). If the copy can't be made (out of memory), the scope
// has no fields rather than throwing.
/////////////////////////////////////////////////////////////////////////////
class TASWLogScope
{
private:
    const TASWLogScope* m_Parent; // The thread's scope before this one, or null
    Detail::TOwnedFields m_Fields;

public:
    // A copy of the calling thread's scope fields, each key once, to use on another thread. Empty if the thread has no
    // scope, or if the copy can't be made (out of memory).
    [[nodiscard]] static TASWLogContext Capture() noexcept;

    // The calling thread's innermost scope, or null
    [[nodiscard]] static const TASWLogScope* GetCurrent() noexcept;

public:
    explicit TASWLogScope(std::initializer_list<TASWLogField> fields) noexcept;
    // The fields of 'context' (see Capture()), plus 'fields', which replace those with the same key
    explicit TASWLogScope(const TASWLogContext& context, std::initializer_list<TASWLogField> fields = {}) noexcept;
    ~TASWLogScope();

    TASWLogScope(const TASWLogScope&) = delete;
    TASWLogScope& operator=(const TASWLogScope&) = delete;

    // This scope's own fields, as given
    [[nodiscard]] std::span<const TASWLogField> GetFields() const noexcept;

    // The scope this one is in, or null
    [[nodiscard]] const TASWLogScope* GetParent() const noexcept;
};

//---------------------------------------------------------------------------

namespace Detail
{

//---------------------------------------------------------------------------
template<typename TVisit>
bool VisitAllLogFields(const TASWLogScope* scope, std::span<const TASWLogField> fields, TVisit& visit)
{
    if (scope != nullptr && !VisitAllLogFields(scope->GetParent(), scope->GetFields(), visit))
        return false;

    for (const auto& field : fields)
    {
        if (!visit(field))
            return false;
    }

    return true;
}

} // namespace Detail

//---------------------------------------------------------------------------
template<typename TVisit>
void ForEachLogField(const TASWLogScope* scope, std::span<const TASWLogField> fields, TVisit&& visit)
{
    if (scope == nullptr)
    {
        // The usual case, without a scope: only the entry's own fields, which rarely repeat a key
        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            bool isRepeated = false;
            for (std::size_t earlier = 0; earlier < index && !isRepeated; ++earlier)
                isRepeated = fields[earlier].Key == fields[index].Key;

            if (isRepeated)
                continue;

            const TASWLogField* winner = &fields[index];
            for (std::size_t later = index + 1; later < fields.size(); ++later)
            {
                if (fields[later].Key == fields[index].Key)
                    winner = &fields[later];
            }

            visit(*winner);
        }

        return;
    }

    std::size_t position = 0;
    auto visitCandidate = [&](const TASWLogField& candidate) {
            bool isRepeated = false;
            const TASWLogField* winner = &candidate;
            std::size_t index = 0;
            auto findWinner = [&](const TASWLogField& other) {
                    if (other.Key == candidate.Key)
                    {
                        if (index < position)
                        {
                            isRepeated = true;
                            return false;
                        }

                        winner = &other;
                    }

                    ++index;

                    return true;
                };

            Detail::VisitAllLogFields(scope, fields, findWinner);
            ++position;

            if (!isRepeated)
                visit(*winner);

            return true;
        };

    Detail::VisitAllLogFields(scope, fields, visitCandidate);
}

} // namespace ASWLog

#endif // ASWLog_FieldsH
