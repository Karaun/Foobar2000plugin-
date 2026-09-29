@echo off
setlocal EnableExtensions
chcp 65001 >nul
cd /d "%~dp0.."

set "SDK_HEADER=sdk\foobar2000\SDK\foobar2000.h"
set "URL=https://www.foobar2000.org/downloads/SDK-2023-09-23.7z"
set "ARCHIVE=%CD%\SDK-2023-09-23.7z"
set "TOOLS=%CD%\tools"
set "PORTABLE7Z=%TOOLS%\7zr.exe"

if exist "%SDK_HEADER%" (
    echo [OK] foobar2000 SDK already exists.
    goto :retarget
)

if not exist "%ARCHIVE%" (
    echo [INFO] Downloading official foobar2000 SDK 2023-09-23...
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
      "$ProgressPreference='SilentlyContinue'; [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -UseBasicParsing -Uri '%URL%' -OutFile '%ARCHIVE%'"
    if errorlevel 1 (
        echo [ERROR] SDK download failed.
        echo Official SDK page:
        echo   https://www.foobar2000.org/SDK
        echo Expected archive:
        echo   %ARCHIVE%
        pause
        exit /b 1
    )
) else (
    echo [OK] Existing SDK archive found:
    echo      %ARCHIVE%
)

if exist "sdk" rmdir /s /q "sdk"
mkdir "sdk" >nul 2>&1

rem ------------------------------------------------------------
rem Method 1: installed 7-Zip
rem ------------------------------------------------------------
set "SEVENZIP="
if exist "%ProgramFiles%\7-Zip\7z.exe" set "SEVENZIP=%ProgramFiles%\7-Zip\7z.exe"
if exist "%ProgramFiles(x86)%\7-Zip\7z.exe" set "SEVENZIP=%ProgramFiles(x86)%\7-Zip\7z.exe"
if not defined SEVENZIP (
    for /f "delims=" %%I in ('where 7z.exe 2^>nul') do if not defined SEVENZIP set "SEVENZIP=%%I"
)

if defined SEVENZIP (
    echo [INFO] Extracting SDK with installed 7-Zip...
    "%SEVENZIP%" x -y "%ARCHIVE%" "-o%CD%\sdk" >nul
    if exist "%SDK_HEADER%" goto :retarget
    echo [WARN] Installed 7-Zip extraction did not produce the expected SDK layout.
)

rem ------------------------------------------------------------
rem Method 2: Windows built-in tar.exe (libarchive).
rem Modern Windows 10/11 normally includes it and it can often read .7z.
rem ------------------------------------------------------------
where tar.exe >nul 2>&1
if not errorlevel 1 (
    echo [INFO] Trying Windows built-in tar.exe...
    if exist "sdk" rmdir /s /q "sdk"
    mkdir "sdk" >nul 2>&1
    tar.exe -xf "%ARCHIVE%" -C "%CD%\sdk" >nul 2>&1
    if exist "%SDK_HEADER%" goto :retarget
    echo [WARN] tar.exe could not extract this .7z archive correctly.
)

rem ------------------------------------------------------------
rem Method 3: download official standalone 7zr.exe.
rem No installation and no administrator rights required.
rem ------------------------------------------------------------
if not exist "%TOOLS%" mkdir "%TOOLS%" >nul 2>&1

if not exist "%PORTABLE7Z%" (
    echo [INFO] Downloading official portable 7zr.exe...
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
      "$ProgressPreference='SilentlyContinue'; [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -UseBasicParsing -Uri 'https://www.7-zip.org/a/7zr.exe' -OutFile '%PORTABLE7Z%'"
    if errorlevel 1 (
        echo [ERROR] Could not download portable 7zr.exe.
        echo You can manually download 7zr.exe from:
        echo   https://www.7-zip.org/download.html
        echo and place it here:
        echo   %PORTABLE7Z%
        pause
        exit /b 2
    )
)

echo [INFO] Extracting SDK with portable 7zr.exe...
if exist "sdk" rmdir /s /q "sdk"
mkdir "sdk" >nul 2>&1
"%PORTABLE7Z%" x -y "%ARCHIVE%" "-o%CD%\sdk" >nul
if errorlevel 1 (
    echo [ERROR] 7zr.exe failed to extract the SDK archive.
    pause
    exit /b 3
)

if not exist "%SDK_HEADER%" (
    echo [ERROR] SDK extraction completed, but the expected file was not found:
    echo   %SDK_HEADER%
    echo.
    echo Please check whether the archive is complete:
    echo   %ARCHIVE%
    pause
    exit /b 4
)

:retarget
echo [INFO] Retargeting SDK projects to VS2019 / v142 / C++17...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Retarget_SDK_VS2019.ps1"
if errorlevel 1 (
    echo [ERROR] SDK retarget failed.
    pause
    exit /b 5
)

echo.
echo [OK] SDK is ready:
echo   %CD%\%SDK_HEADER%
exit /b 0
