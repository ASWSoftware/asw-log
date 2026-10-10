/* **************************************************************************
Test_ASWLog_Types.cpp
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
#include "Test_ASWLog_Types.h"
//---------------------------------------------------------------------------
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_Types.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

// "key=value" for each of the entry's fields, in the order ForEachField() gives them (text values as they are)
std::vector<std::string> DescribeFields(const ASWLog::TASWPendingEntry& entry)
{
    std::vector<std::string> fields;
    entry.ForEachField([&fields](const ASWLog::TASWLogField& field) {
                if (field.Value.GetKind() == ASWLog::ValueKind::Text)
                    fields.push_back(std::format("{}={}", field.Key, field.Value.GetText()));
                else
                    fields.push_back(std::format("{}={}", field.Key, field.Value.GetInt()));
            });

    return fields;
}

} // namespace

//---------------------------------------------------------------------------
TTest_ASWLog_Types::TTest_ASWLog_Types()
    : inherited("ASWLog_Types_Tests")
{
    RegisterTest(&TTest_ASWLog_Types::Test_AsyncOverflowPolicy_FromString, "AsyncOverflowPolicy_FromString");
    RegisterTest(&TTest_ASWLog_Types::Test_AsyncOverflowPolicy_ToString, "AsyncOverflowPolicy_ToString");
    RegisterTest(&TTest_ASWLog_Types::Test_ColorMode_FromString, "ColorMode_FromString");
    RegisterTest(&TTest_ASWLog_Types::Test_ColorMode_ToString, "ColorMode_ToString");
    RegisterTest(&TTest_ASWLog_Types::Test_ErrorKind_ToString, "ErrorKind_ToString");
    RegisterTest(&TTest_ASWLog_Types::Test_FlushMode_FromString, "FlushMode_FromString");
    RegisterTest(&TTest_ASWLog_Types::Test_FlushMode_ToString, "FlushMode_ToString");
    RegisterTest(&TTest_ASWLog_Types::Test_Level_FromString, "Level_FromString");
    RegisterTest(&TTest_ASWLog_Types::Test_Level_ToString, "Level_ToString");
    RegisterTest(&TTest_ASWLog_Types::Test_LineEnding_FromString, "LineEnding_FromString");
    RegisterTest(&TTest_ASWLog_Types::Test_LineEnding_ToString, "LineEnding_ToString");
    RegisterTest(&TTest_ASWLog_Types::Test_LogError_ToString, "LogError_ToString");
    RegisterTest(&TTest_ASWLog_Types::Test_MultilineMode_FromString, "MultilineMode_FromString");
    RegisterTest(&TTest_ASWLog_Types::Test_MultilineMode_ToString, "MultilineMode_ToString");
    RegisterTest(&TTest_ASWLog_Types::Test_PendingEntry_ChangingFieldsWhileVisitingIsSafe, "PendingEntry_ChangingFieldsWhileVisitingIsSafe");
    RegisterTest(&TTest_ASWLog_Types::Test_PendingEntry_FindsFieldsOfTheEntryAndItsScopes, "PendingEntry_FindsFieldsOfTheEntryAndItsScopes");
    RegisterTest(&TTest_ASWLog_Types::Test_PendingEntry_RemoveFieldRemovesItForThisEntry, "PendingEntry_RemoveFieldRemovesItForThisEntry");
    RegisterTest(&TTest_ASWLog_Types::Test_PendingEntry_SetFieldReplacesOrAddsACopy, "PendingEntry_SetFieldReplacesOrAddsACopy");
    RegisterTest(&TTest_ASWLog_Types::Test_PendingEntry_SetMessageOwnsTheText, "PendingEntry_SetMessageOwnsTheText");
    RegisterTest(&TTest_ASWLog_Types::Test_TimePrecision_FromString, "TimePrecision_FromString");
    RegisterTest(&TTest_ASWLog_Types::Test_TimePrecision_ToString, "TimePrecision_ToString");
    RegisterTest(&TTest_ASWLog_Types::Test_TimeZone_FromString, "TimeZone_FromString");
    RegisterTest(&TTest_ASWLog_Types::Test_TimeZone_ToString, "TimeZone_ToString");
}
//---------------------------------------------------------------------------
TTest_ASWLog_Types::~TTest_ASWLog_Types()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_AsyncOverflowPolicy_FromString()
{
    // Arrange
    const auto block = ASWLog::AsyncOverflowPolicy_FromString("BLOCK");
    const auto dropNewest = ASWLog::AsyncOverflowPolicy_FromString("DROP_NEWEST");
    const auto dropNewestAlias_mixedCase = ASWLog::AsyncOverflowPolicy_FromString("DropNewest");
    const auto unknown = ASWLog::AsyncOverflowPolicy_FromString("DROP_OLDEST");

    // Act & Assert
    CheckTrue(block.has_value(), "BLOCK should parse");
    CheckTrue(dropNewest.has_value(), "DROP_NEWEST should parse");
    CheckTrue(dropNewestAlias_mixedCase.has_value(), "DropNewest should parse");
    CheckFalse(unknown.has_value(), "An unknown string should not parse");
    CheckEquals(ASWLog::AsyncOverflowPolicy::Block, *block, "BLOCK should map to Block");
    CheckEquals(ASWLog::AsyncOverflowPolicy::DropNewest, *dropNewest, "DROP_NEWEST should map to DropNewest");
    CheckEquals(ASWLog::AsyncOverflowPolicy::DropNewest, *dropNewestAlias_mixedCase, "DropNewest should map to DropNewest");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_AsyncOverflowPolicy_ToString()
{
    // Arrange / Act / Assert
    CheckEquals(std::string("BLOCK"), std::string(ASWLog::AsyncOverflowPolicy_ToString(ASWLog::AsyncOverflowPolicy::Block)), "Block should stringify as BLOCK");
    CheckEquals(std::string("DROP_NEWEST"), std::string(ASWLog::AsyncOverflowPolicy_ToString(ASWLog::AsyncOverflowPolicy::DropNewest)), "DropNewest should stringify as DROP_NEWEST");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_ColorMode_FromString()
{
    // Arrange
    const auto autoMode = ASWLog::ColorMode_FromString("AUTO");
    const auto always = ASWLog::ColorMode_FromString("ALWAYS");
    const auto never = ASWLog::ColorMode_FromString("NEVER");
    const auto never_mixedCase = ASWLog::ColorMode_FromString("Never");
    const auto unknown = ASWLog::ColorMode_FromString("SOMETIMES");

    // Act & Assert
    CheckTrue(autoMode.has_value(), "AUTO should parse");
    CheckTrue(always.has_value(), "ALWAYS should parse");
    CheckTrue(never.has_value(), "NEVER should parse");
    CheckTrue(never_mixedCase.has_value(), "Never mixed case should parse");
    CheckFalse(unknown.has_value(), "An unknown string should not parse");
    CheckEquals(ASWLog::ColorMode::Auto, *autoMode, "AUTO should map to Auto");
    CheckEquals(ASWLog::ColorMode::Always, *always, "ALWAYS should map to Always");
    CheckEquals(ASWLog::ColorMode::Never, *never, "NEVER should map to Never");
    CheckEquals(ASWLog::ColorMode::Never, *never_mixedCase, "Never should map to Never");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_ColorMode_ToString()
{
    // Arrange
    const std::string autoMode = std::string(ASWLog::ColorMode_ToString(ASWLog::ColorMode::Auto));
    const std::string always = std::string(ASWLog::ColorMode_ToString(ASWLog::ColorMode::Always));
    const std::string never = std::string(ASWLog::ColorMode_ToString(ASWLog::ColorMode::Never));

    // Act & Assert
    CheckEquals(std::string("AUTO"), autoMode, "Auto should stringify as AUTO");
    CheckEquals(std::string("ALWAYS"), always, "Always should stringify as ALWAYS");
    CheckEquals(std::string("NEVER"), never, "Never should stringify as NEVER");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_ErrorKind_ToString()
{
    // Arrange / Act / Assert
    CheckEquals(std::string("OPEN_FAILED"), std::string(ASWLog::ErrorKind_ToString(ASWLog::ErrorKind::OpenFailed)), "OpenFailed should stringify as OPEN_FAILED");
    CheckEquals(std::string("WRITE_FAILED"), std::string(ASWLog::ErrorKind_ToString(ASWLog::ErrorKind::WriteFailed)), "WriteFailed should stringify as WRITE_FAILED");
    CheckEquals(std::string("FLUSH_FAILED"), std::string(ASWLog::ErrorKind_ToString(ASWLog::ErrorKind::FlushFailed)), "FlushFailed should stringify as FLUSH_FAILED");
    CheckEquals(std::string("SYNC_FAILED"), std::string(ASWLog::ErrorKind_ToString(ASWLog::ErrorKind::SyncFailed)), "SyncFailed should stringify as SYNC_FAILED");
    CheckEquals(std::string("CLOSE_FAILED"), std::string(ASWLog::ErrorKind_ToString(ASWLog::ErrorKind::CloseFailed)), "CloseFailed should stringify as CLOSE_FAILED");
    CheckEquals(std::string("ROTATION_FAILED"), std::string(ASWLog::ErrorKind_ToString(ASWLog::ErrorKind::RotationFailed)), "RotationFailed should stringify as ROTATION_FAILED");
    CheckEquals(std::string("DELETE_FAILED"), std::string(ASWLog::ErrorKind_ToString(ASWLog::ErrorKind::DeleteFailed)), "DeleteFailed should stringify as DELETE_FAILED");
    CheckEquals(std::string("EXCEPTION"), std::string(ASWLog::ErrorKind_ToString(ASWLog::ErrorKind::Exception)), "Exception should stringify as EXCEPTION");
    CheckEquals(std::string("ENTRIES_DROPPED"), std::string(ASWLog::ErrorKind_ToString(ASWLog::ErrorKind::EntriesDropped)), "EntriesDropped should stringify as ENTRIES_DROPPED");
    CheckEquals(static_cast<std::size_t>(ASWLog::ErrorKind::EntriesDropped) + 1, ASWLog::ErrorKindCount, "ErrorKindCount should count every kind (EntriesDropped is the last)");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_FlushMode_FromString()
{
    // Arrange
    const auto everyWrite = ASWLog::FlushMode_FromString("EVERY_WRITE");
    const auto everyWriteAlias = ASWLog::FlushMode_FromString("EVERYWRITE");
    const auto onNewLine = ASWLog::FlushMode_FromString("ON_NEW_LINE");
    const auto onNewLineAlias_mixed = ASWLog::FlushMode_FromString("OnNewLine");
    const auto manual = ASWLog::FlushMode_FromString("MANUAL");
    const auto periodic = ASWLog::FlushMode_FromString("PERIODIC");

    // Act & Assert
    CheckTrue(everyWrite.has_value(), "EVERY_WRITE should parse");
    CheckTrue(everyWriteAlias.has_value(), "EVERYWRITE should parse");
    CheckTrue(onNewLine.has_value(), "ON_NEW_LINE should parse");
    CheckTrue(onNewLineAlias_mixed.has_value(), "OnNewLine should parse");
    CheckTrue(manual.has_value(), "MANUAL should parse");
    CheckTrue(periodic.has_value(), "PERIODIC should parse");
    CheckEquals(ASWLog::FlushMode::EveryWrite, *everyWrite, "EVERY_WRITE should map to EveryWrite");
    CheckEquals(ASWLog::FlushMode::EveryWrite, *everyWriteAlias, "EVERYWRITE should map to EveryWrite");
    CheckEquals(ASWLog::FlushMode::OnNewLine, *onNewLine, "ON_NEW_LINE should map to OnNewLine");
    CheckEquals(ASWLog::FlushMode::OnNewLine, *onNewLineAlias_mixed, "OnNewLine should map to OnNewLine");
    CheckEquals(ASWLog::FlushMode::Manual, *manual, "MANUAL should map to Manual");
    CheckEquals(ASWLog::FlushMode::Periodic, *periodic, "PERIODIC should map to Periodic");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_FlushMode_ToString()
{
    // Arrange
    const std::string everyWrite = std::string(ASWLog::FlushMode_ToString(ASWLog::FlushMode::EveryWrite));
    const std::string onNewLine = std::string(ASWLog::FlushMode_ToString(ASWLog::FlushMode::OnNewLine));
    const std::string manual = std::string(ASWLog::FlushMode_ToString(ASWLog::FlushMode::Manual));
    const std::string periodic = std::string(ASWLog::FlushMode_ToString(ASWLog::FlushMode::Periodic));

    // Act & Assert
    CheckEquals(std::string("EVERY_WRITE"), everyWrite, "EVERY_WRITE should stringify as EVERY_WRITE");
    CheckEquals(std::string("ON_NEW_LINE"), onNewLine, "ON_NEW_LINE should stringify as ON_NEW_LINE");
    CheckEquals(std::string("MANUAL"), manual, "MANUAL should stringify as MANUAL");
    CheckEquals(std::string("PERIODIC"), periodic, "PERIODIC should stringify as PERIODIC");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_Level_FromString()
{
    // Arrange
    const auto trace = ASWLog::Level_FromString("TRACE");
    const auto debug = ASWLog::Level_FromString("DEBUG");
    const auto info = ASWLog::Level_FromString("INFO");
    const auto info_mixedCase = ASWLog::Level_FromString("Info");
    const auto warn = ASWLog::Level_FromString("WARN");
    const auto warnAlias = ASWLog::Level_FromString("WARNING");
    const auto error = ASWLog::Level_FromString("ERROR");
    const auto critical = ASWLog::Level_FromString("CRITICAL");
    const auto criticalAlias = ASWLog::Level_FromString("FATAL");
    const auto off = ASWLog::Level_FromString("OFF");
    const auto offAlias = ASWLog::Level_FromString("none");

    // Act & Assert
    CheckTrue(trace.has_value(), "TRACE should parse");
    CheckTrue(debug.has_value(), "DEBUG should parse");
    CheckTrue(info.has_value(), "INFO should parse");
    CheckTrue(info_mixedCase.has_value(), "Info mixed case should parse");
    CheckTrue(warn.has_value(), "WARN should parse");
    CheckTrue(warnAlias.has_value(), "WARNING alias should parse");
    CheckTrue(error.has_value(), "ERROR should parse");
    CheckTrue(critical.has_value(), "CRITICAL should parse");
    CheckTrue(criticalAlias.has_value(), "FATAL should parse");
    CheckEquals(ASWLog::Level::Trace, *trace, "TRACE should map to Trace");
    CheckEquals(ASWLog::Level::Debug, *debug, "DEBUG should map to Debug");
    CheckEquals(ASWLog::Level::Info, *info, "INFO should map to Info");
    CheckEquals(ASWLog::Level::Info, *info_mixedCase, "Info should map to Info");
    CheckEquals(ASWLog::Level::Warn, *warn, "WARN should map to Warn");
    CheckEquals(ASWLog::Level::Warn, *warnAlias, "WARNING should map to Warn");
    CheckEquals(ASWLog::Level::Error, *error, "ERROR should map to Error");
    CheckEquals(ASWLog::Level::Critical, *critical, "CRITICAL should map to Critical");
    CheckEquals(ASWLog::Level::Critical, *criticalAlias, "FATAL should map to Critical");
    CheckTrue(off.has_value() && *off == ASWLog::Level::Off, "OFF should map to Off");
    CheckTrue(offAlias.has_value() && *offAlias == ASWLog::Level::Off, "none (NONE alias, any case) should map to Off");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_Level_ToString()
{
    // Arrange
    const std::string trace = std::string(ASWLog::Level_ToString(ASWLog::Level::Trace));
    const std::string debug = std::string(ASWLog::Level_ToString(ASWLog::Level::Debug));
    const std::string info = std::string(ASWLog::Level_ToString(ASWLog::Level::Info));
    const std::string warn = std::string(ASWLog::Level_ToString(ASWLog::Level::Warn));
    const std::string error = std::string(ASWLog::Level_ToString(ASWLog::Level::Error));
    const std::string critical = std::string(ASWLog::Level_ToString(ASWLog::Level::Critical));
    const std::string off = std::string(ASWLog::Level_ToString(ASWLog::Level::Off));

    // Act & Assert
    CheckEquals(std::string("TRACE"), trace, "Trace should stringify as TRACE");
    CheckEquals(std::string("DEBUG"), debug, "Debug should stringify as DEBUG");
    CheckEquals(std::string("INFO"), info, "Info should stringify as INFO");
    CheckEquals(std::string("WARN"), warn, "Warn should stringify as WARN");
    CheckEquals(std::string("ERROR"), error, "Error should stringify as ERROR");
    CheckEquals(std::string("CRITICAL"), critical, "Critical should stringify as CRITICAL");
    CheckEquals(std::string("OFF"), off, "Off should stringify as OFF");
    CheckEquals(static_cast<std::size_t>(ASWLog::Level::Off), ASWLog::LevelCount,
        "LevelCount should count the severity levels, which come before Off");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_LineEnding_FromString()
{
    // Arrange
    const auto lf = ASWLog::LineEnding_FromString("LF");
    const auto lfAlias = ASWLog::LineEnding_FromString("LINUX");
    const auto lf_lower = ASWLog::LineEnding_FromString("lf");
    const auto crlf = ASWLog::LineEnding_FromString("CRLF");
    const auto crlfAlias = ASWLog::LineEnding_FromString("WINDOWS");

    // Act & Assert
    CheckTrue(lf.has_value(), "LF should parse");
    CheckTrue(lfAlias.has_value(), "LINUX should parse");
    CheckTrue(lf_lower.has_value(), "lf should parse");
    CheckTrue(crlf.has_value(), "CRLF should parse");
    CheckTrue(crlfAlias.has_value(), "WINDOWS should parse");
    CheckEquals(ASWLog::LineEnding::LF, *lf, "LF should map to LF");
    CheckEquals(ASWLog::LineEnding::LF, *lfAlias, "LINUX should map to LF");
    CheckEquals(ASWLog::LineEnding::LF, *lf_lower, "lf should map to LF");
    CheckEquals(ASWLog::LineEnding::CRLF, *crlf, "CRLF should map to CRLF");
    CheckEquals(ASWLog::LineEnding::CRLF, *crlfAlias, "WINDOWS should map to CRLF");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_LineEnding_ToString()
{
    // Arrange
    const std::string lf = std::string(ASWLog::LineEnding_ToString(ASWLog::LineEnding::LF));
    const std::string crlf = std::string(ASWLog::LineEnding_ToString(ASWLog::LineEnding::CRLF));

    // Act & Assert
    CheckEquals(std::string("LF"), lf, "LF should stringify as LF");
    CheckEquals(std::string("CRLF"), crlf, "CRLF should stringify as CRLF");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_LogError_ToString()
{
    // Arrange
    ASWLog::TASWLogError messageOnly;
    messageOnly.Kind = ASWLog::ErrorKind::Exception;
    messageOnly.Message = "Dropped an entry: out of memory";

    const auto code = std::make_error_code(std::errc::no_such_file_or_directory);
    ASWLog::TASWLogError everyPart;
    everyPart.Kind = ASWLog::ErrorKind::OpenFailed;
    everyPart.Message = "Couldn't open the log file";
    everyPart.Path = "logs/app.log";
    everyPart.Code = code;
    everyPart.SuppressedCount = 3;

    // Act
    const auto messageOnlyText = messageOnly.ToString();
    const auto everyPartText = everyPart.ToString();

    // Assert
    CheckEquals(std::string("EXCEPTION: Dropped an entry: out of memory"), messageOnlyText, "The empty parts should be left out");
    CheckEquals("OPEN_FAILED: Couldn't open the log file 'logs/app.log': " + code.message() + " (3 more not reported)", everyPartText, "Every part should be in the line");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_MultilineMode_FromString()
{
    // Arrange
    const auto preserve = ASWLog::MultilineMode_FromString("PRESERVE");
    const auto indent = ASWLog::MultilineMode_FromString("indent");
    const auto escape = ASWLog::MultilineMode_FromString("Escape");
    const auto unknown = ASWLog::MultilineMode_FromString("verbatim");

    // Act & Assert
    CheckTrue(preserve == ASWLog::MultilineMode::Preserve, "PRESERVE should map to Preserve");
    CheckTrue(indent == ASWLog::MultilineMode::Indent, "indent should map to Indent, ignoring case");
    CheckTrue(escape == ASWLog::MultilineMode::Escape, "Escape should map to Escape, ignoring case");
    CheckFalse(unknown.has_value(), "An unknown mode should not parse");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_MultilineMode_ToString()
{
    // Act & Assert
    CheckEquals(std::string("PRESERVE"), std::string(ASWLog::MultilineMode_ToString(ASWLog::MultilineMode::Preserve)), "Preserve should stringify as PRESERVE");
    CheckEquals(std::string("INDENT"), std::string(ASWLog::MultilineMode_ToString(ASWLog::MultilineMode::Indent)), "Indent should stringify as INDENT");
    CheckEquals(std::string("ESCAPE"), std::string(ASWLog::MultilineMode_ToString(ASWLog::MultilineMode::Escape)), "Escape should stringify as ESCAPE");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_PendingEntry_ChangingFieldsWhileVisitingIsSafe()
{
    // Arrange: text fields from a scope, then (after the first change) the entry's own copies
    ASWLog::TASWLogScope scope{ { "a", "1" }, { "b", "2" }, { "c", "3" } };
    ASWLog::TASWLogRecord record;
    record.Scope = ASWLog::TASWLogScope::GetCurrent();
    ASWLog::TASWPendingEntry entry(record);
    std::vector<std::string> firstVisit;
    std::vector<std::string> secondVisit;

    // Act: each change replaces the copies the visit is going over (a visit over freed copies is undefined behavior,
    // which AddressSanitizer reports; without it the freed memory usually still holds the old fields)
    entry.ForEachField([&](const ASWLog::TASWLogField& field) {
            firstVisit.push_back(std::format("{}={}", field.Key, field.Value.GetText()));
            entry.SetField(field.Key, "x");
        });
    entry.ForEachField([&](const ASWLog::TASWLogField& field) {
            secondVisit.push_back(std::format("{}={}", field.Key, field.Value.GetText()));
            entry.SetField(field.Key, std::string(field.Value.GetText()) + "y");
        });

    // Assert
    CheckTrue(firstVisit == std::vector<std::string>{ "a=1", "b=2", "c=3" }, "The first visit should go over the fields as they were");
    CheckTrue(secondVisit == std::vector<std::string>{ "a=x", "b=x", "c=x" }, "The second visit should go over the first visit's changes");
    CheckTrue(DescribeFields(entry) == std::vector<std::string>{ "a=xy", "b=xy", "c=xy" }, "Every change should be kept");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_PendingEntry_FindsFieldsOfTheEntryAndItsScopes()
{
    // Arrange: a scope and the entry's own fields, one of which replaces the scope's
    ASWLog::TASWLogScope scope{ { "requestId", "8f3a" }, { "user", "amy" } };
    const ASWLog::TASWLogField own[] = { { "user", "bob" }, { "orderId", 17 } };
    const std::span<const ASWLog::TASWLogField> ownFields(own);
    ASWLog::TASWLogRecord record;
    record.Fields = &ownFields;
    record.Scope = ASWLog::TASWLogScope::GetCurrent();
    const ASWLog::TASWPendingEntry entry(record);

    // Act
    const auto* requestId = entry.FindField("requestId");
    const auto* user = entry.FindField("user");
    const auto* orderId = entry.FindField("orderId");
    const auto* missing = entry.FindField("missing");

    // Assert
    AssertNotNull(requestId, "A scope's field should be found");
    AssertNotNull(user, "A field the entry and its scope both have should be found");
    AssertNotNull(orderId, "The entry's own field should be found");
    CheckEquals(std::string("8f3a"), std::string(requestId->GetText()), "The scope's value should be returned");
    CheckEquals(std::string("bob"), std::string(user->GetText()), "The entry's own value should beat the scope's");
    CheckEquals(17, orderId->GetInt(), "The entry's own value should be returned");
    CheckNull(missing, "A field the entry doesn't have should give null");
    CheckTrue(DescribeFields(entry) == std::vector<std::string>{ "requestId=8f3a", "user=bob", "orderId=17" },
        "ForEachField() should give each key once, as written");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_PendingEntry_RemoveFieldRemovesItForThisEntry()
{
    // Arrange
    ASWLog::TASWLogScope scope{ { "user", "amy" }, { "requestId", "8f3a" } };
    const ASWLog::TASWLogField own[] = { { "user", "bob" }, { "orderId", 17 } };
    const std::span<const ASWLog::TASWLogField> ownFields(own);
    ASWLog::TASWLogRecord record;
    record.Fields = &ownFields;
    record.Scope = ASWLog::TASWLogScope::GetCurrent();
    ASWLog::TASWPendingEntry entry(record);

    // Act
    entry.RemoveField("missing");
    const auto* fieldsAfterMissing = entry.GetRecord().Fields;
    entry.RemoveField("user");

    // Assert
    CheckSame(record.Fields, fieldsAfterMissing, "Removing a field the entry doesn't have should change nothing");
    CheckTrue(DescribeFields(entry) == std::vector<std::string>{ "requestId=8f3a", "orderId=17" },
        "The field should be gone, its own value and the scope's");
    CheckNull(entry.GetRecord().Scope, "The entry's fields should no longer depend on the scope");
    CheckEquals(std::string("amy"), std::string(ASWLog::TASWLogScope::GetCurrent()->GetFields()[0].Value.GetText()),
        "The scope itself should keep the field");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_PendingEntry_SetFieldReplacesOrAddsACopy()
{
    // Arrange
    ASWLog::TASWLogScope scope{ { "requestId", "8f3a" }, { "user", "amy" } };
    const ASWLog::TASWLogField own[] = { { "orderId", 17 } };
    const std::span<const ASWLog::TASWLogField> ownFields(own);
    ASWLog::TASWLogRecord record;
    record.Fields = &ownFields;
    record.Scope = ASWLog::TASWLogScope::GetCurrent();
    ASWLog::TASWPendingEntry entry(record);

    // Act: the key and value of the added field are temporaries
    entry.SetField("user", "***");
    {
        std::string key = "token";
        std::string value = "secret_value";
        entry.SetField(key, value);
        key.assign(key.size(), '?');
        value.assign(value.size(), '?');
    }
    entry.SetField("orderId", 18);

    // Assert
    CheckTrue(DescribeFields(entry) == std::vector<std::string>{ "requestId=8f3a", "user=***", "orderId=18", "token=secret_value" },
        "A field should be replaced where it is, and a new one added after the others, as a copy");
    CheckSame(record.Fields, &ownFields, "The record the entry was made from should be unchanged");
    CheckEquals(17, ownFields[0].Value.GetInt(), "The caller's fields should be unchanged");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_PendingEntry_SetMessageOwnsTheText()
{
    // Arrange
    ASWLog::TASWLogRecord record;
    record.LogLevel = ASWLog::Level::Warn;
    record.Raw = true;
    record.Message = "password=secret";
    record.Category = "Net";
    ASWLog::TASWPendingEntry entry(record);

    // Act: the new message is a temporary
    {
        std::string redacted = "password=***";
        entry.SetMessage(redacted);
        redacted.assign(redacted.size(), '?');
    }

    // Assert
    CheckEquals(std::string("password=***"), std::string(entry.GetRecord().Message), "The entry should own its new message");
    CheckEquals(std::string("password=secret"), std::string(record.Message), "The original record should be unchanged");
    CheckEquals(ASWLog::Level::Warn, entry.GetRecord().LogLevel, "The level should be kept");
    CheckTrue(entry.GetRecord().Raw, "The flags should be kept");
    CheckEquals(std::string("Net"), std::string(entry.GetRecord().Category), "The category should be kept");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_TimePrecision_FromString()
{
    // Arrange
    const auto milliseconds = ASWLog::TimePrecision_FromString("MILLISECONDS");
    const auto microseconds = ASWLog::TimePrecision_FromString("microseconds");
    const auto nanoseconds = ASWLog::TimePrecision_FromString("NANOSECONDS");
    const auto msAlias = ASWLog::TimePrecision_FromString("ms");
    const auto usAlias = ASWLog::TimePrecision_FromString("US");
    const auto nsAlias = ASWLog::TimePrecision_FromString("NS");
    const auto unknown = ASWLog::TimePrecision_FromString("seconds");

    // Act & Assert
    CheckTrue(milliseconds == ASWLog::TimePrecision::Milliseconds, "MILLISECONDS should map to Milliseconds");
    CheckTrue(microseconds == ASWLog::TimePrecision::Microseconds, "microseconds should map to Microseconds, ignoring case");
    CheckTrue(nanoseconds == ASWLog::TimePrecision::Nanoseconds, "NANOSECONDS should map to Nanoseconds");
    CheckTrue(msAlias == ASWLog::TimePrecision::Milliseconds, "ms should map to Milliseconds");
    CheckTrue(usAlias == ASWLog::TimePrecision::Microseconds, "US should map to Microseconds");
    CheckTrue(nsAlias == ASWLog::TimePrecision::Nanoseconds, "NS should map to Nanoseconds");
    CheckFalse(unknown.has_value(), "An unknown precision should not parse");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_TimePrecision_ToString()
{
    // Act & Assert
    CheckEquals(std::string("MILLISECONDS"), std::string(ASWLog::TimePrecision_ToString(ASWLog::TimePrecision::Milliseconds)), "Milliseconds should stringify as MILLISECONDS");
    CheckEquals(std::string("MICROSECONDS"), std::string(ASWLog::TimePrecision_ToString(ASWLog::TimePrecision::Microseconds)), "Microseconds should stringify as MICROSECONDS");
    CheckEquals(std::string("NANOSECONDS"), std::string(ASWLog::TimePrecision_ToString(ASWLog::TimePrecision::Nanoseconds)), "Nanoseconds should stringify as NANOSECONDS");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_TimeZone_FromString()
{
    // Arrange
    const auto utc = ASWLog::TimeZone_FromString("UTC");
    const auto local = ASWLog::TimeZone_FromString("local");
    const auto unknown = ASWLog::TimeZone_FromString("EST");

    // Act & Assert
    CheckTrue(utc == ASWLog::TimeZone::UTC, "UTC should map to UTC");
    CheckTrue(local == ASWLog::TimeZone::Local, "local should map to Local, ignoring case");
    CheckFalse(unknown.has_value(), "A named time zone should not parse");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_TimeZone_ToString()
{
    // Act & Assert
    CheckEquals(std::string("UTC"), std::string(ASWLog::TimeZone_ToString(ASWLog::TimeZone::UTC)), "UTC should stringify as UTC");
    CheckEquals(std::string("LOCAL"), std::string(ASWLog::TimeZone_ToString(ASWLog::TimeZone::Local)), "Local should stringify as LOCAL");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_Types)
