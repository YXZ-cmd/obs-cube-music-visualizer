@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake was not found. Install CMake and enable "Add CMake to PATH".
  pause
  exit /b 1
)

echo Configuring Cube Music Visualizer for Windows x64...
cmake --preset windows-x64
if errorlevel 1 goto :failed

echo Building plugin...
cmake --build --preset windows-x64 --config RelWithDebInfo
if errorlevel 1 goto :failed

echo Creating installable folder...
cmake --install build_x64 --prefix release\RelWithDebInfo --config RelWithDebInfo
if errorlevel 1 goto :failed

echo.
echo Build finished. Open release\RelWithDebInfo
pause
exit /b 0

:failed
echo.
echo Build failed. Keep this window open and copy the error message.
pause
exit /b 1
