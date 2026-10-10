/* **************************************************************************
Test_ASWLog_CategoryLog.cpp
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
#include "Test_ASWLog_CategoryLog.h"
//---------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_CategoryLog.h"
#include "ASWLog_FileLog.h"
#include "ASWLog_MultiLog.h"
#include "ASWLog_NullLog.h"
//---------------------------------------------------------------------------

namespace
{

// A value whose formatting is counted in CategoryFormatCount, to check whether a *Fmt call through a category formats
struct TCategoryCountedValue
{
};

int CategoryFormatCount = 0;

} // namespace

template<>
struct std::formatter<TCategoryCountedValue>
{
    constexpr std::format_parse_context::iterator parse(std::format_parse_context& context)
    {
        return context.begin();
    }

    std::format_context::iterator format(const TCategoryCountedValue& /*value*/, std::format_context& context) const
    {
        ++CategoryFormatCount;
        return std::format_to(context.out(), "counted");
    }
};

namespace ASWUnitTests
{

namespace
{

const auto GroupBaseTempDir = std::filesystem::temp_directory_path() / "aswlog_categorylog_tests";
const auto TestTempDir = GroupBaseTempDir / "test";

// Longer than any standard library's small string buffer, so a copy of it is on the heap, and a view of a destroyed
// one points to freed memory (which AddressSanitizer reports)
const std::string LongCategoryName = "A_category_name_longer_than_a_small_string_buffer";

constexpr std::chrono::milliseconds WaitTimeout = std::chrono::seconds(5);

// A logger that records "<LEVEL>|<category>|<message>" for each entry it writes ("|forced" added if forced), with the
// CategoryLevel and source line each came with, and counts the calls a category must not pass on. Like any
// TASWLogBase logger it applies its own minimum level (Info by default).
class TRecordingLogger final : public ASWLog::TASWLogBase
{
protected:
    std::string_view GetLoggerClassName() const noexcept override
    {
        return "TRecordingLogger";
    }

    void WriteRecord(const ASWLog::TASWLogRecord& record) override
    {
        Written.push_back(std::format("{}|{}|{}{}", ASWLog::Level_ToString(record.LogLevel), record.Category, record.Message,
            record.Forced ? "|forced" : ""));
        CategoryLevels.push_back(record.CategoryLevel);
        SourceLines.push_back(record.Location.line());
    }

public:
    std::vector<std::string> Written;
    std::vector<std::optional<ASWLog::Level> > CategoryLevels;
    std::vector<std::uint_least32_t> SourceLines;
    int LifecycleCallCount = 0; // Initialize(), Reconfigure(), Open() and Close() calls
    int FlushCount = 0;

    bool Initialize(const ASWLog::TASWLogConfig& config) noexcept override
    {
        ++LifecycleCallCount;
        try
        {
            [[maybe_unused]] const auto previousConfig = SetConfig(config);
            SetMinimumLevel(config.InitialMinimumLevel);
            m_IsInitialized.store(true, std::memory_order_release);

            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool Reconfigure(const ASWLog::TASWLogConfig& /*config*/) noexcept override
    {
        ++LifecycleCallCount;
        return true;
    }

    bool Open() noexcept override
    {
        ++LifecycleCallCount;
        return true;
    }

    bool Close() noexcept override
    {
        ++LifecycleCallCount;
        return true;
    }

    bool Flush() noexcept override
    {
        ++FlushCount;
        return true;
    }

    bool IsOpen() const noexcept override
    {
        return true;
    }
};

// A config with a backtrace of 'capacity' entries from Trace up, written at Error, and a minimum level of Info
ASWLog::TASWLogConfig MakeBacktraceConfig(std::size_t capacity)
{
    ASWLog::TASWLogConfig config;
    config.Backtrace.Capacity = capacity;
    config.Backtrace.LowestLevel = ASWLog::Level::Trace;
    config.Backtrace.DumpAtLevel = ASWLog::Level::Error;
    return config;
}

// A file config for 'file' in TestTempDir whose lines show only the level (and the category), minimum level Info,
// without the startup and shutdown lines
ASWLog::TASWLogConfig MakeFileConfig(const std::filesystem::path& file)
{
    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = file;
    config.File.OpenRetryCount = 1;
    config.Line.ShowTimestamp = false;
    config.Line.ShowProcessId = false;
    config.Line.ShowThreadId = false;
    config.Shutdown.WriteLine = false;
    config.Startup.WriteTimeInfo = false;
    config.Startup.WriteOSInfo = false;
    config.Startup.WriteDriveInfo = false;
    config.Startup.WriteSystemMemoryInfo = false;
    config.Startup.WriteApplicationInfo = false;
    config.Startup.WriteMemoryUsage = false;

    return config;
}

std::string ReadFileText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};

    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

// Clears the category levels a test applied (they are process-wide) when it ends
struct TCategoryLevelsReset
{
    TCategoryLevelsReset() = default;
    TCategoryLevelsReset(const TCategoryLevelsReset&) = delete;
    TCategoryLevelsReset& operator=(const TCategoryLevelsReset&) = delete;

