@echo off

REM Temp script

call "%Rad370%\bin\rsvars.bat"

pushd "%~dp0"
call cmd /c msbuild /t:Build /p:Config=Debug;Platform=Win64x ASWLogExample.cbproj
popd
