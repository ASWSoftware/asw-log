/* **************************************************************************
Test_ASWLog_Fields.cpp
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
#include "Test_ASWLog_Fields.h"
//---------------------------------------------------------------------------
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_Base.h"
#include "ASWLog_CategoryLog.h"
#include "ASWLog_Fields.h"
#include "ASWLog_FileLog.h"
#include "ASWLog_MultiLog.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

const auto GroupBaseTempDir = std::filesystem::temp_directory_path() / "aswlog_fields_tests";
const auto TestTempDir = GroupBaseTempDir / "test";

enum class TColor
{
    Red,
};

enum TPlainColor
{
    PlainRed,
};

//---------------------------------------------------------------------------

std::string DescribeField(const ASWLog::TASWLogField& field);
std::string DescribeFields(const ASWLog::TASWLogRecord& record);
std::string DescribeFieldList(const ASWLog::TASWLogScope* scope, std::span<const ASWLog::TASWLogField> fields);

//---------------------------------------------------------------------------

/*
  DescribeField

  "<key>=<value>", the value written plainly by the test (not by the library's layouts), with its kind:
  "id=i:17", "n=u:4", "x=d:9.5", "ok=b:true", "user=t:amy"
*/
std::string DescribeField(const ASWLog::TASWLogField& field)
{
    const auto& value = field.Value;
    switch (value.GetKind())
    {
        case ASWLog::ValueKind::Int:
            return std::format("{}=i:{}", field.Key, value.GetInt());

        case ASWLog::ValueKind::UInt:
            return std::format("{}=u:{}", field.Key, value.GetUInt());

        case ASWLog::ValueKind::Double:
            return std::format("{}=d:{}", field.Key, value.GetDouble());

        case ASWLog::ValueKind::Bool:
            return std::format("{}=b:{}", field.Key, value.GetBool());

        case ASWLog::ValueKind::Text:
            return std::format("{}=t:{}", field.Key, value.GetText());
    }

    return "?";
}

/*
  DescribeFieldList

  The fields ForEachLogField() gives, each as DescribeField() writes it, separated by spaces
*/
std::string DescribeFieldList(const ASWLog::TASWLogScope* scope, std::span<const ASWLog::TASWLogField> fields)
{
    std::string text;
    ASWLog::ForEachLogField(scope, fields, [&text](const ASWLog::TASWLogField& field) {
                if (!text.empty())
                    text += ' ';

                text += DescribeField(field);
            });

    return text;
}

/*
  DescribeFields

  An entry's fields, as DescribeFieldList() writes them
*/
std::string DescribeFields(const ASWLog::TASWLogRecord& record)
{
    return DescribeFieldList(record.Scope, record.GetOwnFields());
}

//---------------------------------------------------------------------------

// Writes "<message>|<fields>" for each entry it is given (see DescribeFields()), copied while the call lasts
class TFieldsLogger final : public ASWLog::TASWLogBase
{
public:
    std::vector<std::string> Written;

protected:
    std::string_view GetLoggerClassName() const noexcept override
    {
        return "TFieldsLogger";
    }

    void WriteRecord(const ASWLog::TASWLogRecord& record) override
    {
        Written.push_back(std::format("{}|{}", record.Message, DescribeFields(record)));
    }

public:
    bool Initialize(const ASWLog::TASWLogConfig& config) noexcept override
    {
        [[maybe_unused]] const auto previousConfig = SetConfig(config);
        SetMinimumLevel(config.InitialMinimumLevel);
        m_IsInitialized.store(true, std::memory_order_release);
        return true;
    }

    bool Reconfigure(const ASWLog::TASWLogConfig& config) noexcept override
    {
        [[maybe_unused]] const auto previousConfig = SetConfig(config);
        return true;
    }

    bool Open() noexcept override
    {
        return true;
    }

    bool Close() noexcept override
    {
        return true;
    }

    bool Flush() noexcept override
    {
        return true;
    }

    bool IsOpen() const noexcept override
    {
        return true;
    }
};

} // namespace

