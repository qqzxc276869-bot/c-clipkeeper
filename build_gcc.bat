@echo off
setlocal
gcc -Wall -Wextra -std=c11 main.c clip_store.c clipboard_win.c -o ClipKeeper.exe -mwindows -ladvapi32 -lshell32
set "STATUS=%ERRORLEVEL%"
endlocal & exit /b %STATUS%
