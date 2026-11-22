@echo off
setlocal
cd /d "%~dp0"

:: 1. Auto-initialize MSVC x64 environment
where cl >nul 2>&1
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "%VSWHERE%" (
        echo [ERROR] Visual Studio Installer not found.
        pause
        exit /b 1
    )
    set "VSINSTALL="
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VSINSTALL=%%i"
    )
    if not defined VSINSTALL (
        echo [ERROR] Visual Studio with VC++ x86/x64 tools not found.
        pause
        exit /b 1
    )
    call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
    if errorlevel 1 (
        echo [ERROR] Failed to initialize vcvars64.bat.
        pause
        exit /b 1
    )
)

:: 2. Ensure scintilla and lexilla folders exist
if not exist "scintilla\win32\scintilla.mak" (
    echo Downloading Scintilla 5.6.9...
    curl -fSL "https://www.scintilla.org/scintilla569.tgz" -o scintilla569.tgz
    if errorlevel 1 (
        echo [ERROR] Failed to download scintilla569.tgz from scintilla.org.
        pause
        exit /b 1
    )
    tar -xzf scintilla569.tgz
    del /q scintilla569.tgz >nul 2>&1
    if not exist "scintilla\win32\scintilla.mak" (
        echo [ERROR] Scintilla archive extraction failed.
        pause
        exit /b 1
    )
)

if not exist "lexilla\src\lexilla.mak" (
    echo Cloning lexilla...
    git clone https://github.com/ScintillaOrg/lexilla.git lexilla
    if errorlevel 1 (
        echo [ERROR] Failed to clone lexilla. Make sure git is installed and reachable.
        pause
        exit /b 1
    )
)

:: 3. Build Scintilla.dll
if not exist "scintilla\bin\Scintilla.dll" (
    echo Building Scintilla.dll...
    pushd scintilla\win32
    nmake -f scintilla.mak
    if errorlevel 1 (
        popd
        echo [ERROR] Scintilla build failed!
        pause
        exit /b 1
    )
    popd
)

:: 4. Build Lexilla.dll
if not exist "lexilla\bin\Lexilla.dll" (
    echo Building Lexilla.dll...
    pushd lexilla\src
    nmake -f lexilla.mak
    if errorlevel 1 (
        popd
        echo [ERROR] Lexilla build failed!
        pause
        exit /b 1
    )
    popd
)

echo [OK] Scintilla and Lexilla built successfully.
pause