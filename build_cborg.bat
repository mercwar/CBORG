@echo off
setlocal
cd /d "%~dp0"

where cl >nul 2>&1
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VSINSTALL=%%i"
    )
    if defined VSINSTALL call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
)

if not exist "cborg.lib" (
    echo [ERROR] cborg.lib not found. Build cborg.dll first.
    pause
    exit /b 1
)

taskkill /F /IM cborg.exe /T >nul 2>&1

if exist "cborg.rc" (
    rc /nologo /fo cborg.res cborg.rc
    if errorlevel 1 (
        echo [ERROR] Failed to compile cborg.rc
        pause
        exit /b 1
    )
    cl /nologo /W4 /O2 /TC /I. /Iscintilla\include cborg.c cborg_tree.c cborg_symtree.c ^
        /link cborg.res user32.lib gdi32.lib comdlg32.lib comctl32.lib shell32.lib cborg.lib /SUBSYSTEM:WINDOWS /OUT:cborg.exe
) else (
    cl /nologo /W4 /O2 /TC /I. /Iscintilla\include cborg.c cborg_tree.c cborg_symtree.c ^
        /link user32.lib gdi32.lib comdlg32.lib comctl32.lib shell32.lib cborg.lib /SUBSYSTEM:WINDOWS /OUT:cborg.exe
)

if errorlevel 1 (
    echo [ERROR] Failed to compile cborg.exe
    pause
    exit /b 1
)

del /q cborg.obj cborg_tree.obj cborg_symtree.obj cborg.res >nul 2>&1
echo [OK] Built cborg.exe
start "" cborg.exe
endlocal
