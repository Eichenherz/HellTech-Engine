@echo off
setlocal enabledelayedexpansion

echo Fetching latest miniz release info...

:: Write PowerShell script to temp file to avoid cmd escaping issues with pipes
set "PS_SCRIPT=%TEMP%\get_miniz.ps1"

(
echo $release = Invoke-RestMethod -Uri "https://api.github.com/repos/richgel999/miniz/releases/latest"
echo $asset = $release.assets ^| Where-Object { $_.name -match "^miniz-[\d\.]+\.zip$" } ^| Select-Object -First 1
echo if ^($asset^) { Write-Output $asset.browser_download_url } else { Write-Output "NOTFOUND" }
) > "%PS_SCRIPT%"

for /f "delims=" %%i in ('powershell -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%"') do (
    set "DOWNLOAD_URL=%%i"
)

del "%PS_SCRIPT%" 2>nul

if "%DOWNLOAD_URL%"=="NOTFOUND" (
    echo ERROR: Could not find a miniz-^<version^>.zip asset in the latest release.
    exit /b 1
)

if "%DOWNLOAD_URL%"=="" (
    echo ERROR: Failed to fetch release info from GitHub.
    exit /b 1
)

for %%F in ("%DOWNLOAD_URL%") do set "ZIP_NAME=%%~nxF"

echo Latest release: %ZIP_NAME%
echo Downloading from: %DOWNLOAD_URL%

powershell -NoProfile -Command "Invoke-WebRequest -Uri '%DOWNLOAD_URL%' -OutFile '%ZIP_NAME%'"

if not exist "%ZIP_NAME%" (
    echo ERROR: Download failed.
    exit /b 1
)

echo Download complete.

if not exist "3rdParty\miniz" mkdir "3rdParty\miniz"

echo Extracting to 3rdParty\miniz...
powershell -NoProfile -Command "Expand-Archive -Path '%ZIP_NAME%' -DestinationPath '3rdParty\miniz' -Force"

if errorlevel 1 (
    echo ERROR: Extraction failed.
    exit /b 1
)

del "%ZIP_NAME%"

echo Done! miniz extracted to 3rdParty\miniz
endlocal