/* **************************************************************************
ASWLog_Utils.cpp
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
#include "ASWLog_Utils.h"
//---------------------------------------------------------------------------
// System includes here
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif
//---------------------------------------------------------------------------

namespace ASWLog
{

#if !defined(_WIN32)
namespace
{

// Returns the number in a /proc/self/status line such as "VmRSS:     1234 kB", or 0 if it has none.
std::uint64_t ParseStatusLineKB(std::string_view line) noexcept
{
    const auto start = line.find_first_of("0123456789");
    if (start == std::string_view::npos)
        return 0;

    std::uint64_t value = 0;
    std::from_chars(line.data() + start, line.data() + line.size(), value);
    return value;
}

} // namespace
#endif

//---------------------------------------------------------------------------
std::string GenerateLogFileName(std::string_view prefix, std::string_view customPostfix)
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

    // Format target layout: YYYYMMDD_HHMMSS_mmm
    std::string timeStr = std::format("{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}",
        localCalendarTime.tm_year + 1900,
        localCalendarTime.tm_mon + 1,
        localCalendarTime.tm_mday,
        localCalendarTime.tm_hour,
        localCalendarTime.tm_min,
        localCalendarTime.tm_sec,
        millisecondsFraction);

    std::string name = prefix.empty() ? std::string() : std::format("{}_", prefix);
    name += std::format("{}_PID{}_TID{}", timeStr, GetCurrentOSProcessId(), GetCurrentOSThreadId());

    if (!customPostfix.empty())
        name += std::format("_{}", customPostfix);

    return name;
}

//---------------------------------------------------------------------------
std::string GetApplicationInfoString()
{
    auto exePath = GetExecutablePath();
    return std::format("application_exe='{}', command_line='{}'", PathToUTF8String(exePath), GetCommandLineString());
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
std::uint32_t GetCurrentOSProcessId() noexcept
{
#if defined(_WIN32)
    return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(getpid());
#endif
}

//---------------------------------------------------------------------------
std::uint32_t GetCurrentOSThreadId() noexcept
{
#if defined(_WIN32)
    return static_cast<std::uint32_t>(GetCurrentThreadId());
#else
    // gettid() itself needs glibc 2.30 or later. Not cached per thread, since a forked child's thread gets a new id.
    return static_cast<std::uint32_t>(syscall(SYS_gettid));
#endif
}

//---------------------------------------------------------------------------
std::string GetDriveInfoString()
{
    std::error_code errorCode;
    const auto currentPath = std::filesystem::current_path(errorCode);
    auto freeSpace = std::filesystem::space(currentPath, errorCode);
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
            residentSetKb = ParseStatusLineKB(line);
        else if (line.rfind("VmHWM:", 0) == 0)
            peakResidentSetKb = ParseStatusLineKB(line);
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
    const auto rtlGetVersionProc = ntdll == nullptr ? nullptr : GetProcAddress(ntdll, "RtlGetVersion");
    // Cast through the generic function pointer type void (*)() so GCC's -Wcast-function-type accepts converting
    // GetProcAddress()'s FARPROC to the real signature.
    const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFunction>(reinterpret_cast<void (*)()>(rtlGetVersionProc));
    // The EX form (accepted by RtlGetVersion when dwOSVersionInfoSize says so) adds wProductType, used below to
    // recognize a server edition that the product type table doesn't name.
    RTL_OSVERSIONINFOEXW versionInfo{};
#endif
    versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);

#if defined(USE_GET_VERSION_EX)
    if (GetVersionExW(reinterpret_cast<LPOSVERSIONINFOW>(&versionInfo)))
#else
    if (rtlGetVersion != nullptr && rtlGetVersion(reinterpret_cast<PRTL_OSVERSIONINFOW>(&versionInfo)) == 0)
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

        DWORD productType = 0;
        if (!GetProductInfo(major, minor, 0, 0, &productType))
            productType = 0; // PRODUCT_UNDEFINED

        const auto edition = GetWindowsEditionName(productType, versionInfo.wProductType != VER_NT_WORKSTATION);

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

    return std::format("utc={}, local={}, offset_minutes={}",
        Time::ToISO8601String(now), Time::ToLocalISO8601String(now), Time::GetUTCOffsetMinutes(now));
}

//---------------------------------------------------------------------------
#if defined(_WIN32)
std::string GetWindowsEditionName(std::uint32_t productType, bool isServer)
{
    std::string edition;

#if defined(PRODUCT_ULTIMATE)
    if (productType == PRODUCT_ULTIMATE)
        edition = "Ultimate";
#endif
#if defined(PRODUCT_HOME_BASIC)
    if (productType == PRODUCT_HOME_BASIC)
        edition = "Home";
#endif
#if defined(PRODUCT_HOME_PREMIUM)
    if (productType == PRODUCT_HOME_PREMIUM)
        edition = "Home";
#endif
#if defined(PRODUCT_CORE)
    if (productType == PRODUCT_CORE)
        edition = "Home";
#endif
#if defined(PRODUCT_CORE_N)
    if (productType == PRODUCT_CORE_N)
        edition = "Home N";
#endif
#if defined(PRODUCT_CORE_SINGLELANGUAGE)
    if (productType == PRODUCT_CORE_SINGLELANGUAGE)
        edition = "Home Single Language";
#endif
#if defined(PRODUCT_CORE_COUNTRYSPECIFIC)
    if (productType == PRODUCT_CORE_COUNTRYSPECIFIC)
        edition = "Home China";
#endif
#if defined(PRODUCT_PROFESSIONAL)
    if (productType == PRODUCT_PROFESSIONAL)
        edition = "Pro";
#endif
#if defined(PRODUCT_PROFESSIONAL_N)
    if (productType == PRODUCT_PROFESSIONAL_N)
        edition = "Pro N";
#endif
#if defined(PRODUCT_PRO_WORKSTATION)
    if (productType == PRODUCT_PRO_WORKSTATION)
        edition = "Pro for Workstations";
#endif
#if defined(PRODUCT_PRO_WORKSTATION_N)
    if (productType == PRODUCT_PRO_WORKSTATION_N)
        edition = "Pro for Workstations N";
#endif
#if defined(PRODUCT_ENTERPRISE)
    if (productType == PRODUCT_ENTERPRISE)
        edition = "Enterprise";
#endif
#if defined(PRODUCT_ENTERPRISE_N)
    if (productType == PRODUCT_ENTERPRISE_N)
        edition = "Enterprise N";
#endif
#if defined(PRODUCT_ENTERPRISE_S)
    if (productType == PRODUCT_ENTERPRISE_S)
        edition = "Enterprise LTSC";
#endif
#if defined(PRODUCT_ENTERPRISE_S_N)
    if (productType == PRODUCT_ENTERPRISE_S_N)
        edition = "Enterprise N LTSC";
#endif
#if defined(PRODUCT_EDUCATION)
    if (productType == PRODUCT_EDUCATION)
        edition = "Education";
#endif
#if defined(PRODUCT_EDUCATION_N)
    if (productType == PRODUCT_EDUCATION_N)
        edition = "Education N";
#endif
#if defined(PRODUCT_STANDARD_SERVER)
    if (productType == PRODUCT_STANDARD_SERVER)
        edition = "Server Standard";
#endif
#if defined(PRODUCT_STANDARD_SERVER_CORE)
    if (productType == PRODUCT_STANDARD_SERVER_CORE)
        edition = "Server Standard Core";
#endif
#if defined(PRODUCT_STANDARD_EVALUATION_SERVER)
    if (productType == PRODUCT_STANDARD_EVALUATION_SERVER)
        edition = "Server Standard Evaluation";
#endif
#if defined(PRODUCT_DATACENTER_SERVER)
    if (productType == PRODUCT_DATACENTER_SERVER)
        edition = "Server Datacenter";
#endif
#if defined(PRODUCT_DATACENTER_SERVER_CORE)
    if (productType == PRODUCT_DATACENTER_SERVER_CORE)
        edition = "Server Datacenter Core";
#endif
#if defined(PRODUCT_DATACENTER_EVALUATION_SERVER)
    if (productType == PRODUCT_DATACENTER_EVALUATION_SERVER)
        edition = "Server Datacenter Evaluation";
#endif
#if defined(PRODUCT_DATACENTER_SERVER_AZURE_EDITION)
    if (productType == PRODUCT_DATACENTER_SERVER_AZURE_EDITION)
        edition = "Server Datacenter: Azure Edition";
#endif

    // Product types not listed above (there are many niche ones) still say whether this is a server, plus the raw
    // product type so the exact edition can be looked up.
    if (edition.empty())
        edition = std::format("{} (product type 0x{:X})", isServer ? "Server" : "Unknown", productType);

    return edition;
}
#endif

//---------------------------------------------------------------------------
bool IsRootFolder(const std::filesystem::path& folder) noexcept
{
    try
    {
#if defined(_WIN32)
        // Asks Windows rather than parsing the path, because standard libraries differ on UNC paths (MinGW's reads
        // "\\server\share" as a relative path). GetFullPathNameW resolves a relative path, ".", ".." and '/'.
        const DWORD fullPathSize = GetFullPathNameW(folder.c_str(), 0, nullptr, nullptr);
        if (fullPathSize == 0)
            return true;

        std::wstring fullPath(fullPathSize, L'\0');
        const DWORD fullPathLength = GetFullPathNameW(folder.c_str(), fullPathSize, fullPath.data(), nullptr);
        if (fullPathLength == 0 || fullPathLength >= fullPathSize)
            return true;

        fullPath.resize(fullPathLength);

        // The mount point of the volume holding the folder, with a trailing separator, e.g. "C:\" or
        // "\\server\share\". It is never longer than the full path plus that separator.
        std::wstring volumePath(fullPath.size() + 2, L'\0');
        if (!GetVolumePathNameW(fullPath.c_str(), volumePath.data(), static_cast<DWORD>(volumePath.size())))
            return true;

        volumePath.resize(std::char_traits<wchar_t>::length(volumePath.c_str()));

        if (fullPath.back() != L'\\')
            fullPath += L'\\';

        return CompareStringOrdinal(fullPath.c_str(), -1, volumePath.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
        // Only a root folder is its own parent. Unlike comparing paths, this also covers "." and "..".
        std::error_code errorCode;
        const bool isOwnParent = std::filesystem::equivalent(folder, folder / "..", errorCode);
        return isOwnParent || errorCode;
#endif
    }
    catch (...)
    {
        return true;
    }
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
std::string PathToUTF8String(const std::filesystem::path& path) noexcept
{
    try
    {
        const auto utf8 = path.u8string();
        return std::string(utf8.begin(), utf8.end());
    }
    catch (...)
    {
        return {};
    }
}

//---------------------------------------------------------------------------
std::error_code RenameWithoutReplacing(const std::filesystem::path& from, const std::filesystem::path& to) noexcept
{
#if defined(_WIN32)
    // Without MOVEFILE_REPLACE_EXISTING, the move fails if the target exists, checked by Windows as part of the move
    if (MoveFileExW(from.c_str(), to.c_str(), 0))
        return {};

    const DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
        return std::make_error_code(std::errc::file_exists);

    return std::error_code(static_cast<int>(error), std::system_category());
#else
    // rename() replaces an existing target, but link() fails if it exists, as one step. Then the old name is removed.
    if (link(from.c_str(), to.c_str()) == 0)
    {
        if (unlink(from.c_str()) == 0)
            return {};

        // Two names for one file would keep the backup growing with the log, so undo the link
        const int unlinkError = errno;
        unlink(to.c_str());

        return std::error_code(unlinkError, std::generic_category());
    }

    const int linkError = errno;
    if (linkError == EEXIST)
        return std::make_error_code(std::errc::file_exists);

    // A file system without hard links (e.g. FAT or some network shares): check, then rename. Another process could
    // still create the target in between there.
    if (linkError == EPERM || linkError == ENOTSUP || linkError == EOPNOTSUPP)
    {
        std::error_code errorCode;
        if (std::filesystem::exists(to, errorCode))
            return std::make_error_code(std::errc::file_exists);

        if (errorCode)
            return errorCode;

        std::filesystem::rename(from, to, errorCode);
        return errorCode;
    }

    return std::error_code(linkError, std::generic_category());
#endif
}

//---------------------------------------------------------------------------

namespace Detail
{

namespace
{

// The line breaks MultilineMode::Escape writes
constexpr std::string_view EscapedCR = "\\r";
constexpr std::string_view EscapedLF = "\\n";

// The line breaks MultilineMode::Indent writes: the line ending and the next line's marker, whose space is left out
// when that line is empty
constexpr std::string_view IndentedCRLF = "\r\n    | ";
constexpr std::string_view IndentedLF = "\n    | ";

// U+FFFD, the replacement character, in UTF-8
constexpr std::string_view ReplacementCharacter = "\xEF\xBF\xBD";

//---------------------------------------------------------------------------

bool HasCROrLF(const char* data) noexcept;
bool IsPlainJSONCharacter(unsigned char character) noexcept;
bool IsPlainJSONWord(const char* data) noexcept;
std::size_t MeasureUTF8Sequence(std::string_view text, std::size_t index, std::size_t& invalidLength) noexcept;

//---------------------------------------------------------------------------

/*
  HasCROrLF

  True if any of the 8 characters at 'data' is a CR or a LF, tested together in a 64-bit word (see IsPlainJSONWord())
*/
bool HasCROrLF(const char* data) noexcept
{
    constexpr std::uint64_t Ones = 0x0101010101010101;
    constexpr std::uint64_t HighBits = 0x8080808080808080;

    std::uint64_t word;
    std::memcpy(&word, data, sizeof(word));

    // The usual test for a zero byte, on the word XORed with each of them
    const std::uint64_t returns = word ^ (Ones * '\r');
    const std::uint64_t lineFeeds = word ^ (Ones * '\n');
    const std::uint64_t found = ((returns - Ones) & ~returns) | ((lineFeeds - Ones) & ~lineFeeds);

    return (found & HighBits) != 0;
}

