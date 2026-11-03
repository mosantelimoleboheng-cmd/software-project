@echo off
setlocal
where cmake >nul 2>nul
if errorlevel 1 (echo CMake was not found. Install CMake and add it to PATH.& exit /b 1)
echo Configuring Moderated LMS...
cmake -S . -B build
if errorlevel 1 exit /b 1
echo Building Moderated LMS...
cmake --build build --config Release
if errorlevel 1 exit /b 1
echo Build successful.
endlocal
