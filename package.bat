@echo off
setlocal

REM Root directory = this script's directory
set "ROOT=%~dp0"
REM Remove trailing backslash
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

set "BUILD_DIR=%ROOT%\build"
set "EXE_NAME=LanMeeting.exe"
set "EXE_PATH=%BUILD_DIR%\%EXE_NAME%"

echo [INFO] Root:      %ROOT%
echo [INFO] Build dir: %BUILD_DIR%
echo [INFO] Exe path:  %EXE_PATH%
echo.

if not exist "%EXE_PATH%" (
    echo [ERROR] "%EXE_PATH%" not found.
    echo         Please make sure you have a Release build in the "build" folder.
    echo         You can adjust BUILD_DIR / EXE_NAME in package.bat if needed.
    exit /b 1
)

echo [1/4] Building project (cmake --build)...
cmake --build "%BUILD_DIR%"
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

set "DIST_DIR=%ROOT%\dist"
if exist "%DIST_DIR%" (
    echo [INFO] Removing old dist directory...
    rmdir /s /q "%DIST_DIR%"
)
mkdir "%DIST_DIR%"

echo [2/4] Copying executable to dist...
copy "%EXE_PATH%" "%DIST_DIR%\%EXE_NAME%" >nul
if errorlevel 1 (
    echo [ERROR] Failed to copy executable to "%DIST_DIR%".
    exit /b 1
)

REM Adjust these paths if your Qt is installed elsewhere
set "QT_ROOT=C:\Qt\6.8.0\mingw_64"
set "WINDEPLOYQT=%QT_ROOT%\bin\windeployqt.exe"

if not exist "%WINDEPLOYQT%" (
    echo [ERROR] windeployqt not found at:
    echo         %WINDEPLOYQT%
    echo         Please edit QT_ROOT in package.bat to match your Qt installation.
    exit /b 1
)

echo [3/4] Running windeployqt...
pushd "%DIST_DIR%"
"%WINDEPLOYQT%" "%EXE_NAME%"
if errorlevel 1 (
    echo [ERROR] windeployqt failed.
    popd
    exit /b 1
)
popd

echo [4/4] Copying MinGW and FFmpeg runtime DLLs...

REM MinGW runtime DLLs (adjust version/path if needed)
set "MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin"
for %%F in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    if exist "%MINGW_BIN%\%%F" (
        copy "%MINGW_BIN%\%%F" "%DIST_DIR%" >nul
    )
)

REM Optional: FFmpeg DLLs, if present (adjust FFMPEG_BIN if needed)
set "FFMPEG_BIN=D:\dev\ffmpeg\bin"
if exist "%FFMPEG_BIN%" (
    for %%F in (avcodec-61.dll avcodec-62.dll avformat-61.dll avformat-62.dll avutil-59.dll avutil-60.dll swresample-5.dll swresample-6.dll swscale-8.dll swscale-9.dll) do (
        if exist "%FFMPEG_BIN%\%%F" (
            copy "%FFMPEG_BIN%\%%F" "%DIST_DIR%" >nul
        )
    )
)

echo.
echo [DONE] Packaged application is in:
echo        "%DIST_DIR%"
echo        You can copy this whole folder to another Windows machine and run %EXE_NAME%.

endlocal
exit /b 0