/*
  IsPlainJSONCharacter

  True for an ASCII character a JSON string holds as it is: not a control character, '"' or '\'
*/
bool IsPlainJSONCharacter(unsigned char character) noexcept
{
    return character >= 0x20 && character < 0x80 && character != '"' && character != '\\';
}

/*
  IsPlainJSONWord

  True if all 8 characters at 'data' are plain (see IsPlainJSONCharacter()), tested together in a 64-bit word, which
  is several times faster than one at a time for the usual messages
*/
bool IsPlainJSONWord(const char* data) noexcept
{
    constexpr std::uint64_t Ones = 0x0101010101010101;
    constexpr std::uint64_t HighBits = 0x8080808080808080;

    std::uint64_t word;
    std::memcpy(&word, data, sizeof(word));

    // A byte's high bit ends up set if it is 0x80 or above, or (for the bytes below 0x80) below 0x20, or equal to '"'
    // or '\' (the usual test for a zero byte, on the word XORed with them); the order of the bytes doesn't matter
    const std::uint64_t quotes = word ^ (Ones * '"');
    const std::uint64_t backslashes = word ^ (Ones * '\\');
    const std::uint64_t special = word | ((word - Ones * 0x20) & ~word) | ((quotes - Ones) & ~quotes) |
        ((backslashes - Ones) & ~backslashes);

    return (special & HighBits) == 0;
}

