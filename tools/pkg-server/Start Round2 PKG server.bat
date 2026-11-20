@echo off
title Round2 PKG server
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Round2-PKG-Server.ps1"
echo.
echo The Round2 PKG server stopped. Press a key to close.
pause >nul
