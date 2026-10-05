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
#include <optional>
#include <string>
#include <system_error>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWLog_Types.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

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
    CheckEquals(static_cast<int32_t>(ASWLog::AsyncOverflowPolicy::Block), static_cast<int32_t>(*block), "BLOCK should map to Block");
    CheckEquals(static_cast<int32_t>(ASWLog::AsyncOverflowPolicy::DropNewest), static_cast<int32_t>(*dropNewest), "DROP_NEWEST should map to DropNewest");
    CheckEquals(static_cast<int32_t>(ASWLog::AsyncOverflowPolicy::DropNewest), static_cast<int32_t>(*dropNewestAlias_mixedCase), "DropNewest should map to DropNewest");
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
    CheckEquals(static_cast<int32_t>(ASWLog::ColorMode::Auto), static_cast<int32_t>(*autoMode), "AUTO should map to Auto");
    CheckEquals(static_cast<int32_t>(ASWLog::ColorMode::Always), static_cast<int32_t>(*always), "ALWAYS should map to Always");
    CheckEquals(static_cast<int32_t>(ASWLog::ColorMode::Never), static_cast<int32_t>(*never), "NEVER should map to Never");
    CheckEquals(static_cast<int32_t>(ASWLog::ColorMode::Never), static_cast<int32_t>(*never_mixedCase), "Never should map to Never");
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
    CheckEquals(static_cast<int32_t>(ASWLog::FlushMode::EveryWrite), static_cast<int32_t>(*everyWrite), "EVERY_WRITE should map to EveryWrite");
    CheckEquals(static_cast<int32_t>(ASWLog::FlushMode::EveryWrite), static_cast<int32_t>(*everyWriteAlias), "EVERYWRITE should map to EveryWrite");
    CheckEquals(static_cast<int32_t>(ASWLog::FlushMode::OnNewLine), static_cast<int32_t>(*onNewLine), "ON_NEW_LINE should map to OnNewLine");
    CheckEquals(static_cast<int32_t>(ASWLog::FlushMode::OnNewLine), static_cast<int32_t>(*onNewLineAlias_mixed), "OnNewLine should map to OnNewLine");
    CheckEquals(static_cast<int32_t>(ASWLog::FlushMode::Manual), static_cast<int32_t>(*manual), "MANUAL should map to Manual");
    CheckEquals(static_cast<int32_t>(ASWLog::FlushMode::Periodic), static_cast<int32_t>(*periodic), "PERIODIC should map to Periodic");
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
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Trace), static_cast<int32_t>(*trace), "TRACE should map to Trace");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Debug), static_cast<int32_t>(*debug), "DEBUG should map to Debug");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Info), static_cast<int32_t>(*info), "INFO should map to Info");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Info), static_cast<int32_t>(*info_mixedCase), "Info should map to Info");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Warn), static_cast<int32_t>(*warn), "WARN should map to Warn");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Warn), static_cast<int32_t>(*warnAlias), "WARNING should map to Warn");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Error), static_cast<int32_t>(*error), "ERROR should map to Error");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Critical), static_cast<int32_t>(*critical), "CRITICAL should map to Critical");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Critical), static_cast<int32_t>(*criticalAlias), "FATAL should map to Critical");
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
    CheckEquals(static_cast<int32_t>(ASWLog::LineEnding::LF), static_cast<int32_t>(*lf), "LF should map to LF");
    CheckEquals(static_cast<int32_t>(ASWLog::LineEnding::LF), static_cast<int32_t>(*lfAlias), "LINUX should map to LF");
    CheckEquals(static_cast<int32_t>(ASWLog::LineEnding::LF), static_cast<int32_t>(*lf_lower), "lf should map to LF");
    CheckEquals(static_cast<int32_t>(ASWLog::LineEnding::CRLF), static_cast<int32_t>(*crlf), "CRLF should map to CRLF");
    CheckEquals(static_cast<int32_t>(ASWLog::LineEnding::CRLF), static_cast<int32_t>(*crlfAlias), "WINDOWS should map to CRLF");
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

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP(ASWUnitTests::TTest_ASWLog_Types)
