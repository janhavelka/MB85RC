@echo off
setlocal

set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

if not exist "%PIO_EXE%" (
    >&2 echo PlatformIO Core was not found at: "%PIO_EXE%".
    >&2 echo Install or repair the PlatformIO IDE extension for VS Code, then rerun this wrapper.
    exit /b 1
)

"%PIO_EXE%" %*
exit /b %ERRORLEVEL%
