@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run_Character.ps1" -Character pretorius -Renderer ollama -OllamaModel gemma2:2b
pause
