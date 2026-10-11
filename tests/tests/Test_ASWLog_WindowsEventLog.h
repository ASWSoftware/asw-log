/* **************************************************************************
Test_ASWLog_WindowsEventLog.h
Author: Anthony S. West - ASW Software

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

#pragma once

#ifndef Test_ASWLog_WindowsEventLogH
#define Test_ASWLog_WindowsEventLogH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_WindowsEventLog
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_WindowsEventLog : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_GetInstance_ReturnsTheSameLogger();
    void Test_HandleCrash_ReportsTheFormattedCrashLineOnly();
    void Test_Open_OpensTheConfiguredSource();
    void Test_RegisterSource_NeedsAdministratorRights();
    void Test_Report_CutsTooLongMessages();
    void Test_Report_MapsEachLevelToATypeAndEventId();
    void Test_Report_StripsOnlyTheLineEnding();
    void Test_ShouldLog_FollowsThePlatform();
    void Test_Windows_ReportsAFailedOpen();
    void Test_Windows_ReportsARealEvent();
    void Test_Windows_ReportsARefusedEvent();

public:
    TTest_ASWLog_WindowsEventLog();
    ~TTest_ASWLog_WindowsEventLog() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_WindowsEventLogH
