/* **************************************************************************
Test_ASWLog_Types.cpp
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
#include "Test_ASWLog_Types.h"
//---------------------------------------------------------------------------
#include "ASWLog_Types.h"
//---------------------------------------------------------------------------
#include <optional>
#include <string>
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

//---------------------------------------------------------------------------
TTest_ASWLog_Types::TTest_ASWLog_Types()
    : inherited("ASWLog_Types_Tests")
{
    RegisterTest(&TTest_ASWLog_Types::Test_FlushMode_FromString, "FlushMode_FromString");
    RegisterTest(&TTest_ASWLog_Types::Test_FlushMode_ToString, "FlushMode_ToString");
    RegisterTest(&TTest_ASWLog_Types::Test_Level_FromString, "Level_FromString");
    RegisterTest(&TTest_ASWLog_Types::Test_Level_ToString, "Level_ToString");
    RegisterTest(&TTest_ASWLog_Types::Test_LineEnding_FromString, "LineEnding_FromString");
    RegisterTest(&TTest_ASWLog_Types::Test_LineEnding_ToString, "LineEnding_ToString");
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
    CheckTrue(everyWrite.has_value(), __func__, __LINE__, "EVERY_WRITE should parse");
    CheckTrue(everyWriteAlias.has_value(), __func__, __LINE__, "EVERYWRITE should parse");
    CheckTrue(onNewLine.has_value(), __func__, __LINE__, "ON_NEW_LINE should parse");
    CheckTrue(onNewLineAlias_mixed.has_value(), __func__, __LINE__, "OnNewLine should parse");
    CheckTrue(manual.has_value(), __func__, __LINE__, "MANUAL should parse");
    CheckTrue(periodic.has_value(), __func__, __LINE__, "PERIODIC should parse");
    CheckEquals(static_cast<int32_t>(ASWLog::FlushMode::EveryWrite), static_cast<int32_t>(*everyWrite), __func__, __LINE__, "EVERY_WRITE should map to EveryWrite");
    CheckEquals(static_cast<int32_t>(ASWLog::FlushMode::EveryWrite), static_cast<int32_t>(*everyWriteAlias), __func__, __LINE__, "EVERYWRITE should map to EveryWrite");
    CheckEquals(static_cast<int32_t>(ASWLog::FlushMode::OnNewLine), static_cast<int32_t>(*onNewLine), __func__, __LINE__, "ON_NEW_LINE should map to OnNewLine");
    CheckEquals(static_cast<int32_t>(ASWLog::FlushMode::OnNewLine), static_cast<int32_t>(*onNewLineAlias_mixed), __func__, __LINE__, "OnNewLine should map to OnNewLine");
    CheckEquals(static_cast<int32_t>(ASWLog::FlushMode::Manual), static_cast<int32_t>(*manual), __func__, __LINE__, "MANUAL should map to Manual");
    CheckEquals(static_cast<int32_t>(ASWLog::FlushMode::Periodic), static_cast<int32_t>(*periodic), __func__, __LINE__, "PERIODIC should map to Periodic");
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
    CheckEquals(std::string("EVERY_WRITE"), everyWrite, __func__, __LINE__, "EVERY_WRITE should stringify as EVERY_WRITE");
    CheckEquals(std::string("ON_NEW_LINE"), onNewLine, __func__, __LINE__, "ON_NEW_LINE should stringify as ON_NEW_LINE");
    CheckEquals(std::string("MANUAL"), manual, __func__, __LINE__, "MANUAL should stringify as MANUAL");
    CheckEquals(std::string("PERIODIC"), periodic, __func__, __LINE__, "PERIODIC should stringify as PERIODIC");
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

    // Act & Assert
    CheckTrue(trace.has_value(), __func__, __LINE__, "TRACE should parse");
    CheckTrue(debug.has_value(), __func__, __LINE__, "DEBUG should parse");
    CheckTrue(info.has_value(), __func__, __LINE__, "INFO should parse");
    CheckTrue(info_mixedCase.has_value(), __func__, __LINE__, "Info mixed case should parse");
    CheckTrue(warn.has_value(), __func__, __LINE__, "WARN should parse");
    CheckTrue(warnAlias.has_value(), __func__, __LINE__, "WARNING alias should parse");
    CheckTrue(error.has_value(), __func__, __LINE__, "ERROR should parse");
    CheckTrue(critical.has_value(), __func__, __LINE__, "CRITICAL should parse");
    CheckTrue(criticalAlias.has_value(), __func__, __LINE__, "FATAL should parse");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Trace), static_cast<int32_t>(*trace), __func__, __LINE__, "TRACE should map to Trace");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Debug), static_cast<int32_t>(*debug), __func__, __LINE__, "DEBUG should map to Debug");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Info), static_cast<int32_t>(*info), __func__, __LINE__, "INFO should map to Info");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Info), static_cast<int32_t>(*info_mixedCase), __func__, __LINE__, "Info should map to Info");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Warn), static_cast<int32_t>(*warn), __func__, __LINE__, "WARN should map to Warn");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Warn), static_cast<int32_t>(*warnAlias), __func__, __LINE__, "WARNING should map to Warn");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Error), static_cast<int32_t>(*error), __func__, __LINE__, "ERROR should map to Error");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Critical), static_cast<int32_t>(*critical), __func__, __LINE__, "CRITICAL should map to Critical");
    CheckEquals(static_cast<int32_t>(ASWLog::Level::Critical), static_cast<int32_t>(*criticalAlias), __func__, __LINE__, "FATAL should map to Critical");
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

    // Act & Assert
    CheckEquals(std::string("TRACE"), trace, __func__, __LINE__, "Trace should stringify as TRACE");
    CheckEquals(std::string("DEBUG"), debug, __func__, __LINE__, "Debug should stringify as DEBUG");
    CheckEquals(std::string("INFO"), info, __func__, __LINE__, "Info should stringify as INFO");
    CheckEquals(std::string("WARN"), warn, __func__, __LINE__, "Warn should stringify as WARN");
    CheckEquals(std::string("ERROR"), error, __func__, __LINE__, "Error should stringify as ERROR");
    CheckEquals(std::string("CRITICAL"), critical, __func__, __LINE__, "Critical should stringify as CRITICAL");
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
    CheckTrue(lf.has_value(), __func__, __LINE__, "LF should parse");
    CheckTrue(lfAlias.has_value(), __func__, __LINE__, "LINUX should parse");
    CheckTrue(lf_lower.has_value(), __func__, __LINE__, "lf should parse");
    CheckTrue(crlf.has_value(), __func__, __LINE__, "CRLF should parse");
    CheckTrue(crlfAlias.has_value(), __func__, __LINE__, "WINDOWS should parse");
    CheckEquals(static_cast<int32_t>(ASWLog::LineEnding::LF), static_cast<int32_t>(*lf), __func__, __LINE__, "LF should map to LF");
    CheckEquals(static_cast<int32_t>(ASWLog::LineEnding::LF), static_cast<int32_t>(*lfAlias), __func__, __LINE__, "LINUX should map to LF");
    CheckEquals(static_cast<int32_t>(ASWLog::LineEnding::LF), static_cast<int32_t>(*lf_lower), __func__, __LINE__, "lf should map to LF");
    CheckEquals(static_cast<int32_t>(ASWLog::LineEnding::CRLF), static_cast<int32_t>(*crlf), __func__, __LINE__, "CRLF should map to CRLF");
    CheckEquals(static_cast<int32_t>(ASWLog::LineEnding::CRLF), static_cast<int32_t>(*crlfAlias), __func__, __LINE__, "WINDOWS should map to CRLF");
}
//---------------------------------------------------------------------------
void TTest_ASWLog_Types::Test_LineEnding_ToString()
{
    // Arrange
    const std::string lf = std::string(ASWLog::LineEnding_ToString(ASWLog::LineEnding::LF));
    const std::string crlf = std::string(ASWLog::LineEnding_ToString(ASWLog::LineEnding::CRLF));

    // Act & Assert
    CheckEquals(std::string("LF"), lf, __func__, __LINE__, "LF should stringify as LF");
    CheckEquals(std::string("CRLF"), crlf, __func__, __LINE__, "CRLF should stringify as CRLF");
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests
