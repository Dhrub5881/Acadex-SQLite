

@echo off
title ACADEX - Terminal Note Management

set "PATH=D:\Mys2s\ucrt64\bin;%PATH%"

cd /d "D:\PBL\Acadex_SQL_New"

if not exist "acadex.exe" (
    echo ERROR: acadex.exe not found!
    pause
    exit /b 1
)

acadex.exe

echo.
echo Acadex has closed.
pause
