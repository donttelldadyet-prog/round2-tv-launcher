@echo off
rem Reopens the Round2 PKG window (the server must already be running in its black window).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Round2-PKG-Server.ps1" -WindowOnly
