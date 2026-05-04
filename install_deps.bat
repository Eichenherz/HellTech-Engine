@echo off
setlocal enabledelayedexpansion

if not exist "3rdParty" mkdir "3rdParty"

set FAILED=0

call :clone "3rdParty/unordered_dense"          "https://github.com/martinus/unordered_dense.git"
call :clone "3rdParty/dds"                      "https://github.com/turanszkij/dds.git"
call :clone "3rdParty/imgui"                    "https://github.com/ocornut/imgui.git"
call :clone "3rdParty/VulkanMemoryAllocator"    "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git"
call :clone "3rdParty/cgltf"                    "https://github.com/jkuhlmann/cgltf.git"
call :clone "3rdParty/meshoptimizer"            "https://github.com/zeux/meshoptimizer.git"
call :clone "3rdParty/MikkTSpace"               "https://github.com/mmikk/MikkTSpace.git"
call :clone "3rdParty/bc7enc_rdo"               "https://github.com/Eichenherz/bc7enc_rdo.git"
call :clone "3rdParty/ImGuiFileDialog"          "https://github.com/aiekick/ImGuiFileDialog.git"
call :clone "3rdParty/flux"                     "https://github.com/tcbrindle/flux.git"
call :clone "3rdParty/minunit"                  "https://github.com/kattkieru/minunit.git"
call :clone "3rdParty/OffsetAllocator"          "https://github.com/sebbbi/OffsetAllocator.git"

echo.
if %FAILED%==0 (
    echo All repositories cloned successfully.
) else (
    echo WARNING: %FAILED% repository/repositories failed to clone.
    exit /b 1
)

echo.
echo Fetching miniz...
call "%~dp0get_miniz.bat"
if errorlevel 1 (
    echo ERROR: get_miniz.bat failed.
    exit /b 1
)
exit /b 0

:clone
set "PATH_=%~1"
set "URL_=%~2"
echo.
echo [%PATH_%] %URL_%
if exist "%PATH_%" (
    echo   Already exists, skipping.
) else (
    git clone "%URL_%" "%PATH_%"
    if errorlevel 1 (
        echo   ERROR: Failed to clone %URL_%
        set /a FAILED+=1
    ) else (
        echo   OK
    )
)
exit /b 0