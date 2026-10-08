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
#include <format>
//---------------------------------------------------------------------------
#include "ASWLog_Version.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

std::string MakeCategoryName(std::string_view name, const IASWLog& log);

//---------------------------------------------------------------------------

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
