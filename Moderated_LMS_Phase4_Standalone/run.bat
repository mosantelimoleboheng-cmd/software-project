@echo off
call build.bat
if errorlevel 1 exit /b 1
if exist build\Release\ModeratedLMS.exe (build\Release\ModeratedLMS.exe) else (build\ModeratedLMS.exe)
