@echo off
REM Set DXC to your custom executable
set "DXC=D:\EichenRepos\QiY\packages\Microsoft.Direct3D.DXC.1.8.2505.32\build\native\bin\x64\dxc.exe"

REM Run your Python script, forwarding all output to the terminal
python "dxc_compile_shaders.py"

REM Exit immediately when Python finishes
exit /b %errorlevel%