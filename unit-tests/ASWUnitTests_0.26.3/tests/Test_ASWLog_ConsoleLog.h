/* **************************************************************************
Test_ASWLog_ConsoleLog.h
Author: Anthony S. West - ASW Software

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

#ifndef Test_ASWLog_ConsoleLogH
#define Test_ASWLog_ConsoleLogH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_ConsoleLog
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_ConsoleLog : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_GetUseColor_ReflectsSetUseColor();
    void Test_Initialize_SuppressesInfoBannersBelowMinimumLevel();
    void Test_Initialize_WritesDriveInfoWhenEnabled();
    void Test_IsColorSupported_ReflectsPlatformState();
    void Test_LogLineMetadata_Options();
    void Test_LogRawAndForceOptions();
    void Test_LogRespectsMinimumLevel();
    void Test_OnLogEntry_FiresForQualifyingLevelsOnly();
    void Test_ResetLevelColor_RestoresDefault();
    void Test_ResetLevelColors_RestoresAllDefaults();
    void Test_SetLevelColor_EmptyStringDisablesColorForLevel();
    void Test_SetLevelColor_OverridesDefaultColor();
    void Test_UseColor_False_SuppressesAnsiCodes();
    void Test_UseColor_WrapsOutputWithAnsiCodes();
    void Test_WarnAndAboveWriteToStdErr();

public:
    TTest_ASWLog_ConsoleLog();
    ~TTest_ASWLog_ConsoleLog() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_ConsoleLogH
