/* **************************************************************************
ASWLog_CategoryLog.cpp
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
#include "ASWLog_CategoryLog.h"
//---------------------------------------------------------------------------
// System includes here
#include <algorithm>
#include <format>
#include <mutex>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWLog_Utils.h"
#include "ASWLog_Version.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

// A level TASWCategoryLog::ApplyLevels() gives the categories its name covers
struct TLevelRule
{
    std::string Name; // "*", or a category's name
    Level MinimumLevel = Level::Info;
};

// The categories that exist, and the rules TASWCategoryLog::ApplyLevels() set last
struct TCategoryRegistry
{
    std::mutex Mutex;
    std::vector<TASWCategoryLog*> Categories;
    std::vector<TLevelRule> Rules;
};

//---------------------------------------------------------------------------

const TLevelRule* FindLevelRule(const std::vector<TLevelRule>& rules, std::string_view name) noexcept;
TCategoryRegistry& GetCategoryRegistry();
std::string MakeCategoryName(std::string_view name, const IASWLog& log);
bool ParseLevelRules(std::string_view spec, std::vector<TLevelRule>& rules, std::string& error);

//---------------------------------------------------------------------------

/*
  FindLevelRule

  The rule of 'rules' with the longest name that covers the category 'name' (its own name, the name of a category
  above it, or "*"), or null if none covers it
*/
const TLevelRule* FindLevelRule(const std::vector<TLevelRule>& rules, std::string_view name) noexcept
{
    const TLevelRule* found = nullptr;
    std::size_t foundLength = 0;

    for (const auto& rule : rules)
    {
        const bool isAll = rule.Name == "*";
        const std::size_t length = isAll ? 0 : rule.Name.size();
        const bool covers = isAll || (name.size() >= length && Detail::EqualsIgnoringCase(name.substr(0, length), rule.Name) &&
            (name.size() == length || name[length] == '.'));

        if (covers && (found == nullptr || length > foundLength))
        {
            found = &rule;
            foundLength = length;
        }
    }

    return found;
}

/*
  GetCategoryRegistry

  The process's category registry. Never destroyed, so categories destroyed at exit (e.g. static ones in other files)
  can still leave it.
*/
TCategoryRegistry& GetCategoryRegistry()
{
    static auto* const registry = new TCategoryRegistry();

    return *registry;
}

/*
  MakeCategoryName

  The full name of a category called 'name' wrapping 'log': "<log's name>.<name>" if 'log' is a category, else 'name'
*/
std::string MakeCategoryName(std::string_view name, const IASWLog& log)
{
    const auto* category = dynamic_cast<const TASWCategoryLog*>(&log);
    if (category == nullptr)
        return std::string(name);

    std::string fullName(category->GetName());
    fullName += '.';
    fullName.append(name);
    return fullName;
}

/*
  ParseLevelRules

  Adds the "name=level" items of 'spec' to 'rules' (see TASWCategoryLog::ApplyLevels()), a name given again replacing
  its earlier level. Returns false at the first invalid item, describing it in 'error'.
*/
bool ParseLevelRules(std::string_view spec, std::vector<TLevelRule>& rules, std::string& error)
{
    std::size_t start = 0;

    while (start <= spec.size())
    {
        const auto comma = spec.find(',', start);
        const auto end = comma == std::string_view::npos ? spec.size() : comma;
        const auto item = Detail::TrimSpaces(spec.substr(start, end - start));
        start = end + 1;

        if (item.empty())
            continue;

        const auto equals = item.find('=');
        if (equals == std::string_view::npos)
        {
            error = std::format("'{}' has no '=' (expected name=level)", item);
            return false;
        }

        const auto name = Detail::TrimSpaces(item.substr(0, equals));
        const auto levelText = Detail::TrimSpaces(item.substr(equals + 1));

        if (name.empty())
        {
            error = std::format("'{}' has no category name", item);
            return false;
        }

        if (name != "*" && name.find('*') != std::string_view::npos)
        {
            error = std::format("'{}': a name can't hold '*' (a name covers the categories under it; \"*\" covers all)", item);
            return false;
        }

        const auto level = Level_FromString(levelText);
        if (!level)
        {
            error = std::format("'{}': '{}' isn't a level (Trace, Debug, Info, Warn, Error, Critical or Off)", item, levelText);
            return false;
        }

        const auto existing = std::find_if(rules.begin(), rules.end(), [name](const TLevelRule& rule)
                {
                    return Detail::EqualsIgnoringCase(rule.Name, name);
                });

        if (existing != rules.end())
            existing->MinimumLevel = *level;
        else
            rules.push_back(TLevelRule{ std::string(name), *level });
    }

    return true;
}

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWCategoryLog
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWCategoryLog::TASWCategoryLog(std::string_view name, IASWLog& log)
    : m_Log(log),
      m_Name(MakeCategoryName(name, log))
{
    auto& registry = GetCategoryRegistry();
    std::lock_guard<std::mutex> lock(registry.Mutex);

    registry.Categories.push_back(this);

    if (const auto* rule = FindLevelRule(registry.Rules, m_Name))
        m_Level.store(static_cast<std::uint8_t>(rule->MinimumLevel), std::memory_order_relaxed);
}

//---------------------------------------------------------------------------
TASWCategoryLog::~TASWCategoryLog()
{
    auto& registry = GetCategoryRegistry();
    std::lock_guard<std::mutex> lock(registry.Mutex);

    std::erase(registry.Categories, this);
}

