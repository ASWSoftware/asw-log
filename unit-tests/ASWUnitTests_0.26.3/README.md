# ASWUnitTests - https://github.com/ASWSoftware/asw-unit-tests

ASWUnitTests is a speedy light-weight C++ unit test tool for Windows and Linux projects.

# Features

- `Check` prefix methods for `assert` sections of unit tests that aren't intended to throw (e.g. CheckTrue())
- `Assert` prefix methods for `assert` sections of unit tests that should throw right away (e.g. AssertTrue())
- `SetExceptionExpected()` - basic testing support for expected exceptions

See `ASWUnitTests_TestBase.h` for basic list of supported `Check/Assert` methods.
See the example unit test `Test_ASWTools_String.cpp` in `tests` folder for how to use `SetExceptionExpected()`.

# Donations:

If you find ASWUnitTests helpful, donations are always appreciated:

PayPal:
donate@aswsoftware.com

Bitcoin:
15rKqL1numHJyE36ottMbhs5cmCjJkuowV

# How to Use

Important! Tests should be run outside of the debugger, unless you really want to see every exception that occurs, due
to the nature of unit testing.

For examples of how to use, see the `tests` and `toTest` folders and `src\ASWUnitTests_Handler.cpp`.

ASWUnitTests is intended to exist in a sub folder within your project at the root (e.g. `myProject\asw-unit-tests`).

Modify the project files within (e.g. `rad370` or `cmake`, etc.) to point to your source.
The `toTest` folder is an example of source that is to be tested. While this example folder exists in the root of this
project, your source should be wherever you like.

## CMake

The `cmake` folder contains a portable CMake project for building with CMake, JetBrains CLion, Visual Studio, Clang,
or MinGW. From the repository root, configure and build it with:

```
cmake -S cmake -B build
cmake --build build --config Release
```

The executable is written to `build/bin/Release/ASWUnitTests.exe` with multi-configuration generators. The RAD Studio
project writes its final executable to the same `build/bin/<Config>` directory. Debug builds pause for input after the
tests; Release builds run the tests and exit.

The executable uses semantic versioning for its `--version` output. Update `src/ASWUnitTests_Version.h` when preparing a
release. The CMake project does not need a separate version declaration because it currently builds the test executable
directly rather than packaging or installing it.

## Registering Tests

For registering a test group/module, see: TTestHandler::RegisterTestGroups() in `src\ASWUnitTests_Handler.cpp`. This
is the only unit in `src` that needs to be modified when adding a new test module. For example:

```
// Whatever includes at the top of the file for the test modules

void TTestHandler::RegisterTestGroups()
{
    // Example of how to add a module:
    // m_TestGroups.push_back(std::unique_ptr<TestClassName>(new TestClassName()));

    // ----- Add each class to be tested

    m_TestGroups.push_back(std::unique_ptr<TTest_TMyClassToTest>(new TTest_TMyClassToTest()));
    m_TestGroups.push_back(std::unique_ptr<TTest_TMyClassToTest2>(new TTest_TMyClassToTest2()));

    // ----- End adding classes to be tested
}
```

Test modules themselves inherit from `TTestGroupBase` and each method that needs to be tested for a module must
be explicitely registered within that module's constructor. For example:

```
// Whatever include at the top of the file for your `TMyClassToTest` class, etc.

//---------------------------------------------------------------------------
TTest_TMyClassToTest::TTest_TMyClassToTest()
    : inherited("Name_of_My_Module_Unit_For_TMyClassToTest")
{
    // Can register test methods this way
    RegisterTest([this]()
        {
            Test_NameOfMethodBeingTested_WhatIsBeingTested();
        }, "NameOfMethodBeingTested_WhatIsBeingTested");

    // Or you can register test methods this way
    RegisterTest(&TTest_TMyClassToTest::Test_NameOfMethodBeingTested_WhatIsBeingTested,
        "NameOfMethodBeingTested_WhatIsBeingTested");
    RegisterTest(&TTest_TMyClassToTest::Test_NameOfMethod2BeingTested_WhatIsBeingTested,
        "NameOfMethod2BeingTested_WhatIsBeingTested");
}
//---------------------------------------------------------------------------
TTest_TMyClassToTest::~TTest_TMyClassToTest()
{
}
//---------------------------------------------------------------------------
void TTest_TMyClassToTest::SetUp_Group()
{
    // Any setup code to execute before running the group
}
//---------------------------------------------------------------------------
void TTest_TMyClassToTest::SetUp_Test(ITestCase& /*testCase*/)
{
//    Log("Setting up test: " + testCase.GetName());
    // Any setup code to execute before running a test
}
//---------------------------------------------------------------------------
void TTest_TMyClassToTest::TearDown_Group()
{
    // Any clean up code to run after the group test is done
}
//---------------------------------------------------------------------------
void TTest_TMyClassToTest::TearDown_Test(ITestCase& /*testCase*/)
{
//    Log("Tearing down test: " + testCase.GetName());
    // Any clean up code to run after a test
}
//---------------------------------------------------------------------------
```

# Coding Standards

To use uncrustify (for coding standards (pretty formatting) for this repo):

1. Get `uncrustify` and ensure that the path to `uncrustify.exe` is added to the Windows `Path` environment variable.
    https://github.com/uncrustify/uncrustify

2. Run the following command locally to set the git hooks directory (uncrustify will run upon commit):
```
git config --local core.hooksPath .githooks/
```
