@echo off
setlocal

set "BUILD_DIR=build-vs2022"

echo [zBot] Configuring project...
cmake -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo [zBot] Configure failed.
    exit /b %errorlevel%
)

echo [zBot] Building Release...
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo [zBot] Build failed.
    exit /b %errorlevel%
)

echo [zBot] Build finished successfully.
exit /b 0