//---------------------------------------------------------------------------
bool TASWCategoryLog::ApplyLevels(std::string_view spec)
{
    std::string error;

    return ApplyLevels(spec, error);
}

//---------------------------------------------------------------------------
bool TASWCategoryLog::ApplyLevels(std::string_view spec, std::string& error)
{
    error.clear();

    std::vector<TLevelRule> rules;
    if (!ParseLevelRules(spec, rules, error))
        return false;

    auto& registry = GetCategoryRegistry();
    std::lock_guard<std::mutex> lock(registry.Mutex);

    registry.Rules = std::move(rules);

    for (auto* category : registry.Categories)
    {
        // Not through SetMinimumLevel(), a virtual call: a category of a derived class may be in its destructor,
        // waiting for the lock to leave the registry
        if (const auto* rule = FindLevelRule(registry.Rules, category->m_Name))
            category->m_Level.store(static_cast<std::uint8_t>(rule->MinimumLevel), std::memory_order_relaxed);
    }

    return true;
}

//---------------------------------------------------------------------------
bool TASWCategoryLog::Close() noexcept
{
    return false;
}

//---------------------------------------------------------------------------
void TASWCategoryLog::DumpBacktrace() noexcept
{
    if (IsEnabled())
        m_Log.DumpBacktrace();
}

//---------------------------------------------------------------------------
bool TASWCategoryLog::Flush() noexcept
{
    return m_Log.Flush();
}

//---------------------------------------------------------------------------
std::shared_ptr<const TASWLogConfig> TASWCategoryLog::GetConfig() const noexcept
{
    return m_Log.GetConfig();
}

//---------------------------------------------------------------------------
std::string TASWCategoryLog::GetFullVersionStr() const
{
    return std::format("TASWCategoryLog - Base version {}", GetVersionStr());
}

//---------------------------------------------------------------------------
Level TASWCategoryLog::GetMinimumLevel() const noexcept
{
    const auto level = m_Level.load(std::memory_order_relaxed);
    return level == NoOwnLevel ? m_Log.GetMinimumLevel() : static_cast<Level>(level);
}

//---------------------------------------------------------------------------
std::string_view TASWCategoryLog::GetName() const noexcept
{
    return m_Name;
}

//---------------------------------------------------------------------------
std::string_view TASWCategoryLog::GetVersionStr() const noexcept
{
    return Version; // See ASWLog_Version.h
}

//---------------------------------------------------------------------------
bool TASWCategoryLog::HasOwnMinimumLevel() const noexcept
{
    return m_Level.load(std::memory_order_relaxed) != NoOwnLevel;
}

//---------------------------------------------------------------------------
bool TASWCategoryLog::Initialize(const TASWLogConfig& /*config*/) noexcept
{
    return false;
}

//---------------------------------------------------------------------------
bool TASWCategoryLog::IsEnabled() const noexcept
{
    return m_IsEnabled.load(std::memory_order_relaxed);
}

//---------------------------------------------------------------------------
bool TASWCategoryLog::IsOpen() const noexcept
{
    return m_Log.IsOpen();
}

//---------------------------------------------------------------------------
bool TASWCategoryLog::Open() noexcept
{
    return false;
}

//---------------------------------------------------------------------------
bool TASWCategoryLog::Reconfigure(const TASWLogConfig& /*config*/) noexcept
{
    return false;
}

//---------------------------------------------------------------------------
void TASWCategoryLog::ResetMinimumLevel() noexcept
{
    m_Level.store(NoOwnLevel, std::memory_order_relaxed);
}

//---------------------------------------------------------------------------
void TASWCategoryLog::SetEnabled(bool enabled) noexcept
{
    m_IsEnabled.store(enabled, std::memory_order_relaxed);
}

//---------------------------------------------------------------------------
void TASWCategoryLog::SetMinimumLevel(Level level) noexcept
{
    m_Level.store(static_cast<std::uint8_t>(level), std::memory_order_relaxed);
}

//---------------------------------------------------------------------------
bool TASWCategoryLog::ShouldLog(Level level) const noexcept
{
    TASWLogRecord record;
    record.LogLevel = level;

    return ShouldLog(record);
}

//---------------------------------------------------------------------------
bool TASWCategoryLog::ShouldLog(const TASWLogRecord& record) const noexcept
{
    return IsEnabled() && m_Log.ShouldLog(WithCategory(record));
}

//---------------------------------------------------------------------------
/*
    TASWCategoryLog::WithCategory

    'record' with this category's name and own level filled in, where a category wrapping this one hasn't set them
*/
TASWLogRecord TASWCategoryLog::WithCategory(const TASWLogRecord& record) const noexcept
{
    TASWLogRecord result = record;
    if (result.Category.empty())
        result.Category = m_Name;

    if (!result.CategoryLevel)
    {
        const auto level = m_Level.load(std::memory_order_relaxed);
        if (level != NoOwnLevel)
            result.CategoryLevel = static_cast<Level>(level);
    }

    return result;
}

//---------------------------------------------------------------------------
void TASWCategoryLog::Write(const TASWLogRecord& record) noexcept
{
    if (IsEnabled())
        m_Log.Write(WithCategory(record));
}

//---------------------------------------------------------------------------

} // namespace ASWLog