    ~TCategoryLevelsReset()
    {
        ASWLog::TASWCategoryLog::ApplyLevels("");
    }
};

// Checks 'condition' until it is true, for at most WaitTimeout. Returns false if it never was.
bool WaitUntil(const std::function<bool()>& condition)
{
    const auto deadline = std::chrono::steady_clock::now() + WaitTimeout;
    while (!condition())
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    return true;
}

} // namespace

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_CategoryLog
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_CategoryLog::TTest_ASWLog_CategoryLog()
    : inherited("ASWLog_CategoryLog_Tests")
{
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_ApplyLevels_AppliesToCategoriesMadeLater, "ApplyLevels_AppliesToCategoriesMadeLater");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_ApplyLevels_GivesTheLongestCoveringNamesLevel, "ApplyLevels_GivesTheLongestCoveringNamesLevel");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_ApplyLevels_IsSafeWhileCategoriesComeAndGo, "ApplyLevels_IsSafeWhileCategoriesComeAndGo");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_ApplyLevels_LeavesUncoveredCategories, "ApplyLevels_LeavesUncoveredCategories");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_ApplyLevels_RejectsAnInvalidSpec, "ApplyLevels_RejectsAnInvalidSpec");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_Async_KeepsItsOwnCopyOfTheName, "Async_KeepsItsOwnCopyOfTheName");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_Backtrace_KeepsEntriesBelowTheCategoryLevel, "Backtrace_KeepsEntriesBelowTheCategoryLevel");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_Backtrace_KeepsItsOwnCopyOfTheName, "Backtrace_KeepsItsOwnCopyOfTheName");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_FileLog_WritesTheCategoryAfterTheLevel, "FileLog_WritesTheCategoryAfterTheLevel");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_GetMinimumLevel_FollowsTheWrappedLoggerUntilSet, "GetMinimumLevel_FollowsTheWrappedLoggerUntilSet");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_Lifecycle_LeavesTheWrappedLoggerAlone, "Lifecycle_LeavesTheWrappedLoggerAlone");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_MultiLog_CategoryLevelReplacesOnlyTheCompositeLevel, "MultiLog_CategoryLevelReplacesOnlyTheCompositeLevel");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_Nesting_JoinsNamesAndInheritsLevels, "Nesting_JoinsNamesAndInheritsLevels");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_SetEnabled_SilencesOnlyTheCategory, "SetEnabled_SilencesOnlyTheCategory");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_SetMinimumLevel_OffSilencesAllButForcedEntries, "SetMinimumLevel_OffSilencesAllButForcedEntries");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_SetMinimumLevel_ReplacesTheWrappedLoggersLevel, "SetMinimumLevel_ReplacesTheWrappedLoggersLevel");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_ShouldLog_MatchesWhatIsWritten, "ShouldLog_MatchesWhatIsWritten");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_Write_KeepsACategoryAlreadySet, "Write_KeepsACategoryAlreadySet");
    RegisterTest(&TTest_ASWLog_CategoryLog::Test_Write_StampsTheNameAndOwnLevel, "Write_StampsTheNameAndOwnLevel");
}
//---------------------------------------------------------------------------
TTest_ASWLog_CategoryLog::~TTest_ASWLog_CategoryLog()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::SetUp_Group()
{
    Log("Setting up temp group folder: " + GroupBaseTempDir.string());
    std::filesystem::create_directories(GroupBaseTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::SetUp_Test(ITestCase& testCase)
{
    Log("  Setting up temp folder for " + testCase.GetName() + ": " + TestTempDir.string());
    std::filesystem::create_directories(TestTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::TearDown_Group()
{
    Log("Cleaning up temp group folder:" + GroupBaseTempDir.string());
    std::filesystem::remove_all(GroupBaseTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::TearDown_Test(ITestCase& testCase)
{
    Log("  Cleaning up temp folder for " + testCase.GetName() + ": " + TestTempDir.string());
    std::filesystem::remove_all(TestTempDir);
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_ApplyLevels_AppliesToCategoriesMadeLater()
{
    // Arrange
    TCategoryLevelsReset resetLevels;
    TRecordingLogger logger;

    // Act
    const bool isValid = ASWLog::TASWCategoryLog::ApplyLevels("Later=Debug");
    ASWLog::TASWCategoryLog laterLog("Later", logger);
    ASWLog::TASWCategoryLog httpLog("Http", laterLog);
    ASWLog::TASWCategoryLog otherLog("Other", logger);

    // Assert
    CheckTrue(isValid, "The spec should be valid");
    CheckEquals(ASWLog::Level::Debug, laterLog.GetMinimumLevel(), "A category made after ApplyLevels() should get its level");
    CheckTrue(laterLog.HasOwnMinimumLevel(), "The spec's level should be the category's own");
    CheckTrue(httpLog.HasOwnMinimumLevel(), "A category under a covered one should get the level too");
    CheckEquals(ASWLog::Level::Debug, httpLog.GetMinimumLevel(), "Later.Http should get Later's level");
    CheckFalse(otherLog.HasOwnMinimumLevel(), "A category the spec doesn't cover should get no level");

    // Act: an empty spec covers no category made later
    ASWLog::TASWCategoryLog::ApplyLevels("");
    ASWLog::TASWCategoryLog afterClearLog("Later", logger);

    // Assert
    CheckFalse(afterClearLog.HasOwnMinimumLevel(), "After an empty spec, a new category should get no level");
    CheckEquals(ASWLog::Level::Debug, laterLog.GetMinimumLevel(), "An empty spec should leave the levels already set");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_ApplyLevels_GivesTheLongestCoveringNamesLevel()
{
    // Arrange
    TCategoryLevelsReset resetLevels;
    TRecordingLogger logger;
    ASWLog::TASWCategoryLog dbLog("Db", logger);
    ASWLog::TASWCategoryLog netLog("Net", logger);
    ASWLog::TASWCategoryLog dnsLog("Dns", netLog);
    ASWLog::TASWCategoryLog httpLog("Http", netLog);
    ASWLog::TASWCategoryLog proxyLog("Proxy", httpLog);
    ASWLog::TASWCategoryLog networkLog("Network", logger);

    // Act: broader names first, any case, spaces, an empty item, and Net given twice (the last wins)
    const bool isValid = ASWLog::TASWCategoryLog::ApplyLevels(" * = Warn , net=Debug, NET.HTTP = trace,, Net=Info, ");

    // Assert
    CheckTrue(isValid, "The spec should be valid");
    CheckEquals(ASWLog::Level::Warn, dbLog.GetMinimumLevel(), "* should cover Db");
    CheckEquals(ASWLog::Level::Info, netLog.GetMinimumLevel(), "Net should get its last level");
    CheckEquals(ASWLog::Level::Info, dnsLog.GetMinimumLevel(), "Net should cover Net.Dns");
    CheckEquals(ASWLog::Level::Trace, httpLog.GetMinimumLevel(), "Net.Http should win over Net for Net.Http");
    CheckEquals(ASWLog::Level::Trace, proxyLog.GetMinimumLevel(), "Net.Http should cover Net.Http.Proxy");
    CheckEquals(ASWLog::Level::Warn, networkLog.GetMinimumLevel(), "Net should not cover Network, only * should");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_ApplyLevels_IsSafeWhileCategoriesComeAndGo()
{
    // Arrange
    constexpr int ThreadCount = 4;
    constexpr int CategoriesPerThread = 2000;

    TCategoryLevelsReset resetLevels;
    ASWLog::TASWNullLog logger; // Thread-safe
    std::atomic<bool> isDone{ false };
    std::atomic<int> withoutLevelCount{ 0 };
    ASWLog::TASWCategoryLog::ApplyLevels("Churn=Warn");

    // Act: threads make, use and destroy categories while the levels change
    std::vector<std::thread> threads;
    for (int thread = 0; thread < ThreadCount; ++thread)
    {
        threads.emplace_back([&logger, &withoutLevelCount]
            {
                for (int index = 0; index < CategoriesPerThread; ++index)
                {
                    ASWLog::TASWCategoryLog churnLog("Churn", logger);
                    const auto level = churnLog.GetMinimumLevel();
                    if (!churnLog.HasOwnMinimumLevel() || (level != ASWLog::Level::Warn && level != ASWLog::Level::Debug))
                        ++withoutLevelCount;

                    churnLog.LogDebug("churn");
                }
            });
    }

    const auto applyLevels = [&isDone]
        {
            for (int round = 0; !isDone.load(); ++round)
                ASWLog::TASWCategoryLog::ApplyLevels(round % 2 == 0 ? "Churn=Warn" : "Churn=Debug");
        };
    std::thread applier(applyLevels);

    for (auto& thread : threads)
        thread.join();

    isDone.store(true);
    applier.join();

    // Assert
    CheckEquals(0, withoutLevelCount.load(), "Every category made after the first spec should get a level");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_ApplyLevels_LeavesUncoveredCategories()
{
    // Arrange
    TCategoryLevelsReset resetLevels;
    TRecordingLogger logger;
    ASWLog::TASWCategoryLog coveredLog("Covered", logger);
    ASWLog::TASWCategoryLog freeLog("Free", logger);
    ASWLog::TASWCategoryLog codedLog("Coded", logger);
    codedLog.SetMinimumLevel(ASWLog::Level::Error);

    // Act
    ASWLog::TASWCategoryLog::ApplyLevels("Covered=Debug");
    logger.SetMinimumLevel(ASWLog::Level::Warn);

    // Assert
    CheckEquals(ASWLog::Level::Debug, coveredLog.GetMinimumLevel(), "The covered category should get the spec's level");
    CheckFalse(freeLog.HasOwnMinimumLevel(), "An uncovered category should get no level");
    CheckEquals(ASWLog::Level::Warn, freeLog.GetMinimumLevel(), "An uncovered category should still follow the wrapped logger");
    CheckEquals(ASWLog::Level::Error, codedLog.GetMinimumLevel(), "An uncovered category should keep the level the code set");

    // Act: a spec that no longer covers it leaves its level; the code can still change it
    ASWLog::TASWCategoryLog::ApplyLevels("Other=Info");
    const auto levelAfterOtherSpec = coveredLog.GetMinimumLevel();
    coveredLog.SetMinimumLevel(ASWLog::Level::Critical);
    const auto levelAfterSet = coveredLog.GetMinimumLevel();
    coveredLog.ResetMinimumLevel();

    // Assert
    CheckEquals(ASWLog::Level::Debug, levelAfterOtherSpec, "A spec that doesn't cover the category should leave its level");
    CheckEquals(ASWLog::Level::Critical, levelAfterSet, "SetMinimumLevel() after ApplyLevels() should win");
    CheckFalse(coveredLog.HasOwnMinimumLevel(), "ResetMinimumLevel() should make the category follow again");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_ApplyLevels_RejectsAnInvalidSpec()
{
    // Arrange
    struct TInvalidSpec
    {
        std::string_view Spec;
        std::string_view Error;
    };

    const TInvalidSpec invalidSpecs[]{
        { "Spec=Trace,Net", "'Net' has no '=' (expected name=level)" },
        { "Spec=Trace, =Info", "'=Info' has no category name" },
        { "Spec=Trace,Net.*=Info", "'Net.*=Info': a name can't hold '*' (a name covers the categories under it; \"*\" covers all)" },
        { "Spec=Trace,Net=Loud", "'Net=Loud': 'Loud' isn't a level (Trace, Debug, Info, Warn, Error, Critical or Off)" },
        { "Spec=Trace,Net=", "'Net=': '' isn't a level (Trace, Debug, Info, Warn, Error, Critical or Off)" },
    };

    TCategoryLevelsReset resetLevels;
    TRecordingLogger logger;
    ASWLog::TASWCategoryLog specLog("Spec", logger);
    ASWLog::TASWCategoryLog::ApplyLevels("Spec=Debug");

    for (const auto& invalidSpec : invalidSpecs)
    {
        // Act
        std::string error = "previous";
        const bool isValid = ASWLog::TASWCategoryLog::ApplyLevels(invalidSpec.Spec, error);
        const bool isValidWithoutError = ASWLog::TASWCategoryLog::ApplyLevels(invalidSpec.Spec);
        ASWLog::TASWCategoryLog laterLog("Spec", logger);

        // Assert
        const auto spec = std::string(invalidSpec.Spec);
        CheckFalse(isValid, "The spec should be invalid: " + spec);
        CheckFalse(isValidWithoutError, "The spec should be invalid without an error text too: " + spec);
        CheckEquals(std::string(invalidSpec.Error), error, "The error should describe the bad item: " + spec);
        CheckEquals(ASWLog::Level::Debug, specLog.GetMinimumLevel(), "An invalid spec should change no level: " + spec);
        CheckEquals(ASWLog::Level::Debug, laterLog.GetMinimumLevel(), "An invalid spec should keep the levels for new categories: " + spec);
    }

    // Act: a valid spec clears the error
    std::string error = "previous";
    const bool isValid = ASWLog::TASWCategoryLog::ApplyLevels("Spec=Info", error);

    // Assert
    CheckTrue(isValid, "The spec should be valid");
    CheckEmpty(error, "A valid spec should leave the error empty");
    CheckEquals(ASWLog::Level::Info, specLog.GetMinimumLevel(), "A valid spec should set the level");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_Async_KeepsItsOwnCopyOfTheName()
{
    // Arrange: an asynchronous file logger calls OnLogEntry on its own thread, after the call that logged the entry
    std::mutex callbackMutex;
    std::vector<std::string> callbackCategories;
    auto config = MakeFileConfig("async.log");
    config.Async.Enabled = true;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Trace;
    config.OnLogEntry = [&](const ASWLog::TASWLogRecord& record, std::string_view /*line*/) {
            std::lock_guard<std::mutex> lock(callbackMutex);
            callbackCategories.emplace_back(record.Category);
        };

    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);

    // Act: the category is gone before the logger's thread gets to the entry
    {
        ASWLog::TASWCategoryLog category(LongCategoryName, logger);
        category.LogInfo("queued");
    }

    const bool wasCalledBack = WaitUntil([&] {
            std::lock_guard<std::mutex> lock(callbackMutex);
            return !callbackCategories.empty();
        });

    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    AssertTrue(wasCalledBack, "OnLogEntry should be called for the entry");
    CheckEquals(LongCategoryName, callbackCategories[0], "OnLogEntry should get the category's name, kept with the queued entry");
    CheckEquals("[INFO][" + LongCategoryName + "]: queued\n", ReadFileText(TestTempDir / "async.log"),
        "The line should show the category");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_Backtrace_KeepsEntriesBelowTheCategoryLevel()
{
    // Arrange: the logger keeps a backtrace from Trace; Db is raised to Warn, Net lowered to Debug
    TRecordingLogger logger;
    logger.Initialize(MakeBacktraceConfig(5));
    ASWLog::TASWCategoryLog dbLog("Db", logger);
    ASWLog::TASWCategoryLog netLog("Net", logger);
    dbLog.SetMinimumLevel(ASWLog::Level::Warn);
    netLog.SetMinimumLevel(ASWLog::Level::Debug);

    // Act
    dbLog.LogInfo("db_info"); // Below Db's level, so kept
    netLog.LogTrace("net_trace"); // Below Net's level, so kept
    netLog.LogDebug("net_debug"); // Written
    const auto writtenBeforeError = logger.Written;
    dbLog.LogError("db_error");

    // Assert
    const std::vector<std::string> expectedBeforeError{ "DEBUG|Net|net_debug" };
    CheckTrue(writtenBeforeError == expectedBeforeError, "Only the entry at its category's level should be written at first");
    const std::vector<std::string> expected{
        "DEBUG|Net|net_debug",
        "INFO||Backtrace: the last 2 entries below the minimum level|forced",
        "INFO|Db|db_info|forced",
        "TRACE|Net|net_trace|forced",
        "INFO||Backtrace end|forced",
        "ERROR|Db|db_error"
    };
    CheckTrue(logger.Written == expected, "The entries below their category's level should be in the backtrace, with their category");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_Backtrace_KeepsItsOwnCopyOfTheName()
{
    // Arrange: a backtrace of one entry, so the second kept entry replaces the first
    TRecordingLogger logger;
    logger.Initialize(MakeBacktraceConfig(1));
    ASWLog::TASWCategoryLog netLog("Net", logger);
    netLog.LogDebug("replaced");

    // Act: the category is gone before the backtrace is written
    {
        ASWLog::TASWCategoryLog category(LongCategoryName, logger);
        category.LogDebug("kept");
    }

    logger.DumpBacktrace();

    // Assert
    AssertEquals(3, logger.Written.size(), "The backtrace should be written between its markers");
    CheckEquals("DEBUG|" + LongCategoryName + "|kept|forced", logger.Written[1],
        "The kept entry should keep its category's name after the category is gone");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_FileLog_WritesTheCategoryAfterTheLevel()
{
    // Arrange: the file logger at Info; Net lowered to Debug
    const auto logFile = TestTempDir / "category.log";
    auto config = MakeFileConfig("category.log");
    ASWLog::TASWFileLog logger;
    const bool initialized = logger.Initialize(config);
    ASWLog::TASWCategoryLog netLog("Net", logger);
    netLog.SetMinimumLevel(ASWLog::Level::Debug);

    // Act
    netLog.LogDebug("net_debug");
    logger.LogDebug("plain_debug");
    logger.LogInfo("plain_info");

    config.Line.ShowCategory = false;
    const bool reconfigured = logger.Reconfigure(config);
    netLog.LogInfo("hidden");
    logger.Close();

    // Assert
    CheckTrue(initialized, "Initialize should succeed");
    CheckTrue(reconfigured, "Reconfigure should succeed");
    CheckEquals(std::string("[DEBUG][Net]: net_debug\n[INFO]: plain_info\n[INFO]: hidden\n"), ReadFileText(logFile),
        "The category should follow the level, its own level should let its Debug entry in, and ShowCategory should hide it");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_GetMinimumLevel_FollowsTheWrappedLoggerUntilSet()
{
    // Arrange
    TRecordingLogger logger;
    ASWLog::TASWCategoryLog netLog("Net", logger);

    // Act
    const auto initialLevel = netLog.GetMinimumLevel();
    const bool initiallyHasOwnLevel = netLog.HasOwnMinimumLevel();
    logger.SetMinimumLevel(ASWLog::Level::Warn);
    const auto followedLevel = netLog.GetMinimumLevel();

    netLog.SetMinimumLevel(ASWLog::Level::Debug);
    const auto ownLevel = netLog.GetMinimumLevel();
    const bool hasOwnLevel = netLog.HasOwnMinimumLevel();
    const auto loggerLevel = logger.GetMinimumLevel();

    netLog.ResetMinimumLevel();
    const auto levelAfterReset = netLog.GetMinimumLevel();
    const bool hasOwnLevelAfterReset = netLog.HasOwnMinimumLevel();

    // Assert
    CheckEquals(ASWLog::Level::Info, initialLevel, "A new category should have the wrapped logger's level");
    CheckFalse(initiallyHasOwnLevel, "A new category should have no level of its own");
    CheckEquals(ASWLog::Level::Warn, followedLevel, "The category should follow a change of the wrapped logger's level");
    CheckEquals(ASWLog::Level::Debug, ownLevel, "SetMinimumLevel should give the category its own level");
    CheckTrue(hasOwnLevel, "HasOwnMinimumLevel should be true after SetMinimumLevel");
    CheckEquals(ASWLog::Level::Warn, loggerLevel, "The category's level should not change the wrapped logger's");
    CheckEquals(ASWLog::Level::Warn, levelAfterReset, "ResetMinimumLevel should make the category follow the wrapped logger again");
    CheckFalse(hasOwnLevelAfterReset, "HasOwnMinimumLevel should be false after ResetMinimumLevel");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_Lifecycle_LeavesTheWrappedLoggerAlone()
{
    // Arrange: a logger with a backtrace, so DumpBacktrace() shows whether it was passed on
    TRecordingLogger logger;
    logger.Initialize(MakeBacktraceConfig(5));
    const int lifecycleCallsBefore = logger.LifecycleCallCount;
    ASWLog::TASWCategoryLog netLog("Net", logger);
    const ASWLog::TASWLogConfig config;

    // Act
    const bool initialized = netLog.Initialize(config);
    const bool reconfigured = netLog.Reconfigure(config);
    const bool opened = netLog.Open();
    const bool closed = netLog.Close();
    const bool flushed = netLog.Flush();
    const bool isOpen = netLog.IsOpen();
    const auto loggerConfig = logger.GetConfig();
    const auto categoryConfig = netLog.GetConfig();

    netLog.LogDebug("kept");
    netLog.SetEnabled(false);
    netLog.DumpBacktrace(); // Disabled: not passed on
    const auto writtenWhileDisabled = logger.Written.size();
    netLog.SetEnabled(true);
    netLog.DumpBacktrace();

    // Assert
    CheckFalse(initialized, "Initialize should do nothing and return false");
    CheckFalse(reconfigured, "Reconfigure should do nothing and return false");
    CheckFalse(opened, "Open should do nothing and return false");
    CheckFalse(closed, "Close should do nothing and return false");
    CheckEquals(lifecycleCallsBefore, logger.LifecycleCallCount, "None of them should reach the wrapped logger");
    CheckTrue(flushed, "Flush should return the wrapped logger's result");
    CheckEquals(1, logger.FlushCount, "Flush should be passed on");
    CheckTrue(isOpen, "IsOpen should return the wrapped logger's result");
    CheckSame(loggerConfig, categoryConfig, "GetConfig should return the wrapped logger's config");
    CheckEquals(0, writtenWhileDisabled, "A disabled category should not pass DumpBacktrace on");
    CheckEquals(3, logger.Written.size(), "DumpBacktrace should be passed on: the kept entry between the markers");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_MultiLog_CategoryLevelReplacesOnlyTheCompositeLevel()
{
    // Arrange: a multi-log at Info with one sink at Trace and one at Info; Net lowered to Debug
    TRecordingLogger traceSink;
    TRecordingLogger infoSink;
    traceSink.SetMinimumLevel(ASWLog::Level::Trace);
    ASWLog::TASWMultiLog multiLog;
    multiLog.SetMinimumLevel(ASWLog::Level::Info);
    multiLog.AddLogger(traceSink);
    multiLog.AddLogger(infoSink);
    ASWLog::TASWCategoryLog netLog("Net", multiLog);
    netLog.SetMinimumLevel(ASWLog::Level::Debug);

    // Act
    netLog.LogDebug("net_debug");
    netLog.LogTrace("net_trace");
    multiLog.LogDebug("plain_debug");
    const bool shouldLogDebug = netLog.ShouldLog(ASWLog::Level::Debug);
    const bool shouldLogTrace = netLog.ShouldLog(ASWLog::Level::Trace);

    multiLog.RemoveLogger(traceSink);
    const bool shouldLogDebugWithInfoSinkOnly = netLog.ShouldLog(ASWLog::Level::Debug);

    // Assert
    const std::vector<std::string> expected{ "DEBUG|Net|net_debug" };
    CheckTrue(traceSink.Written == expected, "Net's level should replace the composite's, so its Debug entry reaches the sinks");
    CheckEmpty(infoSink.Written, "A sink should still apply its own minimum level");
    AssertEquals(1, traceSink.CategoryLevels.size(), "The Trace sink should get one entry");
    CheckFalse(traceSink.CategoryLevels[0].has_value(), "The sinks should get the record without the category's level");
    CheckTrue(shouldLogDebug, "ShouldLog should be true for Net's Debug entries");
    CheckFalse(shouldLogTrace, "ShouldLog should be false below Net's level");
    CheckFalse(shouldLogDebugWithInfoSinkOnly, "ShouldLog should be false when no sink takes the entry");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_Nesting_JoinsNamesAndInheritsLevels()
{
    // Arrange
    TRecordingLogger logger;
    ASWLog::TASWCategoryLog netLog("Net", logger);
    ASWLog::TASWCategoryLog httpLog("Http", netLog);

    // Act
    httpLog.LogDebug("below_the_loggers_level");
    netLog.SetMinimumLevel(ASWLog::Level::Debug);
    httpLog.LogDebug("at_nets_level");
    const auto levelFollowingNet = httpLog.GetMinimumLevel();

    httpLog.SetMinimumLevel(ASWLog::Level::Warn);
    httpLog.LogInfo("below_https_level");
    netLog.LogInfo("net_info");

    netLog.SetEnabled(false);
    httpLog.LogError("net_disabled");
    const bool shouldLogWhileNetDisabled = httpLog.ShouldLog(ASWLog::Level::Error);

    // Assert
    CheckEquals(std::string("Net.Http"), std::string(httpLog.GetName()), "A nested category's name should follow its parent's");
    const std::vector<std::string> expected{ "DEBUG|Net.Http|at_nets_level", "INFO|Net|net_info" };
    CheckTrue(logger.Written == expected, "A nested category should use its own level if set, else its parent's, else the logger's");
    CheckEquals(ASWLog::Level::Debug, levelFollowingNet, "A nested category without a level of its own should follow its parent's");
    CheckFalse(shouldLogWhileNetDisabled, "Disabling a category should silence the categories wrapping it");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_SetEnabled_SilencesOnlyTheCategory()
{
    // Arrange
    TRecordingLogger logger;
    ASWLog::TASWCategoryLog netLog("Net", logger);

    // Act
    netLog.SetEnabled(false);
    netLog.LogError("disabled_category");
    netLog.LogForce(ASWLog::Level::Error, "disabled_category_forced");
    logger.LogInfo("plain");
    const bool isEnabled = netLog.IsEnabled();
    const bool shouldLogWhileDisabled = netLog.ShouldLog(ASWLog::Level::Error);

    netLog.SetEnabled(true);
    logger.SetEnabled(false);
    netLog.LogError("disabled_logger");
    const bool shouldLogWhileLoggerDisabled = netLog.ShouldLog(ASWLog::Level::Error);

    // Assert
    const std::vector<std::string> expected{ "INFO||plain" };
    CheckTrue(logger.Written == expected, "A disabled category should pass nothing on, not even forced entries");
    CheckFalse(isEnabled, "IsEnabled should be false after SetEnabled(false)");
    CheckFalse(shouldLogWhileDisabled, "ShouldLog should be false while the category is disabled");
    CheckFalse(shouldLogWhileLoggerDisabled, "ShouldLog should be false while the wrapped logger is disabled");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_SetMinimumLevel_OffSilencesAllButForcedEntries()
{
    // Arrange
    TRecordingLogger logger;
    ASWLog::TASWCategoryLog netLog("Net", logger);
    netLog.SetMinimumLevel(ASWLog::Level::Off);

    // Act
    netLog.LogCritical("critical");
    netLog.LogForce(ASWLog::Level::Warn, "forced");
    logger.LogInfo("plain");

    // Assert
    const std::vector<std::string> expected{ "WARN|Net|forced|forced", "INFO||plain" };
    CheckTrue(logger.Written == expected, "A category at Off should write only forced entries, and leave other entries alone");
    CheckEquals(ASWLog::Level::Info, logger.GetMinimumLevel(), "The wrapped logger's level should not change");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_SetMinimumLevel_ReplacesTheWrappedLoggersLevel()
{
    // Arrange: the logger at Info; Net lowered to Debug, Db raised to Warn, UI following the logger
    TRecordingLogger logger;
    ASWLog::TASWCategoryLog netLog("Net", logger);
    ASWLog::TASWCategoryLog dbLog("Db", logger);
    ASWLog::TASWCategoryLog uiLog("UI", logger);
    netLog.SetMinimumLevel(ASWLog::Level::Debug);
    dbLog.SetMinimumLevel(ASWLog::Level::Warn);

    // Act
    netLog.LogTrace("net_trace");
    netLog.LogDebug("net_debug");
    dbLog.LogInfo("db_info");
    dbLog.LogWarn("db_warn");
    uiLog.LogDebug("ui_debug");
    uiLog.LogInfo("ui_info");
    logger.LogDebug("plain_debug");

    // Assert
    const std::vector<std::string> expected{ "DEBUG|Net|net_debug", "WARN|Db|db_warn", "INFO|UI|ui_info" };
    CheckTrue(logger.Written == expected, "Each category's own level should replace the logger's, both ways, for its entries only");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_ShouldLog_MatchesWhatIsWritten()
{
    // Arrange: one logger without a backtrace, one keeping a backtrace from Debug, both at Info
    TRecordingLogger plainLogger;
    TRecordingLogger backtraceLogger;
    auto backtraceConfig = MakeBacktraceConfig(5);
    backtraceConfig.Backtrace.LowestLevel = ASWLog::Level::Debug;
    backtraceLogger.Initialize(backtraceConfig);

    ASWLog::TASWCategoryLog dbLog("Db", plainLogger);
    ASWLog::TASWCategoryLog netLog("Net", plainLogger);
    ASWLog::TASWCategoryLog keptDbLog("Db", backtraceLogger);
    dbLog.SetMinimumLevel(ASWLog::Level::Warn);
    netLog.SetMinimumLevel(ASWLog::Level::Debug);
    keptDbLog.SetMinimumLevel(ASWLog::Level::Warn);
    CategoryFormatCount = 0;

    // Act
    const bool dbInfo = dbLog.ShouldLog(ASWLog::Level::Info);
    const bool dbWarn = dbLog.ShouldLog(ASWLog::Level::Warn);
    const bool netDebug = netLog.ShouldLog(ASWLog::Level::Debug);
    const bool netTrace = netLog.ShouldLog(ASWLog::Level::Trace);
    const bool keptDbDebug = keptDbLog.ShouldLog(ASWLog::Level::Debug);
    const bool keptDbTrace = keptDbLog.ShouldLog(ASWLog::Level::Trace);

    dbLog.LogInfoFmt("db {}", TCategoryCountedValue{});
    const int formatCountBelowLevel = CategoryFormatCount;
    netLog.LogDebugFmt("net {}", TCategoryCountedValue{});
    const int formatCountAtLevel = CategoryFormatCount;

    // Assert
    CheckFalse(dbInfo, "ShouldLog should be false below a raised category level");
    CheckTrue(dbWarn, "ShouldLog should be true at a raised category level");
    CheckTrue(netDebug, "ShouldLog should be true at a lowered category level");
    CheckFalse(netTrace, "ShouldLog should be false below a lowered category level");
    CheckTrue(keptDbDebug, "ShouldLog should be true for an entry the backtrace keeps");
    CheckFalse(keptDbTrace, "ShouldLog should be false below the backtrace's lowest level");
    CheckEquals(0, formatCountBelowLevel, "A *Fmt call below the category's level should not format");
    CheckEquals(1, formatCountAtLevel, "A *Fmt call at the category's level should format once");
    const std::vector<std::string> expected{ "DEBUG|Net|net counted" };
    CheckTrue(plainLogger.Written == expected, "Only the entry at Net's level should be written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_Write_KeepsACategoryAlreadySet()
{
    // Arrange: a record that already has a category and a level, as from a category wrapping this one
    TRecordingLogger logger;
    ASWLog::TASWCategoryLog netLog("Net", logger);
    netLog.SetMinimumLevel(ASWLog::Level::Warn);
    ASWLog::TASWLogRecord record;
    record.LogLevel = ASWLog::Level::Info;
    record.Message = "from_elsewhere";
    record.Category = "Other";
    record.CategoryLevel = ASWLog::Level::Info;

    // Act
    netLog.Write(record);

    // Assert
    const std::vector<std::string> expected{ "INFO|Other|from_elsewhere" };
    CheckTrue(logger.Written == expected, "The record's own category and level should be kept");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_CategoryLog::Test_Write_StampsTheNameAndOwnLevel()
{
    // Arrange
    TRecordingLogger logger;
    ASWLog::TASWCategoryLog netLog("Net", logger);
    const auto location = std::source_location::current();

    // Act
    netLog.LogInfo("without_own_level");
    netLog.SetMinimumLevel(ASWLog::Level::Debug);
    netLog.Log(ASWLog::Level::Info, "with_own_level", location);

    // Assert
    CheckEquals(std::string("Net"), std::string(netLog.GetName()), "GetName should return the name");
    const std::vector<std::string> expected{ "INFO|Net|without_own_level", "INFO|Net|with_own_level" };
    CheckTrue(logger.Written == expected, "Each entry should carry the category's name");
    AssertEquals(2, logger.CategoryLevels.size(), "Both entries should be written");
    CheckFalse(logger.CategoryLevels[0].has_value(), "Without a level of its own, the category should pass none");
    CheckTrue(logger.CategoryLevels[1] == ASWLog::Level::Debug, "With a level of its own, the category should pass it");
    CheckEquals(location.line(), logger.SourceLines[1], "The caller's location should be kept");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_CategoryLog)