//---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_Fields
///////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWLog_Fields::TTest_ASWLog_Fields()
    : inherited("ASWLog_Fields_Tests")
{
    RegisterTest(&TTest_ASWLog_Fields::Test_Async_OnLogEntryGetsTheFieldsAfterTheScopeEnds, "Async_OnLogEntryGetsTheFieldsAfterTheScopeEnds");
    RegisterTest(&TTest_ASWLog_Fields::Test_Backtrace_KeepsTheFieldsAfterTheScopeEnds, "Backtrace_KeepsTheFieldsAfterTheScopeEnds");
    RegisterTest(&TTest_ASWLog_Fields::Test_Capture_TakesTheScopesToAnotherThread, "Capture_TakesTheScopesToAnotherThread");
    RegisterTest(&TTest_ASWLog_Fields::Test_ForEachLogField_EachKeyOnceInnermostWins, "ForEachLogField_EachKeyOnceInnermostWins");
    RegisterTest(&TTest_ASWLog_Fields::Test_ForEachLogField_RepeatedKeysInTheEntrysOwnFields, "ForEachLogField_RepeatedKeysInTheEntrysOwnFields");
    RegisterTest(&TTest_ASWLog_Fields::Test_Log_CategoryAndMultiLogPassTheFieldsOn, "Log_CategoryAndMultiLogPassTheFieldsOn");
    RegisterTest(&TTest_ASWLog_Fields::Test_Log_EachOverloadPassesItsFields, "Log_EachOverloadPassesItsFields");
    RegisterTest(&TTest_ASWLog_Fields::Test_Log_ScopeFieldsComeBeforeTheEntrysOwn, "Log_ScopeFieldsComeBeforeTheEntrysOwn");
    RegisterTest(&TTest_ASWLog_Fields::Test_Scope_CopiesTemporaryTexts, "Scope_CopiesTemporaryTexts");
    RegisterTest(&TTest_ASWLog_Fields::Test_Scope_IsPerThread, "Scope_IsPerThread");
    RegisterTest(&TTest_ASWLog_Fields::Test_Scope_NestsAndRestoresTheCurrentScope, "Scope_NestsAndRestoresTheCurrentScope");
    RegisterTest(&TTest_ASWLog_Fields::Test_Value_KindsFromEachType, "Value_KindsFromEachType");
    RegisterTest(&TTest_ASWLog_Fields::Test_Value_RejectsCharactersEnumsAndPointers, "Value_RejectsCharactersEnumsAndPointers");
}
//---------------------------------------------------------------------------
TTest_ASWLog_Fields::~TTest_ASWLog_Fields()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::SetUp_Group()
{
    std::filesystem::create_directories(GroupBaseTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::SetUp_Test(ITestCase& /*testCase*/)
{
    std::filesystem::create_directories(TestTempDir);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::TearDown_Group()
{
    std::error_code errorCode;
    std::filesystem::remove_all(GroupBaseTempDir, errorCode);
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::TearDown_Test(ITestCase& /*testCase*/)
{
    std::error_code errorCode;
    std::filesystem::remove_all(TestTempDir, errorCode);
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::Test_Async_OnLogEntryGetsTheFieldsAfterTheScopeEnds()
{
    // Arrange: an asynchronous file logger whose OnLogEntry, on the logger's thread, describes each entry's fields
    std::mutex mutex;
    std::condition_variable called;
    std::vector<std::string> described;

    ASWLog::TASWLogConfig config;
    config.File.FolderPath = TestTempDir;
    config.File.FilePath = "async.log";
    config.File.OpenRetryCount = 1;
    config.Async.Enabled = true;
    config.Startup.WriteTimeInfo = false;
    config.Startup.WriteOSInfo = false;
    config.Startup.WriteDriveInfo = false;
    config.Startup.WriteSystemMemoryInfo = false;
    config.Startup.WriteApplicationInfo = false;
    config.Startup.WriteMemoryUsage = false;
    config.Shutdown.WriteLine = false;
    config.OnLogEntryMinimumLevel = ASWLog::Level::Trace;
    config.OnLogEntry = [&](const ASWLog::TASWLogRecord& record, std::string_view) {
            const std::lock_guard<std::mutex> lock(mutex);
            described.push_back(DescribeFields(record));
            called.notify_all();
        };

    ASWLog::TASWFileLog logger;
    CheckTrue(logger.Initialize(config), "Initialize should succeed");

    // Act: the scope and the call's texts are gone before the logger's thread calls OnLogEntry
    {
        const ASWLog::TASWLogScope scope{ { "requestId", std::string("r-1") } };
        logger.LogInfo("queued", { { "user", std::string("amy") }, { "attempt", 2 } });
    }

    // The logger's thread calls OnLogEntry after it has written the entry, so after Flush() could return
    std::unique_lock<std::mutex> lock(mutex);
    const bool wasCalled = called.wait_for(lock, std::chrono::seconds(5), [&described] {
            return !described.empty();
        });

    // Assert
    AssertTrue(wasCalled, "OnLogEntry should be called");
    AssertEquals(std::size_t(1), described.size(), "OnLogEntry should be called once");
    CheckEquals(std::string("requestId=t:r-1 user=t:amy attempt=i:2"), described[0],
        "OnLogEntry should get copies of the entry's fields and its scope's, made when the entry was queued");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::Test_Backtrace_KeepsTheFieldsAfterTheScopeEnds()
{
    // Arrange
    ASWLog::TASWLogConfig config;
    config.InitialMinimumLevel = ASWLog::Level::Info;
    config.Backtrace.Capacity = 4;
    config.Backtrace.LowestLevel = ASWLog::Level::Debug;

    TFieldsLogger logger;
    logger.Initialize(config);

    // Act: the kept entry's scope and texts are gone when the backtrace is written
    {
        const ASWLog::TASWLogScope scope{ { "requestId", std::string("r-7") } };
        logger.LogDebug("kept", { { "step", std::string("parse") } });
    }

    logger.LogError("failed", { { "code", 5 } });

    // Assert
    AssertEquals(std::size_t(4), logger.Written.size(), "The backtrace's markers, its entry and the error should be written");
    CheckEquals(std::string("kept|requestId=t:r-7 step=t:parse"), logger.Written[1], "The kept entry should keep copies of its fields");
    CheckEquals(std::string("failed|code=i:5"), logger.Written[3], "The error should have its own fields only");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::Test_Capture_TakesTheScopesToAnotherThread()
{
    // Arrange
    ASWLog::TASWLogContext context;
    ASWLog::TASWLogContext emptyContext = ASWLog::TASWLogScope::Capture();
    {
        const ASWLog::TASWLogScope outer{ { "requestId", "r-1" }, { "user", "amy" } };
        const ASWLog::TASWLogScope inner{ { "user", "bob" } };
        const auto captured = ASWLog::TASWLogScope::Capture();
        context = captured; // A copy, which must refer to its own texts
    }

    std::string onOtherThread;
    std::string afterScopeOnOtherThread;

    // Act
    std::thread worker([&] {
        {
            const ASWLog::TASWLogScope scope(context, { { "task", 3 }, { "user", "carl" } });
            onOtherThread = DescribeFieldList(ASWLog::TASWLogScope::GetCurrent(), {});
        }

        afterScopeOnOtherThread = DescribeFieldList(ASWLog::TASWLogScope::GetCurrent(), {});
            });
    worker.join();

    const ASWLog::TASWLogContext copied(context);
    ASWLog::TASWLogContext assigned;
    assigned = context;

    // Assert
    AssertEquals(std::size_t(2), context.GetFields().size(), "The context should hold two fields");

    for (const ASWLog::TASWLogContext* copy : { &copied, static_cast<const ASWLog::TASWLogContext*>(&assigned) })
    {
        AssertEquals(std::size_t(2), copy->GetFields().size(), "A copy should hold the same fields");
        CheckNotSame(context.GetFields()[0].Key.data(), copy->GetFields()[0].Key.data(), "A copy should have its own keys");
        CheckNotSame(context.GetFields()[1].Value.GetText().data(), copy->GetFields()[1].Value.GetText().data(),
            "A copy should have its own texts");
        CheckEquals(std::string("requestId=t:r-1 user=t:bob"), DescribeFieldList(nullptr, copy->GetFields()), "A copy should read the same");
    }

    CheckTrue(emptyContext.IsEmpty(), "Capture() without a scope should give an empty context");
    CheckEquals(std::string("requestId=t:r-1 user=t:bob"), DescribeFieldList(nullptr, context.GetFields()),
        "The context should hold each key once, the inner scope's value winning");
    CheckEquals(std::string("requestId=t:r-1 user=t:carl task=i:3"), onOtherThread,
        "A scope made from the context should apply it on the other thread, its own fields replacing the same keys");
    CheckEmpty(afterScopeOnOtherThread, "The other thread should have no scope once it ends");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::Test_ForEachLogField_EachKeyOnceInnermostWins()
{
    // Arrange: user and req in an outer scope, req again in an inner one, then the entry's own user and x (twice)
    const ASWLog::TASWLogScope outer{ { "user", "amy" }, { "req", 1 } };
    const ASWLog::TASWLogScope inner{ { "req", 2 }, { "depth", 1 } };
    const ASWLog::TASWLogField own[] = { { "user", "bob" }, { "x", 1 }, { "x", 2 } };

    // Act
    const auto described = DescribeFieldList(ASWLog::TASWLogScope::GetCurrent(), own);
    const auto scopesOnly = DescribeFieldList(ASWLog::TASWLogScope::GetCurrent(), {});

    // Assert
    CheckEquals(std::string("user=t:bob req=i:2 depth=i:1 x=i:2"), described,
        "Each key should come once, where it first appears, with its innermost (or the entry's own) value");
    CheckEquals(std::string("user=t:amy req=i:2 depth=i:1"), scopesOnly, "Without the entry's fields, the inner scope should win");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::Test_ForEachLogField_RepeatedKeysInTheEntrysOwnFields()
{
    // Arrange
    const ASWLog::TASWLogField own[] = { { "a", 1 }, { "b", 2 }, { "a", 3 }, { "c", 4 }, { "b", 5 } };

    // Act
    const auto described = DescribeFieldList(nullptr, own);
    const auto none = DescribeFieldList(nullptr, {});

    // Assert
    CheckEquals(std::string("a=i:3 b=i:5 c=i:4"), described, "A repeated key should come once, where it first appears, with its last value");
    CheckEmpty(none, "No fields should give nothing");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::Test_Log_CategoryAndMultiLogPassTheFieldsOn()
{
    // Arrange: a category over a multi-log over two loggers
    TFieldsLogger first;
    TFieldsLogger second;

    ASWLog::TASWMultiLog multiLog;
    multiLog.AddLogger(first);
    multiLog.AddLogger(second);

    ASWLog::TASWCategoryLog netLog("Net", multiLog);

    // Act
    {
        const ASWLog::TASWLogScope scope{ { "requestId", "r-9" } };
        netLog.LogInfo("from net", { { "bytes", 512u } });
    }

    // Assert
    const std::vector<std::string> expected{ "from net|requestId=t:r-9 bytes=u:512" };
    CheckTrue(first.Written == expected, "The first logger should get the entry's fields and its scope's");
    CheckTrue(second.Written == expected, "The second logger should get them too");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::Test_Log_EachOverloadPassesItsFields()
{
    // Arrange
    ASWLog::TASWLogConfig config;
    config.InitialMinimumLevel = ASWLog::Level::Trace;

    TFieldsLogger logger;
    logger.Initialize(config);

    const int value = 7;

    // Act
    logger.Log(ASWLog::Level::Info, "log", { { "n", 1 } });
    logger.LogRaw(ASWLog::Level::Info, "raw", { { "n", 2 } });
    logger.LogForce(ASWLog::Level::Info, "force", { { "n", 3 } });
    logger.LogForceRaw(ASWLog::Level::Info, "forceraw", { { "n", 4 } });
    logger.LogTrace("trace", { { "n", 5 } });
    logger.LogDebug("debug", { { "n", 6 } });
    logger.LogInfo("info", { { "n", 7 } });
    logger.LogWarn("warn", { { "n", 8 } });
    logger.LogError("error", { { "n", 9 } });
    logger.LogCritical("critical", { { "n", 10 } });
    logger.LogFmt(ASWLog::Level::Info, { { "n", 11 } }, "logfmt {}", value);
    logger.LogRawFmt(ASWLog::Level::Info, { { "n", 12 } }, "rawfmt {}", value);
    logger.LogForceFmt(ASWLog::Level::Info, { { "n", 13 } }, "forcefmt {}", value);
    logger.LogForceRawFmt(ASWLog::Level::Info, { { "n", 14 } }, "forcerawfmt {}", value);
    logger.LogTraceFmt({ { "n", 15 } }, "tracefmt {}", value);
    logger.LogDebugFmt({ { "n", 16 } }, "debugfmt {}", value);
    logger.LogInfoFmt({ { "n", 17 } }, "infofmt {}", value);
    logger.LogWarnFmt({ { "n", 18 } }, "warnfmt {}", value);
    logger.LogErrorFmt({ { "n", 19 } }, "errorfmt {}", value);
    logger.LogCriticalFmt({ { "n", 20 } }, "criticalfmt {}", value);
    logger.LogInfo("none");

    // Assert
    const std::vector<std::string> expected{
        "log|n=i:1", "raw|n=i:2", "force|n=i:3", "forceraw|n=i:4", "trace|n=i:5", "debug|n=i:6", "info|n=i:7", "warn|n=i:8",
        "error|n=i:9", "critical|n=i:10", "logfmt 7|n=i:11", "rawfmt 7|n=i:12", "forcefmt 7|n=i:13",
        "forcerawfmt 7|n=i:14", "tracefmt 7|n=i:15", "debugfmt 7|n=i:16", "infofmt 7|n=i:17", "warnfmt 7|n=i:18",
        "errorfmt 7|n=i:19", "criticalfmt 7|n=i:20", "none|"
    };
    AssertEquals(expected.size(), logger.Written.size(), "Each call should write one entry");

    for (std::size_t index = 0; index < expected.size(); ++index)
        CheckEquals(expected[index], logger.Written[index], "Each overload should pass its message and fields");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::Test_Log_ScopeFieldsComeBeforeTheEntrysOwn()
{
    // Arrange
    TFieldsLogger logger;
    logger.Initialize(ASWLog::TASWLogConfig());

    // Act
    logger.LogInfo("before");
    {
        const ASWLog::TASWLogScope scope{ { "requestId", "r-2" }, { "user", "amy" } };
        logger.LogInfo("inside");
        logger.LogInfo("own", { { "user", "bob" }, { "total", 9.5 } });
    }
    logger.LogInfo("after");

    // Assert
    const std::vector<std::string> expected{
        "before|", "inside|requestId=t:r-2 user=t:amy", "own|requestId=t:r-2 user=t:bob total=d:9.5", "after|"
    };
    CheckTrue(logger.Written == expected, "Entries in the scope should carry its fields, the entry's own after them and winning");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::Test_Scope_CopiesTemporaryTexts()
{
    // Arrange & Act: keys and texts that are gone once the scope is made
    const ASWLog::TASWLogScope scope{ { std::string("requestId"), std::to_string(12345) },
        { std::string_view(std::string(40, 'k')), std::string(40, 'x') } };
    const std::vector<std::string> filler(16, std::string(40, 'z')); // Likely to reuse the freed memory

    // Assert
    CheckEquals("requestId=t:12345 " + std::string(40, 'k') + "=t:" + std::string(40, 'x'),
        DescribeFieldList(ASWLog::TASWLogScope::GetCurrent(), {}), "The scope should keep copies of its keys and texts");
    CheckEquals(std::size_t(16), filler.size(), "(keeps the filler alive)");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::Test_Scope_IsPerThread()
{
    // Arrange
    const ASWLog::TASWLogScope scope{ { "requestId", "r-3" } };
    const ASWLog::TASWLogScope* onOtherThread = &scope;

    // Act
    std::thread worker([&onOtherThread] {
        onOtherThread = ASWLog::TASWLogScope::GetCurrent();
            });
    worker.join();

    // Assert
    CheckSame(&scope, ASWLog::TASWLogScope::GetCurrent(), "The scope should be this thread's current one");
    CheckNull(onOtherThread, "Another thread should have no scope");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::Test_Scope_NestsAndRestoresTheCurrentScope()
{
    // Arrange
    const ASWLog::TASWLogScope* beforeAll = ASWLog::TASWLogScope::GetCurrent();
    const ASWLog::TASWLogScope* insideOuter = nullptr;
    const ASWLog::TASWLogScope* insideInner = nullptr;
    const ASWLog::TASWLogScope* innerParent = nullptr;
    const ASWLog::TASWLogScope* afterInner = nullptr;
    const ASWLog::TASWLogScope* outerAddress = nullptr;

    // Act
    {
        const ASWLog::TASWLogScope outer{ { "a", 1 } };
        outerAddress = &outer;
        insideOuter = ASWLog::TASWLogScope::GetCurrent();
        {
            const ASWLog::TASWLogScope inner{ { "b", 2 } };
            insideInner = ASWLog::TASWLogScope::GetCurrent();
            innerParent = inner.GetParent();
        }

        afterInner = ASWLog::TASWLogScope::GetCurrent();
    }

    const ASWLog::TASWLogScope* afterAll = ASWLog::TASWLogScope::GetCurrent();

    // Assert
    CheckNull(beforeAll, "There should be no scope at first");
    CheckSame(outerAddress, insideOuter, "The outer scope should be current inside it");
    CheckNotNull(insideInner, "The inner scope should be current inside it");
    CheckSame(outerAddress, innerParent, "The inner scope should be in the outer one");
    CheckSame(outerAddress, afterInner, "The outer scope should be current again after the inner one ends");
    CheckNull(afterAll, "No scope should be current after both end");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::Test_Value_KindsFromEachType()
{
    // Arrange
    const std::string text = "from std::string";
    const std::uint64_t largest = std::numeric_limits<std::uint64_t>::max();

    // Act
    const ASWLog::TASWLogField fields[] = {
        { "int", -17 }, { "long long", std::numeric_limits<long long>::min() }, { "signed char", static_cast<signed char>(-3) },
        { "unsigned", 4u }, { "size_t", std::size_t(4096) }, { "uint64", largest },
        { "unsigned char", static_cast<unsigned char>(200) }, { "double", 9.99 }, { "float", 0.5f }, { "long double", 2.5L },
        { "bool", true }, { "literal", "text" }, { "string", text }, { "view", std::string_view("view") },
        { "default", ASWLog::TASWLogValue() }
    };

    // Assert
    std::string described;
    for (const auto& field : fields)
        described += DescribeField(field) + ';';

    CheckEquals(std::string("int=i:-17;long long=i:-9223372036854775808;signed char=i:-3;unsigned=u:4;size_t=u:4096;") +
        "uint64=u:18446744073709551615;unsigned char=u:200;double=d:9.99;float=d:0.5;long double=d:2.5;bool=b:true;" +
        "literal=t:text;string=t:from std::string;view=t:view;default=i:0;", described,
        "Each type should become the value of its kind, signed and unsigned integers kept apart");
    CheckSame(text.data(), fields[12].Value.GetText().data(), "Text should be a view of the caller's characters, not a copy");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Fields::Test_Value_RejectsCharactersEnumsAndPointers()
{
    // Act & Assert: these would otherwise become a number or a bool without the caller noticing
    CheckFalse(std::is_constructible_v<ASWLog::TASWLogValue, char>, "A char should be rejected");
    CheckFalse(std::is_constructible_v<ASWLog::TASWLogValue, wchar_t>, "A wchar_t should be rejected");
    CheckFalse(std::is_constructible_v<ASWLog::TASWLogValue, char8_t>, "A char8_t should be rejected");
    CheckFalse(std::is_constructible_v<ASWLog::TASWLogValue, TColor>, "A scoped enum should be rejected");
    CheckFalse(std::is_constructible_v<ASWLog::TASWLogValue, TPlainColor>, "An unscoped enum should be rejected");
    CheckFalse(std::is_constructible_v<ASWLog::TASWLogValue, const int*>, "A pointer that isn't text should be rejected");
    CheckFalse(std::is_constructible_v<ASWLog::TASWLogValue, std::nullptr_t>, "nullptr should be rejected");
    CheckTrue(std::is_constructible_v<ASWLog::TASWLogValue, const char*>, "A C string should be accepted");
    CheckEquals(std::string(""), std::string(ASWLog::TASWLogValue(static_cast<const char*>(nullptr)).GetText()),
        "A null C string should be empty text");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_Fields)