/*
  MeasureUTF8Sequence

  The length of the UTF-8 sequence of a character above U+007F that starts at 'index', if it is valid (see the Unicode
  standard's table of well-formed byte sequences); otherwise 0, and 'invalidLength' gets the length of its longest start
  that could begin a valid sequence, at least 1, which U+FFFD replaces
*/
std::size_t MeasureUTF8Sequence(std::string_view text, std::size_t index, std::size_t& invalidLength) noexcept
{
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t length = 0;
    unsigned char low = 0x80; // The range of the second byte, which some lead bytes narrow
    unsigned char high = 0xBF;

    if (lead >= 0xC2 && lead <= 0xDF)
    {
        length = 2;
    }
    else if (lead >= 0xE0 && lead <= 0xEF)
    {
        length = 3;
        low = lead == 0xE0 ? 0xA0 : 0x80; // No overlong forms
        high = lead == 0xED ? 0x9F : 0xBF; // No surrogates
    }
    else if (lead >= 0xF0 && lead <= 0xF4)
    {
        length = 4;
        low = lead == 0xF0 ? 0x90 : 0x80; // No overlong forms
        high = lead == 0xF4 ? 0x8F : 0xBF; // Nothing above U+10FFFF
    }
    else
    {
        invalidLength = 1;
        return 0;
    }

    std::size_t count = 1;

    while (count < length && index + count < text.size())
    {
        const auto byte = static_cast<unsigned char>(text[index + count]);
        if (byte < low || byte > high)
            break;

        low = 0x80;
        high = 0xBF;
        ++count;
    }

    if (count == length)
        return length;

    invalidLength = count;

    return 0;
}

} // namespace

