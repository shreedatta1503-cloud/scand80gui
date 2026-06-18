@echo off
REM ===========================================================================
REM build_android.bat - Reproducible production Android APK build for QGroundControl
REM ---------------------------------------------------------------------------
REM Native, SIGNED, Release arm64-v8a APK from QGroundControl source on Windows 11.
REM Mirrors the project's official CI recipe (.github\workflows\android.yml).
REM
REM PREREQUISITES (install once, then this script does the rest):
REM   - Git for Windows            https://git-scm.com/download/win
REM   - Python 3.10+ (on PATH)     https://www.python.org/downloads/windows/
REM   - JDK 17 (sets JAVA_HOME)    e.g. Eclipse Temurin 17
REM   - 7-Zip or built-in tar      (for unzip; tar ships with Windows 10+)
REM
REM Everything else (Qt-for-Android, Android SDK/NDK/build-tools, Ninja via Qt,
REM aqtinstall) is provisioned automatically below.
REM
REM Usage (from a "Developer Command Prompt" or normal cmd with the prereqs):
REM   build_android.bat
REM   set QGC_ABIS=arm64-v8a;armeabi-v7a && build_android.bat
REM ===========================================================================
setlocal enabledelayedexpansion

REM ---- 0. Tunables (override via environment before calling) ----------------
if "%QGC_SRC%"=="" set "QGC_SRC=%~dp0qgroundcontrol"
if "%QGC_REPO%"=="" set "QGC_REPO=https://github.com/mavlink/qgroundcontrol.git"
if "%BUILD_DIR%"=="" set "BUILD_DIR=%QGC_SRC%\build-android"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Release"
if "%QGC_ABIS%"=="" set "QGC_ABIS=arm64-v8a"
if "%ANDROID_MIN_SDK%"=="" set "ANDROID_MIN_SDK=29"
if "%QGC_STABLE_BUILD%"=="" set "QGC_STABLE_BUILD=ON"
if "%TOOLS_ROOT%"=="" set "TOOLS_ROOT=C:\qgc-android"
if "%ANDROID_SDK_ROOT%"=="" set "ANDROID_SDK_ROOT=%TOOLS_ROOT%\android-sdk"
if "%QT_BASE%"=="" set "QT_BASE=%TOOLS_ROOT%\Qt"
for /f %%i in ('powershell -NoProfile -Command "[Environment]::ProcessorCount"') do set "JOBS=%%i"

REM Signing keystore (a self-contained release keystore is generated if absent)
if "%QT_ANDROID_KEYSTORE_PATH%"=="" set "QT_ANDROID_KEYSTORE_PATH=%~dp0android_release.keystore"
if "%QT_ANDROID_KEYSTORE_ALIAS%"=="" set "QT_ANDROID_KEYSTORE_ALIAS=QGCAndroidKeyStore"
if "%QT_ANDROID_KEYSTORE_STORE_PASS%"=="" set "QT_ANDROID_KEYSTORE_STORE_PASS=qgcandroid"
if "%QT_ANDROID_KEYSTORE_KEY_PASS%"=="" set "QT_ANDROID_KEYSTORE_KEY_PASS=%QT_ANDROID_KEYSTORE_STORE_PASS%"

echo [build] CPU cores: %JOBS%   ABIs: %QGC_ABIS%   minSdk: %ANDROID_MIN_SDK%

REM ---- 1. Source tree -------------------------------------------------------
if not exist "%QGC_SRC%\.git" (
  echo [build] Cloning QGroundControl ...
  git clone --recursive --depth 1 "%QGC_REPO%" "%QGC_SRC%" || goto :err
) else (
  git -C "%QGC_SRC%" submodule update --init --recursive || goto :err
)

REM ---- 2. Read version pins from build-config.json ---------------------------
set "CFG=%QGC_SRC%\.github\build-config.json"
for /f %%v in ('python -c "import json;print(json.load(open(r'%CFG%'))['qt_version'])"')          do set "QT_VERSION=%%v"
for /f %%v in ('python -c "import json;print(json.load(open(r'%CFG%'))['ndk_full_version'])"')     do set "NDK_FULL=%%v"
for /f %%v in ('python -c "import json;print(json.load(open(r'%CFG%'))['android_platform'])"')     do set "ANDROID_PLATFORM=%%v"
for /f %%v in ('python -c "import json;print(json.load(open(r'%CFG%'))['android_build_tools'])"')  do set "ANDROID_BUILD_TOOLS=%%v"
for /f %%v in ('python -c "import json;print(json.load(open(r'%CFG%'))['android_cmdline_tools'])"') do set "CMDLINE_TOOLS=%%v"
for /f "delims=" %%v in ('python -c "import json;print(json.load(open(r'%CFG%'))['qt_modules'])"')  do set "QT_MODULES=%%v"

set "HOST_QT_ROOT=%QT_BASE%\%QT_VERSION%\msvc2022_64"
set "TARGET_QT_ROOT=%QT_BASE%\%QT_VERSION%\android_arm64_v8a"
set "ANDROID_NDK_ROOT=%ANDROID_SDK_ROOT%\ndk\%NDK_FULL%"
echo [build] Qt %QT_VERSION% ^| NDK %NDK_FULL% ^| platform %ANDROID_PLATFORM% ^| build-tools %ANDROID_BUILD_TOOLS%

