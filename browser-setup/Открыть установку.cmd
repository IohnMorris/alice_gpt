@echo off
cd /d "%~dp0"
set "PYTHONUTF8=1"
"%~dp0..\AlisaVoice-Mini2-test\windows\runtime\python.exe" -B -X utf8 server.py
pause
