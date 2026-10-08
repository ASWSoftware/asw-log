/* **************************************************************************
ASWLog_CategoryLog.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

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

#ifndef ASWLog_CategoryLogH
#define ASWLog_CategoryLogH
//---------------------------------------------------------------------------
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_Interface.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWCategoryLog
//
// A named category of entries, e.g. "Net" or "Db", with a minimum level of its own. It wraps another logger (a file,
// console or multi-log, or another category) and passes every entry on to it, with the category's name in
// TASWLogRecord::Category, which the built-in layout shows after the level, e.g. "[Net]" (see
// TASWLineConfig::ShowCategory):
//     static ASWLog::TASWCategoryLog NetLog("Net", ASWLog::TASWFileLog::GetInstance());
//     NetLog.LogDebugFmt("connected to {}", host);
//
// Until SetMinimumLevel() is called, the category follows the wrapped logger's minimum level. Once set, its level
// replaces the wrapped logger's for this category's entries, both ways: with the file logger at Info,
// NetLog.SetMinimumLevel(Level::Debug) lets Net's Debug entries in, and DbLog.SetMinimumLevel(Level::Warn) keeps Db's
// Info entries out. The wrapped logger's backtrace still keeps the entries below it (see TASWBacktraceConfig), and a
// multi-log's loggers still apply their own minimum levels. ResetMinimumLevel() makes it follow again.
//
// A category wrapping another category adds its name to that one's, e.g. "Net.Http" for HttpLog("Http", NetLog), and
// uses its own level if set, else that category's, and so on up to the wrapped logger's.
//
// The output belongs to the wrapped logger: GetConfig(), Flush(), IsOpen() and DumpBacktrace() pass on to it, while
// Initialize(), Reconfigure(), Open() and Close() do nothing and return false, so a category can't reconfigure or close
// a log the rest of the application uses. SetEnabled(false) silences only this category (and the categories wrapping
// it); the wrapped logger must be enabled too.
//
// Lock-free and thread-safe. The wrapped logger must outlive the category, and must not be a multi-log that holds the
// category. A logger that keeps an entry beyond the call (e.g. in its backtrace) copies the category's name, so the
// category may be destroyed meanwhile.
/////////////////////////////////////////////////////////////////////////////
class TASWCategoryLog : public IASWLog
{
private:
    typedef IASWLog inherited;

private:
    // m_Level's value while the category has no level of its own
    static constexpr std::uint8_t NoOwnLevel = 0xFF;

private:
    IASWLog& m_Log;
    const std::string m_Name;
    std::atomic<std::uint8_t> m_Level{ NoOwnLevel };
    std::atomic<bool> m_IsEnabled{ true };

private:
    TASWLogRecord WithCategory(const TASWLogRecord& record) const noexcept;

public:
    // Shows 'name' as is, or, if 'log' is a category, after that category's name and a '.'. Throws std::bad_alloc if
    // the name can't be copied.
    TASWCategoryLog(std::string_view name, IASWLog& log);
    ~TASWCategoryLog() override = default;

    TASWCategoryLog(const TASWCategoryLog&) = delete;
    TASWCategoryLog& operator=(const TASWCategoryLog&) = delete;

    // The full name, e.g. "Net.Http"
    std::string_view GetName() const noexcept;
    // True once SetMinimumLevel() has given the category a level of its own, until ResetMinimumLevel()
    bool HasOwnMinimumLevel() const noexcept;
    // Makes the category follow the wrapped logger's minimum level again
    void ResetMinimumLevel() noexcept;

    std::string_view GetVersionStr() const noexcept override;
    std::string GetFullVersionStr() const override;

    // The wrapped logger's
    std::shared_ptr<const TASWLogConfig> GetConfig() const noexcept override;

    // Do nothing and return false: the wrapped logger's owner initializes, configures, opens and closes it
    bool Initialize(const TASWLogConfig& config) noexcept override;
    bool Reconfigure(const TASWLogConfig& config) noexcept override;
    bool Open() noexcept override;
    bool Close() noexcept override;

    // Pass on to the wrapped logger (DumpBacktrace() only while this category is enabled)
    bool Flush() noexcept override;
    bool IsOpen() const noexcept override;
    void DumpBacktrace() noexcept override;

    // This category's own: disabled, it passes nothing on, not even forced entries
    bool IsEnabled() const noexcept override;
    void SetEnabled(bool enabled) noexcept override;

    // The category's own level if it has one, else the wrapped logger's
    Level GetMinimumLevel() const noexcept override;
    // Gives the category a level of its own, which replaces the wrapped logger's minimum level for its entries
    void SetMinimumLevel(Level level) noexcept override;

    // Whether the wrapped logger would use an entry passed on by this category (see Write())
    bool ShouldLog(Level level) const noexcept override;
    bool ShouldLog(const TASWLogRecord& record) const noexcept override;

    // Unless this category is disabled, passes the record on to the wrapped logger with the category's name in Category
    // and its own level, if set, in CategoryLevel, where a category wrapping this one hasn't set them already
    void Write(const TASWLogRecord& record) noexcept override;
};

} // namespace ASWLog

#endif // ASWLog_CategoryLogH
