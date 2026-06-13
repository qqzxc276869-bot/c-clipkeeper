@echo off
setlocal
cl /W4 /D_CRT_SECURE_NO_WARNINGS main.c clip_store.c clipboard_win.c /Fe:ClipKeeper.exe user32.lib gdi32.lib advapi32.lib shell32.lib
set "STATUS=%ERRORLEVEL%"
endlocal & exit /b %STATUS%
