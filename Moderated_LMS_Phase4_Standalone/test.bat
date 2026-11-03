@echo off
call build.bat
if errorlevel 1 exit /b 1
ctest --test-dir build -C Release --output-on-failure
