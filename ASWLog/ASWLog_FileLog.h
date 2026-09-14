/* **************************************************************************
ASWLog_FileLog.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Requires C++ 20 or higher.

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

#pragma once

#ifndef ASWLog_FileLogH
#define ASWLog_FileLogH
//---------------------------------------------------------------------------
#include <fstream>
#include <mutex>
//---------------------------------------------------------------------------
#include "ASWLog_Base.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWFileLog
//
// Used for logging to a file.
/////////////////////////////////////////////////////////////////////////////
class TASWFileLog : public TASWLogBase
{
private:
    typedef TASWLogBase inherited;

private:
    std::ofstream m_FileStream;
    std::mutex m_FileMutex; // Protects file write bounds across multiple threads
    std::string m_LastLogDateStr; // Stores YYYY-MM-DD state to detect structural calendar shifts

private:
    void RotateLogFiles(std::string_view reasonTag);

protected:
    std::string_view GetLoggerClassName() const noexcept final
    {
        return "TASWFileLog";
    }

public: // Static methods
    static TASWFileLog& GetInstance(); // Singleton support for the common static instance

public:
    TASWFileLog() = default;
    ~TASWFileLog();

    bool Initialize(const TASWLogConfig& config) override;
    void Finalize(std::string_view exitMessage = "") override;

    void Log(Level level, std::string_view message, std::source_location loc = std::source_location::current()) override;
};

} // namespace ASWLog

#endif // ASWLog_FileLogH
