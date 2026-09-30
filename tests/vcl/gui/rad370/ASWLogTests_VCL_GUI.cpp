/* **************************************************************************
ASWLogTests_VCL_GUI.cpp
Author: Anthony S. West - ASW Software

VCL GUI app that runs the ASWLog unit tests with the ASWUnitTests
framework's GUI runner (third_party\asw-unit-tests\vcl\gui\src).

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
#include <vcl.h>
#pragma hdrstop
#include <tchar.h>
//---------------------------------------------------------------------------
#include <exception>
#include <string>
//---------------------------------------------------------------------------
#include "ASWUnitTests_GUI_CommandLine.h"
#include "ASWUnitTests_GUI_TextDialog.h"
//---------------------------------------------------------------------------
USEFORM("..\..\..\..\third_party\asw-unit-tests\vcl\gui\src\ASWUnitTests_GUI_MainForm.cpp", FormASWUnitTestsGUIMain);
//---------------------------------------------------------------------------
#include "ASWUnitTests_GUI_MainForm.h"
//---------------------------------------------------------------------------
int WINAPI _tWinMain(HINSTANCE, HINSTANCE, LPTSTR, int)
{
    int exitCode = ASWUnitTests::ExitCode_Success;

    try
    {
        Application->Initialize();
        Application->MainFormOnTaskBar = true;
        Application->Title = "ASWLogTests";

        // Parsed before the main form exists, so --help, --version, and argument errors are shown on their own
        // and the process exits with the same code the console runner would use, without opening the GUI.
        ASWUnitTests::TGUIOptions options;
        ASWUnitTests::TGUICommandLineResult const commandLine =
            ASWUnitTests::TGUICommandLine::Parse(ASWUnitTests::TGUICommandLine::ProcessArguments(), options);

        if (commandLine.EarlyExitCode.has_value())
        {
            ASWUnitTests::ShowTextDialog(Application->Title, commandLine.Message);
            return *commandLine.EarlyExitCode;
        }

        Application->CreateForm(__classid(TFormASWUnitTestsGUIMain), &FormASWUnitTestsGUIMain);
        FormASWUnitTestsGUIMain->Start(options);
        Application->Run();

        // The form outlives Run(); Application frees it later, during shutdown.
        exitCode = FormASWUnitTestsGUIMain->ExitCode();
    }
    catch (Exception& exception)
    {
        Application->ShowException(&exception);
        exitCode = ASWUnitTests::ExitCode_UnhandledException;
    }
    catch (std::exception const& exception)
    {
        ASWUnitTests::ShowTextDialog(Application->Title, std::string("Unhandled exception: ") + exception.what());
        exitCode = ASWUnitTests::ExitCode_UnhandledException;
    }
    catch (...)
    {
        try
        {
            throw Exception("");
        }
        catch (Exception& exception)
        {
            Application->ShowException(&exception);
        }

        exitCode = ASWUnitTests::ExitCode_UnhandledExceptionUnknown;
    }

    return exitCode;
}
//---------------------------------------------------------------------------
