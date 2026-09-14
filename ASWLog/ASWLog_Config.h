/* **************************************************************************
ASWLog_Config.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Source for the ASWLog config options.

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

#ifndef ASWLog_ConfigH
#define ASWLog_ConfigH
//---------------------------------------------------------------------------
#include <cstdint>
#include <filesystem>
#include <string>
//---------------------------------------------------------------------------
#include "ASWLog_Types.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

struct TASWLogConfig
{
    Level MinimumLevel = Level::Info;
    std::filesystem::path LogFilePath;
    std::string BannerMessage;

    // Layout configuration
    bool LogUTCDateTime   = true;
    bool LogLevelStr      = true;
    bool LogProcessId     = true;
    bool LogThreadId      = true;
    bool LogSourceLine    = false;
    bool LogMethodName    = false;

//    bool LogModuleName    = false;
//    std::string ModuleName = "Global";

    // --- Log Rotation and Rolling Options ---
    bool EnableRotation       = false;
    std::uintmax_t MaxFileSizeBytes = 10 * 1024 * 1024; // Default 10MB
    bool EnableDailyRolling   = false; // Rolls file over at midnight
};

} // namespace ASWLog

#endif // #ifndef ASWLog_ConfigH
