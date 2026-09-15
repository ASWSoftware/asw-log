/* **************************************************************************
Main.cpp
Author: Anthony S. West - ASW Software

Console app that runs AswUnitTests framework.

Copyright 2025 Anthony S. West

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
#include <exception>
#include <iostream>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Handler.h"
//---------------------------------------------------------------------------
using namespace ASWUnitTests;
//---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    // Check if user requested the version flag
    if (argc > 1 && std::string_view(argv[1]) == "--version")
    {
        std::cout << TTestHandler::GetVersionFullStr() << std::endl;
        return 0;
    }

    int returnCode = 0; // no error

    try
    {
        TTestHandler tester;

        tester.Initialize();

        TTestResults testResults = tester.Run();
        unsigned int nTestsFailed = testResults.FailedCount;
        if (nTestsFailed > 0)
            returnCode = 1;
    }
    catch (std::exception const& ex)
    {
        std::cout << "\nTerminating app. Unhandled exception: " << ex.what() << std::endl;
        returnCode = 2;
    }
    catch (...)
    {
        std::cout << "\nTerminating app. Unhandled exception: Unknown" << std::endl;
        returnCode = 3;
    }

#if !defined(NDEBUG)
#  if defined(_WIN32)
    system("pause");
#  else
    std::cout << "Press enter to continue..." << std::endl;
    std::cin.get();
#  endif
#endif

    return returnCode;
}
