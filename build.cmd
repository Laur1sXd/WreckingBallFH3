@echo off
setlocal EnableDelayedExpansion
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq delims=" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "WB_VS_ROOT=%%I"
    )
    if not defined WB_VS_ROOT (
        echo Install Visual Studio or Build Tools with Desktop development with C++ and the Windows SDK.
        exit /b 1
    )
    call "!WB_VS_ROOT!\VC\Auxiliary\Build\vcvars64.bat" >nul
    if not "!errorlevel!"=="0" exit /b 1
)
cd /d "%~dp0"
if not exist build mkdir build
pushd build
rc /nologo /fo WreckingBall.res ..\src\WreckingBall.rc
if not "!errorlevel!"=="0" exit /b 1
set "MINHOOK=..\third_party\minhook"
cl /nologo /c /O2 /MT /W4 /I%MINHOOK%\include /I%MINHOOK%\src %MINHOOK%\src\buffer.c %MINHOOK%\src\hook.c %MINHOOK%\src\trampoline.c %MINHOOK%\src\hde\hde64.c
if not "!errorlevel!"=="0" exit /b 1
cl /nologo /c /O2 /MT /W4 /EHsc /std:c++17 /I..\src /I%MINHOOK%\include ..\src\wreckingball_fh3.cpp
if not "!errorlevel!"=="0" exit /b 1
link /nologo /DLL /OUT:WreckingBall.dll /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT /MACHINE:X64 wreckingball_fh3.obj WreckingBall.res buffer.obj hook.obj trampoline.obj hde64.obj kernel32.lib user32.lib
if not "!errorlevel!"=="0" exit /b 1
cl /nologo /c /O2 /MT /W4 /EHsc /std:c++17 /DWB_MOD_PLATFORM /I..\src /Fo:wreckingball_platform.obj ..\src\wreckingball_fh3.cpp
if not "!errorlevel!"=="0" exit /b 1
link /nologo /DLL /OUT:WreckingBallPlatform.dll /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT /MACHINE:X64 wreckingball_platform.obj WreckingBall.res kernel32.lib user32.lib
if not "!errorlevel!"=="0" exit /b 1
if "%~1"=="test" (
cl /nologo /O2 /MT /EHsc /std:c++17 /I..\src ..\tests\rules_test.cpp /Fe:rules_test.exe
if not "!errorlevel!"=="0" exit /b 1
rules_test.exe
if not "!errorlevel!"=="0" exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 /I..\src ..\tests\coverage_test.cpp /Fe:coverage_test.exe
if not "!errorlevel!"=="0" exit /b 1
coverage_test.exe
if not "!errorlevel!"=="0" exit /b 1
cl /nologo /O2 /MT /W4 /EHsc /std:c++17 /I..\src /I%MINHOOK%\include ..\tests\runtime_test.cpp buffer.obj hook.obj trampoline.obj hde64.obj kernel32.lib user32.lib /Fe:runtime_test.exe
if not "!errorlevel!"=="0" exit /b 1
runtime_test.exe
if not "!errorlevel!"=="0" exit /b 1
cl /nologo /O2 /MT /W4 /EHsc /std:c++17 ..\tests\platform_settings_test.cpp user32.lib /Fe:platform_settings_test.exe
if not "!errorlevel!"=="0" exit /b 1
platform_settings_test.exe
if not "!errorlevel!"=="0" exit /b 1
cl /nologo /O2 /MT /W4 /EHsc /std:c++17 ..\tests\platform_smoke.cpp /Fe:platform_smoke.exe
if not "!errorlevel!"=="0" exit /b 1
)
popd
