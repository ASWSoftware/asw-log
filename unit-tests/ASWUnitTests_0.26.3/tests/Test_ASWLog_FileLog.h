/* **************************************************************************
Test_ASWLog_FileLog.h
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

#ifndef Test_ASWLog_FileLogH
#define Test_ASWLog_FileLogH
//---------------------------------------------------------------------------
#include "ASWUnitTests_TestBase.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

///////////////////////////////////////////////////////////////////////////
// TTest_ASWLog_FileLog
///////////////////////////////////////////////////////////////////////////
class TTest_ASWLog_FileLog : public TTestGroupBase
{
private:
    typedef TTestGroupBase inherited;

private: // Test methods
    void Test_DeleteOldLogs_RemovesOldFiles();
    void Test_InitializeAndLogInfo_WritesText();
    void Test_LogFormatMethods_FormatsMessage();
    void Test_LogLineMetadata_Options();
    void Test_LogNewLineAndForceOptions();
    void Test_LogRawOptions();
    void Test_MultiThreadedStress_WritesAllMessagesToDisk();
    void Test_MultiThreadedStress_WritesAllMessagesToDisk_OpenClose();

public:
    TTest_ASWLog_FileLog();
    ~TTest_ASWLog_FileLog() override;

    void SetUp_Group() override;
    void SetUp_Test(ITestCase& testCase) override;
    void TearDown_Group() override;
    void TearDown_Test(ITestCase& testCase) override;
};

} // ASWUnitTests

//---------------------------------------------------------------------------
#endif // #ifndef Test_ASWLog_FileLogH
