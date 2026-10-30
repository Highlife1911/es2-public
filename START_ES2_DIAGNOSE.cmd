@echo off
setlocal DisableDelayedExpansion
rem The normal starter provides runtime selection and visible error handling.
call "%~dp0START_ES2.cmd" -CaptureCrash %*
exit /b %ERRORLEVEL%
