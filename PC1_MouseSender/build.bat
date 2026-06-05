@echo off
REM PWNZ VISION PRO - Build Script for PC1 Mouse Sender
REM Requires: Visual Studio Developer Command Prompt

echo ========================================
echo PWNZ VISION PRO (UDP ULTIMATE) Builder
echo ========================================
echo.

REM Check if we're in Developer Command Prompt
where cl >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: Visual Studio compiler not found!
    echo Please run this script from "Developer Command Prompt for VS"
    echo.
    pause
    exit /b 1
)

echo [1/3] Checking source files...
if not exist main.cpp (
    echo ERROR: main.cpp not found!
    pause
    exit /b 1
)
echo OK: Source files found
echo.

echo [2/3] Compiling...
REM Adjust paths to ImGui implementation files as needed
REM If you have ImGui in a different location, update the paths below

REM Option A: If ImGui files are in the same directory
if exist imgui_impl_dx11.cpp (
    cl /EHsc /O2 /W4 ^
        main.cpp ^
        imgui_impl_dx11.cpp ^
        imgui_impl_win32.cpp ^
        d3d11.lib user32.lib ws2_32.lib ^
        /Fe:PWNZ_Vision_Pro.exe ^
        /link /SUBSYSTEM:WINDOWS
) else (
    REM Option B: If you need to provide full paths to ImGui
    echo WARNING: ImGui implementation files not found in current directory.
    echo Please ensure imgui_impl_dx11.cpp and imgui_impl_win32.cpp are available.
    echo.
    echo You can compile manually with:
    echo cl /EHsc main.cpp ^<path_to_imgui^>\imgui_impl_dx11.cpp ^<path_to_imgui^>\imgui_impl_win32.cpp d3d11.lib user32.lib ws2_32.lib /Fe:PWNZ_Vision_Pro.exe
    echo.
    
    REM Try to compile anyway (will fail if ImGui is not in include path)
    cl /EHsc /O2 /W4 ^
        main.cpp ^
        d3d11.lib user32.lib ws2_32.lib ^
        /Fe:PWNZ_Vision_Pro.exe ^
        /link /SUBSYSTEM:WINDOWS
)

echo.
echo [3/3] Checking output...
if exist PWNZ_Vision_Pro.exe (
    echo SUCCESS: PWNZ_Vision_Pro.exe created!
    echo.
    echo File size:
    dir PWNZ_Vision_Pro.exe | find "PWNZ_Vision_Pro.exe"
    echo.
    echo You can now run the application.
) else (
    echo ERROR: Compilation failed!
    echo Please check the error messages above.
)

echo.
echo ========================================
pause
