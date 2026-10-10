/* **************************************************************************
ASWLog_NullLog.h
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

#ifndef ASWLog_NullLogH
#define ASWLog_NullLogH
//---------------------------------------------------------------------------
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------
#include "ASWLog_Interface.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWNullLog
//
// A logger that writes nothing, e.g. to turn logging off behind an IASWLog& without changing the code that logs, or to
// pass to code under test:
//     void Sync(ASWLog::IASWLog& log = ASWLog::TASWNullLog::GetInstance());
//
// ShouldLog() and IsEnabled() are always false (SetEnabled() is ignored), so no logging call formats, stamps or copies
// anything, the LogForce*() ones included: a call costs one virtual call. Everything else behaves like a working
// logger, so code that manages its logger keeps working: Initialize(), Reconfigure(), Open(), Close() and Flush()
// return true, IsOpen() is true, GetConfig() returns the config last passed to Initialize() or Reconfigure() (a default
// config before), and the minimum level reads back as set (Initialize() seeds it from InitialMinimumLevel).
//
// Thread-safe.
/////////////////////////////////////////////////////////////////////////////
class TASWNullLog : public IASWLog
{
private:
    typedef IASWLog inherited;

private:
    mutable std::mutex m_ConfigMutex;
    std::shared_ptr<const TASWLogConfig> m_Config; // Never null; guarded by m_ConfigMutex
    std::atomic<Level> m_MinimumLevel{ Level::Info };

private:
    bool StoreConfig(const TASWLogConfig& config) noexcept;

public: // Static methods
    // A shared instance, e.g. for a default argument. Never destroyed, so it stays usable through static destruction.
    static TASWNullLog& GetInstance();

public:
    // Throws std::bad_alloc if the default config can't be made
    TASWNullLog();
    ~TASWNullLog() override = default;

    TASWNullLog(const TASWNullLog&) = delete;
    TASWNullLog& operator=(const TASWNullLog&) = delete;

    std::string_view GetVersionStr() const noexcept override;
    std::string GetFullVersionStr() const override;

    // The config last passed to Initialize() or Reconfigure(), or a default config
    std::shared_ptr<const TASWLogConfig> GetConfig() const noexcept override;

    // Keep the config (see GetConfig()) and return true; false only if it can't be copied (out of memory). Initialize()
    // also sets the minimum level to config.InitialMinimumLevel.
    bool Initialize(const TASWLogConfig& config) noexcept override;
    bool Reconfigure(const TASWLogConfig& config) noexcept override;

    // Do nothing and return true
    bool Open() noexcept override;
    bool Close() noexcept override;
    bool Flush() noexcept override;
    bool IsOpen() const noexcept override;

    // Does nothing (there is no backtrace)
    void DumpBacktrace() noexcept override;

    // Always false: SetEnabled() is ignored, so the LogForce*Fmt() methods never format either
    bool IsEnabled() const noexcept override;
    void SetEnabled(bool enabled) noexcept override;

    // Read back as set, though nothing is written at any level
    Level GetMinimumLevel() const noexcept override;
    void SetMinimumLevel(Level level) noexcept override;

    // Always false
    bool ShouldLog(Level level) const noexcept override;
    bool ShouldLog(const TASWLogRecord& record) const noexcept override;

    // Does nothing
    void Write(const TASWLogRecord& record) noexcept override;
};

} // namespace ASWLog

#endif // #ifndef ASWLog_NullLogH