//---------------------------------------------------------------------------
void ApplyMultilineMode(std::string& line, MultilineMode mode, LineEnding ending)
{
    std::size_t index = 0;
    const auto first = NextMultilinePiece(line, index, mode, ending);

    if (!first.IsLineBreak && index >= line.size())
        return;

    std::string rewritten;
    rewritten.reserve(line.size() + 32);
    rewritten.append(first.Text);

    while (index < line.size())
        rewritten.append(NextMultilinePiece(line, index, mode, ending).Text);

    line = std::move(rewritten);
}

//---------------------------------------------------------------------------
TJSONPiece NextJSONPiece(std::string_view text, std::size_t& index, char (& escape)[6]) noexcept
{
    const std::size_t start = index;
    std::size_t invalidLength = 0;

    while (index < text.size())
    {
        if (text.size() - index >= 8 && IsPlainJSONWord(text.data() + index))
        {
            index += 8;
            continue;
        }

        const auto character = static_cast<unsigned char>(text[index]);
        if (IsPlainJSONCharacter(character))
        {
            ++index;
            continue;
        }

        if (character < 0x80)
            break;

        const auto length = MeasureUTF8Sequence(text, index, invalidLength);
        if (length == 0)
            break;

        index += length;
    }

    if (index > start)
        return TJSONPiece{ text.substr(start, index - start), false };

    if (index >= text.size())
        return TJSONPiece{};

    if (invalidLength > 0)
    {
        index += invalidLength;
        return TJSONPiece{ ReplacementCharacter, true };
    }

    const char character = text[index++];
    escape[0] = '\\';

    switch (character)
    {
        case '"':
        case '\\':
            escape[1] = character;
            break;

        case '\b':
            escape[1] = 'b';
            break;

        case '\f':
            escape[1] = 'f';
            break;

        case '\n':
            escape[1] = 'n';
            break;

        case '\r':
            escape[1] = 'r';
            break;

        case '\t':
            escape[1] = 't';
            break;

        default:
        {
            constexpr std::string_view HexDigits = "0123456789abcdef";
            const auto code = static_cast<unsigned char>(character);
            escape[1] = 'u';
            escape[2] = '0';
            escape[3] = '0';
            escape[4] = HexDigits[code >> 4];
            escape[5] = HexDigits[code & 0xF];
            return TJSONPiece{ std::string_view(escape, 6), true };
        }
    }

    return TJSONPiece{ std::string_view(escape, 2), true };
}

