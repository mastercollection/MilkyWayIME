@echo off
setlocal

set "ROOT=%~dp0.."

py -3 "%ROOT%\tools\generate-static-hanja.py" %*
if not errorlevel 1 exit /b 0

python "%ROOT%\tools\generate-static-hanja.py" %*
exit /b %ERRORLEVEL%
