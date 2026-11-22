@echo off
setlocal
cd /d "%~dp0"

rem Current Linux server with player-to-player transfers and persistent DB.
set "SERVER_BIN="
if exist "build\transfer-odbc\GameServer" set "SERVER_BIN=./build/transfer-odbc/GameServer"
if not defined SERVER_BIN if exist "build\linux-release\GameServer" set "SERVER_BIN=./build/linux-release/GameServer"
if not defined SERVER_BIN if exist "build\linux-debug\GameServer" set "SERVER_BIN=./build/linux-debug/GameServer"
if not defined SERVER_BIN (
    echo [ERROR] No Linux DB server build found.
    echo Build with: cmake --preset linux-release
    echo             cmake --build --preset linux-release
    pause
    exit /b 1
)

if not defined HARVEN_WSL_DISTRO set "HARVEN_WSL_DISTRO=Ubuntu"
echo [HARVEN] Server: %SERVER_BIN%
echo [HARVEN] WSL: %HARVEN_WSL_DISTRO% / default port: 4000
echo [HARVEN] DB settings: harven.env or Linux environment variables.
echo [HARVEN] Stop with Ctrl+C.
wsl.exe -d %HARVEN_WSL_DISTRO% --exec bash ./Tools/Setup/run_server.sh %SERVER_BIN% --dev %*
set "SERVER_EXIT=%ERRORLEVEL%"
if "%SERVER_EXIT%"=="10" (
    pause
    exit /b 0
)
if not "%SERVER_EXIT%"=="0" (
    echo [ERROR] Server exited with code %SERVER_EXIT%. See the messages above.
    pause
)
exit /b %SERVER_EXIT%