//---------------------------------------------------------------------------
TMultilinePiece NextMultilinePiece(std::string_view text, std::size_t& index, MultilineMode mode,
    LineEnding ending) noexcept
{
    const std::size_t start = index;

    if (mode == MultilineMode::Preserve)
    {
        index = text.size();
        return TMultilinePiece{ text.substr(start), false };
    }

    // Indent leaves a lone CR as it is
    const auto isLineBreakAt = [text, mode](std::size_t position) {
            return text[position] == '\n' || (text[position] == '\r' && (mode == MultilineMode::Escape ||
                (position + 1 < text.size() && text[position + 1] == '\n')));
        };

    while (index < text.size())
    {
        if (text.size() - index >= 8 && !HasCROrLF(text.data() + index))
            index += 8;
        else if (isLineBreakAt(index))
            break;
        else
            ++index;
    }

    if (index > start)
        return TMultilinePiece{ text.substr(start, index - start), false };

    if (index >= text.size())
        return TMultilinePiece{};

    if (mode == MultilineMode::Escape)
        return TMultilinePiece{ text[index++] == '\r' ? EscapedCR : EscapedLF, true };

    index += text[index] == '\r' ? 2 : 1;
    const std::string_view indented = ending == LineEnding::CRLF ? IndentedCRLF : IndentedLF;
    const bool isNextLineEmpty = index >= text.size() || isLineBreakAt(index);

    return TMultilinePiece{ isNextLineEmpty ? indented.substr(0, indented.size() - 1) : indented, true };
}

} // namespace Detail

