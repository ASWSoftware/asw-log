/* **************************************************************************
ASWLog_ConsoleLog.cpp
Author: Anthony S. West - ASW Software

See header for info.

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

//---------------------------------------------------------------------------
// Module header
#include "ASWLog_ConsoleLog.h"
//---------------------------------------------------------------------------
// System includes here
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif
//---------------------------------------------------------------------------

namespace ASWLog
{

namespace
{

constexpr std::string_view AnsiReset = "\x1b[0m";

// True if the environment variable 'name' is set to a non-empty value.
bool IsEnvironmentVariableSet(const char* name) noexcept
{
#if defined(_WIN32)
    // With no buffer, returns the size the value needs including its terminating null (1 for an empty value), or 0
    // if the variable isn't set. Avoids getenv(), which MSVC warns about.
    return GetEnvironmentVariableA(name, nullptr, 0) > 1;
#else
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0';
#endif
}

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TASWConsoleLog
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TASWConsoleLog::~TASWConsoleLog()
{
    Finalize();
}

//---------------------------------------------------------------------------
bool TASWConsoleLog::CloseUnlocked()
{
    m_IsOpen.store(false, std::memory_order_release);
    m_IsInitialized.store(false, std::memory_order_release);
    return true;
}

//---------------------------------------------------------------------------
std::array<std::string, LevelCount> TASWConsoleLog::DefaultLevelColors()
{
    return {
        "\x1b[90m",   // Trace - bright black / gray
        "\x1b[36m",   // Debug - cyan
        "\x1b[32m",   // Info - green
        "\x1b[33m",   // Warn - yellow
        "\x1b[31m",   // Error - red
        "\x1b[1;91m", // Critical - bold bright red
    };
}

//---------------------------------------------------------------------------
bool TASWConsoleLog::DetectStreamColorSupport(bool isStdErr)
{
#if defined(_WIN32)
    const HANDLE handle = GetStdHandle(isStdErr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
        return false;

    // Fails if the stream isn't a console, e.g. when it's redirected to a file or a pipe
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode))
        return false;

    // Consoles before Windows 10 don't accept virtual terminal processing, and would print the codes literally
    return (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0 ||
        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    // A terminal (not a file or a pipe), and not one that declares it can't show colors
    if (isatty(isStdErr ? STDERR_FILENO : STDOUT_FILENO) == 0)
        return false;

    const char* term = std::getenv("TERM");
    return term == nullptr || std::string_view(term) != "dumb";
#endif
}

//---------------------------------------------------------------------------
ColorMode TASWConsoleLog::GetColorMode() const noexcept
{
    return m_ColorMode.load(std::memory_order_acquire);
}

//---------------------------------------------------------------------------
TASWConsoleLog& TASWConsoleLog::GetInstance()
{
    // Never deleted, so the instance stays usable through static destruction. It is only finalized at exit, by a
    // handler registered right after it is created, which runs where a static instance's destructor would have run.
    static TASWConsoleLog* const instance = new TASWConsoleLog();
    [[maybe_unused]] static const int atExitResult = std::atexit([] {
            instance->Finalize();
        });
    return *instance;
}

//---------------------------------------------------------------------------
std::string TASWConsoleLog::GetLevelColor(Level level) const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return std::string(LevelColorUnlocked(level));
}

//---------------------------------------------------------------------------
bool TASWConsoleLog::InitializeUnlocked()
{
    m_StdOutColorSupported.store(DetectStreamColorSupport(false), std::memory_order_release);
    m_StdErrColorSupported.store(DetectStreamColorSupport(true), std::memory_order_release);
    m_NoColorRequested.store(IsEnvironmentVariableSet("NO_COLOR"), std::memory_order_release);

    return OpenUnlocked();
}

//---------------------------------------------------------------------------
bool TASWConsoleLog::IsColorSupported() const noexcept
{
    return m_StdOutColorSupported.load(std::memory_order_acquire) || m_StdErrColorSupported.load(std::memory_order_acquire);
}


//---------------------------------------------------------------------------
std::string_view TASWConsoleLog::LevelColorUnlocked(Level level) const noexcept
{
    const auto index = static_cast<std::size_t>(level);
    return index < m_LevelColors.size() ? std::string_view(m_LevelColors[index]) : std::string_view{};
}

//---------------------------------------------------------------------------
bool TASWConsoleLog::OpenUnlocked()
{
    m_IsOpen.store(true, std::memory_order_release);
    m_IsInitialized.store(true, std::memory_order_release);
    return true;
}

//---------------------------------------------------------------------------
void TASWConsoleLog::ResetLevelColor(Level level) noexcept
{
    const auto index = static_cast<std::size_t>(level);
    if (index >= m_LevelColors.size())
        return;

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_LevelColors[index] = DefaultLevelColors()[index];
}

//---------------------------------------------------------------------------
void TASWConsoleLog::ResetLevelColors() noexcept
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_LevelColors = DefaultLevelColors();
}

//---------------------------------------------------------------------------
void TASWConsoleLog::SetColorMode(ColorMode colorMode) noexcept
{
    m_ColorMode.store(colorMode, std::memory_order_release);
}

//---------------------------------------------------------------------------
void TASWConsoleLog::SetLevelColor(Level level, std::string colorCode)
{
    const auto index = static_cast<std::size_t>(level);
    if (index >= m_LevelColors.size())
        return;

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_LevelColors[index] = std::move(colorCode);
}

//---------------------------------------------------------------------------
bool TASWConsoleLog::ShouldColorStream(bool isStdErr) const noexcept
{
    switch (GetColorMode())
    {
        case ColorMode::Always:
            return true;

        case ColorMode::Never:
            return false;

        case ColorMode::Auto:
            break;
    }

    const auto& isSupported = isStdErr ? m_StdErrColorSupported : m_StdOutColorSupported;
    return isSupported.load(std::memory_order_acquire) && !m_NoColorRequested.load(std::memory_order_acquire);
}

//---------------------------------------------------------------------------
void TASWConsoleLog::WriteLineUnlocked(Level level, std::string_view line, bool /*endsLine*/)
{
    const bool toStdErr = level >= Level::Warn;
    auto& stream = toStdErr ? std::cerr : std::cout;
    const auto color = ShouldColorStream(toStdErr) ? LevelColorUnlocked(level) : std::string_view{};

    if (!color.empty())
        stream << color << line << AnsiReset;
    else
        stream << line;

    stream.flush();
}

//---------------------------------------------------------------------------

} // namespace ASWLog
