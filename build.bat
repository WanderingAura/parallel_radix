@echo off
setlocal

if /I "%~1"=="debug" (
    set CL_FLAGS=/std:c++20 /EHsc /Zi /Od /MDd
    set LINK_FLAGS=/DEBUG
) else if /I "%~1"=="release" (
    set CL_FLAGS=/std:c++20 /EHsc /O2 /DNDEBUG /MD
    set LINK_FLAGS=/RELEASE
) else (
    echo Usage: build.bat [release^|debug]
    exit /b 1
)

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo cl.exe not found on PATH. Run this from a "Developer Command Prompt for VS" ^(or after vcvarsall.bat^).
    exit /b 1
)

cl.exe %CL_FLAGS% parallel_radix_main.cpp /Fe:parallel_radix_main.exe /link %LINK_FLAGS%

endlocal