//---------------------------------------------------------------------------

namespace JSON
{

//---------------------------------------------------------------------------
void AppendString(std::string& output, std::string_view text)
{
    char escape[6];
    std::size_t index = 0;

    output += '"';

    while (index < text.size())
        output.append(Detail::NextJSONPiece(text, index, escape).Text);

    output += '"';
}

} // namespace JSON

//---------------------------------------------------------------------------

namespace Time
{

namespace
{

// The calendar fields WriteISO8601() writes
struct TDateAndTime
{
    int Year = 0;
    std::uint32_t Month = 0;
    std::uint32_t Day = 0;
    std::uint32_t Hour = 0;
    std::uint32_t Minute = 0;
    std::uint32_t Second = 0;
    std::uint32_t Nanosecond = 0;
};

//---------------------------------------------------------------------------

int GetOffsetMinutes(const std::tm& localTime, std::chrono::sys_seconds time) noexcept;
bool ToLocalCalendarTime(std::time_t timeValue, std::tm& localTime) noexcept;
bool ToLocalDateAndTime(std::chrono::system_clock::time_point timePoint, TDateAndTime& dateAndTime, int& offsetMinutes) noexcept;
TDateAndTime ToUTCDateAndTime(std::chrono::system_clock::time_point timePoint) noexcept;
char* WriteDateAndTime(char* out, char* end, const TDateAndTime& dateAndTime) noexcept;
char* WriteDigits(char* out, std::uint32_t value, int digitCount) noexcept;
char* WriteFraction(char* out, std::uint32_t nanoseconds, TimePrecision precision) noexcept;

//---------------------------------------------------------------------------

/*
  GetOffsetMinutes

  How many minutes 'localTime', the local calendar fields of 'time', is ahead of UTC: the local date and time read as
  if they were UTC, minus the actual time. Not mktime(), which would read UTC fields as local time and apply this
  zone's daylight saving rules to them.
*/
int GetOffsetMinutes(const std::tm& localTime, std::chrono::sys_seconds time) noexcept
{
    const std::chrono::sys_days localDate = std::chrono::year(localTime.tm_year + 1900) / (localTime.tm_mon + 1) / localTime.tm_mday;
    const auto localAsUTC = localDate + std::chrono::hours(localTime.tm_hour) + std::chrono::minutes(localTime.tm_min) +
        std::chrono::seconds(localTime.tm_sec);

    return static_cast<int>(std::chrono::duration_cast<std::chrono::minutes>(localAsUTC - time).count());
}

/*
  ToLocalCalendarTime

  Converts 'timeValue' to the local time zone's calendar fields. Returns false if it can't (e.g. a time before 1970 on
  Windows).
*/
bool ToLocalCalendarTime(std::time_t timeValue, std::tm& localTime) noexcept
{
#if defined(_WIN32)
    return localtime_s(&localTime, &timeValue) == 0;
#else
    return localtime_r(&timeValue, &localTime) != nullptr;
#endif
}

/*
  ToLocalDateAndTime

  The calendar fields of 'timePoint' in the local time zone, and the zone's offset from UTC then. Returns false if the
  local time can't be determined (see ToLocalCalendarTime()).
*/
bool ToLocalDateAndTime(std::chrono::system_clock::time_point timePoint, TDateAndTime& dateAndTime, int& offsetMinutes) noexcept
{
    // Floored, so a time before 1970 falls on the second it is in
    const auto seconds = std::chrono::floor<std::chrono::seconds>(timePoint);
    std::tm localTime{};

    if (!ToLocalCalendarTime(std::chrono::system_clock::to_time_t(seconds), localTime))
        return false;

    dateAndTime.Year = localTime.tm_year + 1900;
    dateAndTime.Month = static_cast<std::uint32_t>(localTime.tm_mon + 1);
    dateAndTime.Day = static_cast<std::uint32_t>(localTime.tm_mday);
    dateAndTime.Hour = static_cast<std::uint32_t>(localTime.tm_hour);
    dateAndTime.Minute = static_cast<std::uint32_t>(localTime.tm_min);
    dateAndTime.Second = static_cast<std::uint32_t>(localTime.tm_sec);
    dateAndTime.Nanosecond = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(timePoint - seconds).count());
    offsetMinutes = GetOffsetMinutes(localTime, seconds);

