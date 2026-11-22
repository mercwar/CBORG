@echo off
setlocal
cd /d "%~dp0"

where cl >nul 2>&1
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "%VSWHERE%" (
        echo [ERROR] vswhere.exe not found.
        exit /b 1
    )
    set "VSINSTALL="
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VSINSTALL=%%i"
    )
    if not defined VSINSTALL (
        echo [ERROR] MSVC x64 tools not found.
        exit /b 1
    )
    call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
)

taskkill /F /IM cborg.exe /T >nul 2>&1

cl /nologo /W4 /O2 /TC /I. /Iscintilla\include /Ilexilla\include cborg_dll.c ^
    /link /DLL /DEF:cborg.def user32.lib gdi32.lib /OUT:cborg.dll /IMPLIB:cborg.lib

if errorlevel 1 (
    echo [ERROR] Failed building cborg.dll
    pause
    exit /b 1
)


echo [OK] Built cborg.dll and cborg.lib using cborg.def
endlocal
pause
