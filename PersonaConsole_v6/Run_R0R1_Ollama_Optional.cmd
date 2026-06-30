@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run_Character.ps1" -Character r0r1 -Renderer ollama -OllamaModel qwen3:8b
pause