    return true;
}

/*
  ToUTCDateAndTime

  The calendar fields of 'timePoint' in UTC. Calendar arithmetic only.
*/
TDateAndTime ToUTCDateAndTime(std::chrono::system_clock::time_point timePoint) noexcept
{
    // Floored, so a time before 1970 falls on the day it is in; the time of day is then never negative
    const auto day = std::chrono::floor<std::chrono::days>(timePoint);
    const std::chrono::year_month_day date{ day };
    const auto timeOfDay = timePoint - day;
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeOfDay);
    const auto secondOfDay = static_cast<std::uint32_t>(seconds.count());

    TDateAndTime dateAndTime;
    dateAndTime.Year = static_cast<int>(date.year());
    dateAndTime.Month = static_cast<unsigned>(date.month());
    dateAndTime.Day = static_cast<unsigned>(date.day());
    dateAndTime.Hour = secondOfDay / 3600;
    dateAndTime.Minute = secondOfDay / 60 % 60;
    dateAndTime.Second = secondOfDay % 60;
    dateAndTime.Nanosecond = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(timeOfDay - seconds).count());

    return dateAndTime;
}

/*
  WriteDateAndTime

  Writes "YYYY-MM-DDTHH:mm:ss" (a 4-digit year for the years 0 to 9999, the year in full outside them) and returns the
  end of what it wrote
*/
char* WriteDateAndTime(char* out, char* end, const TDateAndTime& dateAndTime) noexcept
{
    if (dateAndTime.Year >= 0 && dateAndTime.Year <= 9999)
        out = WriteDigits(out, static_cast<std::uint32_t>(dateAndTime.Year), 4);
    else
        out = std::to_chars(out, end, dateAndTime.Year).ptr;

    *out++ = '-';
    out = WriteDigits(out, dateAndTime.Month, 2);
    *out++ = '-';
    out = WriteDigits(out, dateAndTime.Day, 2);
    *out++ = 'T';
    out = WriteDigits(out, dateAndTime.Hour, 2);
    *out++ = ':';
    out = WriteDigits(out, dateAndTime.Minute, 2);
    *out++ = ':';
    return WriteDigits(out, dateAndTime.Second, 2);
}

