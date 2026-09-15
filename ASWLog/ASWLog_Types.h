/* **************************************************************************
ASWLog_Types.h
Author: Anthony S. West - ASW Software

A light-weight logging tool.

Source for the ASWLog types.

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

#ifndef ASWLog_TypesH
#define ASWLog_TypesH
//---------------------------------------------------------------------------
#include <cstdint>
#include <optional>
#include <string_view>
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------

enum class FlushMode
{
    EveryWrite,
    OnNewLine,
    Manual,
    Periodic,
};

[[nodiscard]] std::optional<FlushMode> FlushMode_FromString(std::string_view str) noexcept;

[[nodiscard]] constexpr std::string_view FlushMode_ToString(FlushMode flushMode) noexcept
{
    switch (flushMode)
    {
        case FlushMode::EveryWrite:
            return "EVERY_WRITE";

        case FlushMode::OnNewLine:
            return "ON_NEW_LINE";

        case FlushMode::Manual:
            return "MANUAL";

        case FlushMode::Periodic:
            return "PERIODIC";
    }

    return "UNKNOWN";
}

//---------------------------------------------------------------------------

/*
  Level enum

  Defines the severity thresholds for the logging system.

  Log levels are ordered by increasing severity.
  Filtering logic logs messages where: message_level >= logger_configured_minimum_level.
*/
enum class Level : std::uint8_t
{
    // Use for granular, step-by-step program execution details for low-level diagnostics, etc.
    // Extremely high volume.
    Trace = 0,

    // Use for diagnosing issues, verifying configurations, or tracking system variables.
    Debug = 1,

    // Use to track normal operational health (e.g., service start/stop, user login).
    // Should be used as the standard, safe default runtime level for production environments.
    Info = 2,

    // Use for unexpected or unusual anomalies that do not break current execution flow.
    // The system can self-recover, but the event warrants attention from operations.
    // Examples include network retries, high resource usage, or deprecated API usage.
    Warn = 3,

    // Use for severe operational failures preventing a specific task or request from completing.
    // Requires manual intervention or a code patch to resolve, but the application remains alive.
    // Examples include database connection failures, missing critical files, or caught exceptions.
    Error = 4,

    // Use for catastrophic, unrecoverable system crashes or massive data corruption events.
    // The application is unable to continue safely and will usually abort immediately.
    // Examples include out-of-memory states, hardware faults, or failed sanity checks.
    Critical = 5,
};

[[nodiscard]] std::optional<Level> Level_FromString(std::string_view str) noexcept;

[[nodiscard]] constexpr std::string_view Level_ToString(Level level) noexcept
{
    switch (level)
    {
        case Level::Trace:
            return "TRACE";

        case Level::Debug:
            return "DEBUG";

        case Level::Info:
            return "INFO";

        case Level::Warn:
            return "WARN";

        case Level::Error:
            return "ERROR";

        case Level::Critical:
            return "CRITICAL";
    }

    return "UNKNOWN";
}

//---------------------------------------------------------------------------

enum class LineEnding
{
    LF,
    CRLF,
};

[[nodiscard]] std::optional<LineEnding> LineEnding_FromString(std::string_view str) noexcept;

[[nodiscard]] constexpr std::string_view LineEnding_ToString(LineEnding lineEnding) noexcept
{
    switch (lineEnding)
    {
        case LineEnding::LF:
            return "LF";

        case LineEnding::CRLF:
            return "CRLF";
    }

    return "UNKNOWN";
}

//---------------------------------------------------------------------------

} // namespace ASWLog

#endif // #ifndef ASWLog_TypesH
