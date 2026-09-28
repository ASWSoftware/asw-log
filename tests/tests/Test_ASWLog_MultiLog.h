/* **************************************************************************
Test_ASWLog_MultiLog.h
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

#ifndef Test_ASWLog_MultiLogH
#define Test_ASWLog_MultiLogH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_MultiLog
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_MultiLog : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_AddLogger_RejectsDuplicateRegistration();
    void Test_AddLogger_RejectsSelfRegistration();
    void Test_Contains_ReflectsRegistrationState();
    void Test_GetLoggerCount_ReflectsAddAndRemove();
    void Test_GetLoggers_ReturnsSnapshotOfRegisteredSinks();
    void Test_IsOpen_RequiresAllSinksOpen();
    void Test_Log_FansOutToAllRegisteredSinks();
    void Test_LogForce_BypassesCompositeGate();
    void Test_RemoveAllLoggers_ClearsRegistrationAndReturnsCount();
    void Test_RemoveLogger_StopsReceivingEntries();
    void Test_SetMinimumLevel_GatesFanOutBeforeSinks();

public:
    TTest_ASWLog_MultiLog();
    ~TTest_ASWLog_MultiLog() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_MultiLogH