REM ---- 3. Python venv (jinja2 for QGC codegen) ------------------------------
if not exist "%QGC_SRC%\.venv\Scripts\python.exe" python -m venv "%QGC_SRC%\.venv" || goto :err
"%QGC_SRC%\.venv\Scripts\python.exe" -m pip install -q --upgrade pip jinja2 defusedxml aqtinstall || goto :err
set "AQT=%QGC_SRC%\.venv\Scripts\aqt.exe"

REM ---- 4. Android cmdline-tools + SDK packages ------------------------------
set "SDKMANAGER=%ANDROID_SDK_ROOT%\cmdline-tools\latest\bin\sdkmanager.bat"
if not exist "%SDKMANAGER%" (
  echo [build] Installing Android cmdline-tools %CMDLINE_TOOLS% ...
  mkdir "%ANDROID_SDK_ROOT%\cmdline-tools" 2>nul
  powershell -NoProfile -Command "Invoke-WebRequest -Uri 'https://dl.google.com/android/repository/commandlinetools-win-%CMDLINE_TOOLS%_latest.zip' -OutFile '%TEMP%\cmdtools.zip'" || goto :err
  powershell -NoProfile -Command "Expand-Archive -Force '%TEMP%\cmdtools.zip' '%TEMP%\cmdtools'" || goto :err
  move /Y "%TEMP%\cmdtools\cmdline-tools" "%ANDROID_SDK_ROOT%\cmdline-tools\latest" >nul || goto :err
)
set "ANDROID_HOME=%ANDROID_SDK_ROOT%"
echo y| call "%SDKMANAGER%" --licenses >nul 2>&1
call "%SDKMANAGER%" "platform-tools" "platforms;android-%ANDROID_PLATFORM%" "build-tools;%ANDROID_BUILD_TOOLS%" "ndk;%NDK_FULL%" || goto :err

REM ---- 5. Qt for Android + host Qt ------------------------------------------
if not exist "%HOST_QT_ROOT%"   "%AQT%" install-qt windows desktop %QT_VERSION% win64_msvc2022_64 --outputdir "%QT_BASE%" --modules %QT_MODULES% || goto :err
if not exist "%TARGET_QT_ROOT%" "%AQT%" install-qt windows android %QT_VERSION% android_arm64_v8a --outputdir "%QT_BASE%" --modules %QT_MODULES% || goto :err

REM ---- 6. Signing keystore --------------------------------------------------
if not exist "%QT_ANDROID_KEYSTORE_PATH%" (
  echo [build] Generating release keystore ...
  keytool -genkeypair -v -keystore "%QT_ANDROID_KEYSTORE_PATH%" -alias "%QT_ANDROID_KEYSTORE_ALIAS%" ^
    -keyalg RSA -keysize 2048 -validity 10000 ^
    -storepass "%QT_ANDROID_KEYSTORE_STORE_PASS%" -keypass "%QT_ANDROID_KEYSTORE_KEY_PASS%" ^
    -dname "CN=QGroundControl, O=QGC, C=US" || goto :err
)

REM ---- 7. Configure ---------------------------------------------------------
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
set "PATH=%HOST_QT_ROOT%\bin;%ANDROID_SDK_ROOT%\cmdline-tools\latest\bin;%ANDROID_SDK_ROOT%\platform-tools;%PATH%"
set "GRADLE_OPTS=-Dorg.gradle.daemon=false"

echo [build] Configuring ...
cmake -S "%QGC_SRC%" -B "%BUILD_DIR%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
  -DCMAKE_TOOLCHAIN_FILE="%TARGET_QT_ROOT%/lib/cmake/Qt6/qt.toolchain.cmake" ^
  -DCMAKE_PREFIX_PATH="%TARGET_QT_ROOT%" ^
  -DQT_HOST_PATH="%HOST_QT_ROOT%" ^
  -DQT_ANDROID_ABIS="%QGC_ABIS%" ^
  -DANDROID_SDK_ROOT="%ANDROID_SDK_ROOT%" ^
  -DANDROID_NDK="%ANDROID_NDK_ROOT%" ^
  -DANDROID_NDK_ROOT="%ANDROID_NDK_ROOT%" ^
  -DQT_ANDROID_SIGN_APK=ON ^
  -DQGC_STABLE_BUILD=%QGC_STABLE_BUILD% ^
  -DQGC_QT_ANDROID_MIN_SDK_VERSION=%ANDROID_MIN_SDK% ^
  -DPython3_EXECUTABLE="%QGC_SRC%\.venv\Scripts\python.exe" ^
  -DCMAKE_WARN_DEPRECATED=FALSE || goto :err

REM ---- 8. Build + package ----------------------------------------------------
echo [build] Building (%JOBS% parallel jobs) ...
cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel %JOBS% || goto :err
echo [build] Packaging signed APK ...
cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --target apk --parallel %JOBS% || goto :err

REM ---- 9. Collect output -----------------------------------------------------
if "%OUT_APK%"=="" set "OUT_APK=QGroundControl.apk"
set "OUT=%~dp0%OUT_APK%"
for /r "%BUILD_DIR%" %%f in (*.apk) do (
  echo %%f | findstr /i "release signed" >nul && copy /Y "%%f" "%OUT%" >nul
)
if not exist "%OUT%" for /r "%BUILD_DIR%" %%f in (*.apk) do copy /Y "%%f" "%OUT%" >nul

echo.
echo ==============================================================
echo  BUILD SUCCESS
echo    APK : %OUT%
echo    install: adb install -r "%OUT%"
echo ==============================================================
endlocal
exit /b 0

:err
echo [fail] Build failed with errorlevel %errorlevel%. See console output above.
endlocal
exit /b 1
