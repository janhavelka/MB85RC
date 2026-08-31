@echo off
setlocal

set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

if not exist "%PIO_EXE%" (
    >&2 echo PlatformIO Core was not found at: "%PIO_EXE%".
    >&2 echo Install the PlatformIO IDE extension for VS Code, or run pio directly.
    exit /b 1
)

"%PIO_EXE%" %*
exit /b %ERRORLEVEL%
