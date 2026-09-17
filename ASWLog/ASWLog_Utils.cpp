/* **************************************************************************
ASWLog_Utils.cpp
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
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------
// System includes here
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#include <process.h>
#else
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif
//---------------------------------------------------------------------------

namespace ASWLog
{

//---------------------------------------------------------------------------
std::string GenerateLogFileName(std::string_view customPostfix)
{
    // Get high-precision time point
    auto now = std::chrono::system_clock::now();
    auto timeTimeT = std::chrono::system_clock::to_time_t(now);

    // Extract millisecond fraction component
    auto durationSinceEpoch = now.time_since_epoch();
    auto secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(durationSinceEpoch);
    auto millisecondsFraction = std::chrono::duration_cast<std::chrono::milliseconds>(durationSinceEpoch - secondsSinceEpoch).count();

    // Unpack time_t into a calendar structure (Windows vs Linux)
    std::tm localCalendarTime{};
#if defined(_WIN32)
    localtime_s(&localCalendarTime, &timeTimeT);
#else
    localtime_r(&timeTimeT, &localCalendarTime);
#endif

    // Get platform Process ID
#if defined(_WIN32)
    auto processId = _getpid();
#else
    auto processId = getpid();
#endif

    auto threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());

    // Format target layout: YYYYMMDD_HHMMSS_mmm
    std::string timeStr = std::format("{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}",
        localCalendarTime.tm_year + 1900,
        localCalendarTime.tm_mon + 1,
        localCalendarTime.tm_mday,
        localCalendarTime.tm_hour,
        localCalendarTime.tm_min,
        localCalendarTime.tm_sec,
        millisecondsFraction);

    return std::format("{}_PID{}_TID{}_{}", timeStr, processId, threadId, customPostfix);
}

//---------------------------------------------------------------------------
std::string GetApplicationInfoString()
{
    auto exePath = GetExecutablePath();
    return std::format("application_exe='{}', command_line='{}'", exePath.string(), GetCommandLineString());
}

//---------------------------------------------------------------------------
std::string GetCommandLineString()
{
#if defined(_WIN32)
    wchar_t* commandLine = GetCommandLineW();
    int requiredSize = WideCharToMultiByte(CP_UTF8, 0, commandLine, -1, nullptr, 0, nullptr, nullptr);
    if (requiredSize <= 0)
        return "";

    std::vector<char> buffer(static_cast<std::size_t>(requiredSize));
    WideCharToMultiByte(CP_UTF8, 0, commandLine, -1, buffer.data(), requiredSize, nullptr, nullptr);

    return std::string(buffer.data());
#else
    std::ifstream cmdFile("/proc/self/cmdline");
    std::string commandLine;
    std::string value;

    while (std::getline(cmdFile, value, '\0'))
    {
        if (value.empty())
            continue;

        if (!commandLine.empty())
            commandLine += ' ';

        commandLine += value;
    }

    return commandLine;
#endif
}

//---------------------------------------------------------------------------
std::string GetDriveInfoString()
{
    std::error_code errorCode;
    auto freeSpace = std::filesystem::space(std::filesystem::current_path(), errorCode);
    const auto totalMiB = freeSpace.capacity / (1024ULL * 1024ULL);
    const auto freeMiB = freeSpace.free / (1024ULL * 1024ULL);
    const auto usedMiB = totalMiB > freeMiB ? totalMiB - freeMiB : 0ULL;

    return std::format("disk_total={} MiB, disk_free={} MiB, disk_used={} MiB", totalMiB, freeMiB, usedMiB);
}

//---------------------------------------------------------------------------
std::filesystem::path GetExecutablePath()
{
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH]{};
    const auto length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length > 0)
        return std::filesystem::path{ buffer };
#else
    std::filesystem::path path("/proc/self/exe");
    std::error_code errorCode;
    auto exePath = std::filesystem::read_symlink(path, errorCode);
    if (!errorCode)
        return exePath;
#endif

    return std::filesystem::path("unknown");
}

//---------------------------------------------------------------------------
TMemoryUsage GetMemoryUsage()
{
    TMemoryUsage usage;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS memoryCounters{};
    memoryCounters.cb = sizeof(memoryCounters);

    if (GetProcessMemoryInfo(GetCurrentProcess(), &memoryCounters, sizeof(memoryCounters)))
    {
        usage.WorkingSetBytes = static_cast<std::size_t>(memoryCounters.WorkingSetSize);
        usage.PeakWorkingSetBytes = static_cast<std::size_t>(memoryCounters.PeakWorkingSetSize);
    }
#else
    std::ifstream statusFile("/proc/self/status");
    std::string line;
    std::uint64_t residentSetKb = 0;
    std::uint64_t peakResidentSetKb = 0;

    while (std::getline(statusFile, line))
    {
        if (line.rfind("VmRSS:", 0) == 0)
            residentSetKb = std::stoull(line.substr(line.find_first_of("0123456789")));
        else if (line.rfind("VmHWM:", 0) == 0)
            peakResidentSetKb = std::stoull(line.substr(line.find_first_of("0123456789")));
    }

    usage.WorkingSetBytes = static_cast<std::size_t>(residentSetKb * 1024ULL);
    usage.PeakWorkingSetBytes = static_cast<std::size_t>(peakResidentSetKb * 1024ULL);
#endif

    return usage;
}

//---------------------------------------------------------------------------
std::string GetMemoryUsageString()
{
    const auto usage = GetMemoryUsage();
    return std::format("working_set={} bytes, peak_working_set={} bytes", usage.WorkingSetBytes, usage.PeakWorkingSetBytes);
}

//---------------------------------------------------------------------------
std::string GetOSInfoString()
{
#if defined(_WIN32)
#if defined(USE_GET_VERSION_EX)
    OSVERSIONINFOEXW versionInfo{};
#else
    using RtlGetVersionFunction = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const auto ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtlGetVersion =
        ntdll == nullptr ? nullptr : reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion"));
    RTL_OSVERSIONINFOW versionInfo{};
#endif
    versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);

#if defined(USE_GET_VERSION_EX)
    if (GetVersionExW(reinterpret_cast<LPOSVERSIONINFOW>(&versionInfo)))
#else
    if (rtlGetVersion != nullptr && rtlGetVersion(&versionInfo) == 0)
#endif
    {
        const auto major = versionInfo.dwMajorVersion;
        const auto minor = versionInfo.dwMinorVersion;
        const auto build = versionInfo.dwBuildNumber;
        SYSTEM_INFO systemInfo{};
        GetSystemInfo(&systemInfo);
        std::string arch = "unknown";

        switch (systemInfo.wProcessorArchitecture)
        {
            case PROCESSOR_ARCHITECTURE_AMD64:
                arch = "x64";
                break;
            case PROCESSOR_ARCHITECTURE_ARM:
                arch = "ARM";
                break;
            case PROCESSOR_ARCHITECTURE_INTEL:
                arch = "x86";
                break;
            default:
                break;
        }

        DWORD editionId = 0;
        std::string edition = "Unknown";

        if (GetProductInfo(major, minor, 0, 0, &editionId))
        {
#if defined(PRODUCT_ULTIMATE)
            if (editionId == PRODUCT_ULTIMATE)
                edition = "Ultimate";
#endif
#if defined(PRODUCT_HOME_BASIC)
            if (editionId == PRODUCT_HOME_BASIC)
                edition = "Home";
#endif
#if defined(PRODUCT_HOME_PREMIUM)
            if (editionId == PRODUCT_HOME_PREMIUM)
                edition = "Home";
#endif
#if defined(PRODUCT_PROFESSIONAL)
            if (editionId == PRODUCT_PROFESSIONAL)
                edition = "Pro";
#endif
#if defined(PRODUCT_PROFESSIONAL_N)
            if (editionId == PRODUCT_PROFESSIONAL_N)
                edition = "Pro N";
#endif
#if defined(PRODUCT_ENTERPRISE)
            if (editionId == PRODUCT_ENTERPRISE)
                edition = "Enterprise";
#endif
#if defined(PRODUCT_ENTERPRISE_N)
            if (editionId == PRODUCT_ENTERPRISE_N)
                edition = "Enterprise N";
#endif
#if defined(PRODUCT_EDUCATION)
            if (editionId == PRODUCT_EDUCATION)
                edition = "Education";
#endif
#if defined(PRODUCT_EDUCATION_N)
            if (editionId == PRODUCT_EDUCATION_N)
                edition = "Education N";
#endif
#if defined(PRODUCT_SERVER)
            if (editionId == PRODUCT_SERVER)
                edition = "Server";
#endif
#if defined(PRODUCT_SERVER_CORE)
            if (editionId == PRODUCT_SERVER_CORE)
                edition = "Server Core";
#endif
        }

        return std::format("Windows {}.{} {} (build {}), arch={}", major, minor, edition, build, arch);
    }

    return "Windows OS information unavailable";
#else
    struct utsname unameData {};

    if (uname(&unameData) == 0)
    {
        return std::format("Linux {} {} {}", unameData.sysname, unameData.release, unameData.machine);
    }

    return "Linux OS information unavailable";
#endif
}

//---------------------------------------------------------------------------
TSystemMemoryUsage GetSystemMemoryUsage()
{
    TSystemMemoryUsage usage;
#if defined(_WIN32)
    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof(memoryStatus);

    if (GlobalMemoryStatusEx(&memoryStatus))
    {
        usage.TotalBytes = memoryStatus.ullTotalPhys;
        usage.AvailableBytes = memoryStatus.ullAvailPhys;
    }
#else
    struct sysinfo info {};

    if (sysinfo(&info) == 0)
    {
        const auto memoryUnit = static_cast<std::uintmax_t>(info.mem_unit);
        usage.TotalBytes = static_cast<std::uintmax_t>(info.totalram) * memoryUnit;
        usage.AvailableBytes = static_cast<std::uintmax_t>(info.freeram) * memoryUnit;
    }
#endif
    return usage;
}

//---------------------------------------------------------------------------
std::string GetSystemMemoryUsageString()
{
    const auto usage = GetSystemMemoryUsage();
    return std::format("total={} bytes, available={} bytes", usage.TotalBytes, usage.AvailableBytes);
}

//---------------------------------------------------------------------------
std::string GetTimeInfoString()
{
    const auto now = std::chrono::system_clock::now();
    const auto timeValue = std::chrono::system_clock::to_time_t(now);

    std::tm utcTime{};
    std::tm localTime{};
#if defined(_WIN32)
    gmtime_s(&utcTime, &timeValue);
    localtime_s(&localTime, &timeValue);
#else
    gmtime_r(&timeValue, &utcTime);
    localtime_r(&timeValue, &localTime);
#endif

    const auto utcSeconds = std::mktime(&utcTime);
    const auto localSeconds = std::mktime(&localTime);
    const auto offsetSeconds = localSeconds - utcSeconds;
    const auto offsetMinutes = offsetSeconds / 60;

    return std::format("utc={}, local={}, offset_minutes={}",
        Time::ToISO8601String(now),
        std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}",
            localTime.tm_year + 1900,
            localTime.tm_mon + 1,
            localTime.tm_mday,
            localTime.tm_hour,
            localTime.tm_min,
            localTime.tm_sec),
        offsetMinutes);
}

//---------------------------------------------------------------------------
bool MatchesWildcard(std::string_view value, std::string_view pattern)
{
    std::size_t valueIndex = 0;
    std::size_t patternIndex = 0;
    std::size_t starIndex = std::string_view::npos;
    std::size_t matchIndex = 0;

    while (valueIndex < value.size())
    {
        if (patternIndex < pattern.size() && pattern[patternIndex] == '*')
        {
            starIndex = patternIndex++;
            matchIndex = valueIndex;
        }
        else if (patternIndex < pattern.size() && pattern[patternIndex] == '?')
        {
            ++patternIndex;
            ++valueIndex;
        }
        else if (patternIndex < pattern.size() && pattern[patternIndex] == value[valueIndex])
        {
            ++patternIndex;
            ++valueIndex;
        }
        else if (starIndex != std::string_view::npos)
        {
            patternIndex = starIndex + 1;
            valueIndex = ++matchIndex;
        }
        else
        {
            return false;
        }
    }

    while (patternIndex < pattern.size() && pattern[patternIndex] == '*')
        ++patternIndex;

    return patternIndex == pattern.size();
}

//---------------------------------------------------------------------------

namespace Time
{

//---------------------------------------------------------------------------
std::string ToISO8601String(std::chrono::system_clock::time_point timePoint)
{
    auto timeTimeT = std::chrono::system_clock::to_time_t(timePoint);
    auto durationSinceEpoch = timePoint.time_since_epoch();
    auto secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(durationSinceEpoch);
    auto millisecondsFraction = std::chrono::duration_cast<std::chrono::milliseconds>(durationSinceEpoch - secondsSinceEpoch).count();

    std::tm utcTime{};
#if defined(_WIN32)
    gmtime_s(&utcTime, &timeTimeT);
#else
    gmtime_r(&timeTimeT, &utcTime);
#endif

    // Use ISO 8601 format with T separator and Z suffix
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
        utcTime.tm_year + 1900,
        utcTime.tm_mon + 1,
        utcTime.tm_mday,
        utcTime.tm_hour,
        utcTime.tm_min,
        utcTime.tm_sec,
        millisecondsFraction);
}

//---------------------------------------------------------------------------
std::string ToDateString(std::chrono::system_clock::time_point timePoint)
{
    auto timeTimeT = std::chrono::system_clock::to_time_t(timePoint);
    std::tm utcTime{};
#if defined(_WIN32)
    gmtime_s(&utcTime, &timeTimeT);
#else
    gmtime_r(&timeTimeT, &utcTime);
#endif

    return std::format("{:04}-{:02}-{:02}",
        utcTime.tm_year + 1900,
        utcTime.tm_mon + 1,
        utcTime.tm_mday);
}

} // namespace Time

//---------------------------------------------------------------------------

} // namespace ASWLog