/*
  WriteDigits

  Writes the lowest 'digitCount' decimal digits of 'value', with leading zeros, and returns the end of what it wrote
*/
char* WriteDigits(char* out, std::uint32_t value, int digitCount) noexcept
{
    for (int index = digitCount - 1; index >= 0; --index)
    {
        out[index] = static_cast<char>('0' + value % 10);
        value /= 10;
    }

    return out + digitCount;
}

/*
  WriteFraction

  Writes '.' and the fraction of the second, 'nanoseconds' (0 to 999999999), with the digits of 'precision', cut off
  rather than rounded, and returns the end of what it wrote
*/
char* WriteFraction(char* out, std::uint32_t nanoseconds, TimePrecision precision) noexcept
{
    *out++ = '.';

    switch (precision)
    {
        case TimePrecision::Microseconds:
            return WriteDigits(out, nanoseconds / 1000, 6);

        case TimePrecision::Nanoseconds:
            return WriteDigits(out, nanoseconds, 9);

        case TimePrecision::Milliseconds:
            break;
    }

    return WriteDigits(out, nanoseconds / 1000000, 3);
}

} // namespace

//---------------------------------------------------------------------------
int GetUTCOffsetMinutes(std::chrono::system_clock::time_point timePoint)
{
    const auto seconds = std::chrono::floor<std::chrono::seconds>(timePoint);
    std::tm localTime{};

    if (!ToLocalCalendarTime(std::chrono::system_clock::to_time_t(seconds), localTime))
        return 0;

    return GetOffsetMinutes(localTime, seconds);
}

//---------------------------------------------------------------------------
std::string ToISO8601String(std::chrono::system_clock::time_point timePoint, TimeZone zone, TimePrecision precision)
{
    char buffer[ISO8601BufferSize];
    return std::string(buffer, WriteISO8601(buffer, timePoint, zone, precision));
}

//---------------------------------------------------------------------------
std::string ToDateString(std::chrono::system_clock::time_point timePoint, TimeZone zone)
{
    char buffer[ISO8601BufferSize];
    const std::string_view text(buffer, WriteISO8601(buffer, timePoint, zone));
    return std::string(text.substr(0, text.find('T')));
}

//---------------------------------------------------------------------------
std::string ToLocalISO8601String(std::chrono::system_clock::time_point timePoint)
{
    return ToISO8601String(timePoint, TimeZone::Local);
}

//---------------------------------------------------------------------------
std::size_t WriteISO8601(char (& buffer)[ISO8601BufferSize], std::chrono::system_clock::time_point timePoint, TimeZone zone,
    TimePrecision precision) noexcept
{
    // Each helper is called once, so the compiler can inline the UTC path into one function
    TDateAndTime dateAndTime;
    int offsetMinutes = 0;
    const bool isLocal = zone == TimeZone::Local && ToLocalDateAndTime(timePoint, dateAndTime, offsetMinutes);

    if (!isLocal)
        dateAndTime = ToUTCDateAndTime(timePoint);

    char* out = WriteDateAndTime(buffer, buffer + ISO8601BufferSize, dateAndTime);
    out = WriteFraction(out, dateAndTime.Nanosecond, precision);

    if (isLocal)
    {
        const auto absoluteOffsetMinutes = static_cast<std::uint32_t>(offsetMinutes < 0 ? -offsetMinutes : offsetMinutes);
        *out++ = offsetMinutes < 0 ? '-' : '+';
        out = WriteDigits(out, absoluteOffsetMinutes / 60, 2);
        *out++ = ':';
        out = WriteDigits(out, absoluteOffsetMinutes % 60, 2);
    }
    else
    {
        *out++ = 'Z';
    }

    return static_cast<std::size_t>(out - buffer);
}

} // namespace Time

//---------------------------------------------------------------------------

} // namespace ASWLog
