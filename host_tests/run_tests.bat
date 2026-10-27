@echo off
REM ============================================================
REM  Host unit tests for ESP32-AI (portable firmware modules)
REM  Requires: Visual Studio 2019+ Build Tools (cl.exe)
REM  ArduinoJson is taken from .pio\libdeps (run "pio run" once first)
REM ============================================================
setlocal
set ROOT=%~dp0..
set OUT=%~dp0build
set AJSON=%ROOT%\.pio\libdeps\esp32s3\ArduinoJson\src
if not exist "%AJSON%\ArduinoJson.h" set AJSON=%ROOT%\.pio\libdeps\esp32s3-ble\ArduinoJson\src

if not exist "%AJSON%\ArduinoJson.h" (
  echo ArduinoJson not found in .pio\libdeps - run "pio run" once first.
  exit /b 2
)

REM Load the MSVC environment if cl.exe is not on PATH yet.
REM (kept outside parenthesised blocks: "%ProgramFiles(x86)%" contains a ")")
where cl >nul 2>nul
if not errorlevel 1 goto :have_cl
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSPATH=%%i"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
:have_cl

if not exist "%OUT%" mkdir "%OUT%"

cl /nologo /std:c++17 /EHsc /utf-8 /O2 /W3 /D_CRT_SECURE_NO_WARNINGS ^
  /I "%ROOT%\src" /I "%AJSON%" ^
  "%~dp0test_main.cpp" ^
  "%~dp0test_assistant.cpp" ^
  "%~dp0test_vehicle.cpp" ^
  "%~dp0test_obd.cpp" ^
  "%~dp0test_phone.cpp" ^
  "%ROOT%\src\util\Base64.cpp" ^
  "%ROOT%\src\memory\TextFeatures.cpp" ^
  "%ROOT%\src\memory\MemoryStore.cpp" ^
  "%ROOT%\src\memory\EpisodicLog.cpp" ^
  "%ROOT%\src\agent\AgentTools.cpp" ^
  "%ROOT%\src\agent\Personas.cpp" ^
  "%ROOT%\src\agent\Scheduler.cpp" ^
  "%ROOT%\src\agent\TaskList.cpp" ^
  "%ROOT%\src\agent\Consolidation.cpp" ^
  "%ROOT%\src\agent\ThaiProvinces.cpp" ^
  "%ROOT%\src\ai\ModelFallback.cpp" ^
  "%ROOT%\src\vehicle\DashModel.cpp" ^
  "%ROOT%\src\vehicle\SimEngine.cpp" ^
  "%ROOT%\src\vehicle\Elm327.cpp" ^
  "%ROOT%\src\vehicle\HondaKline.cpp" ^
  "%ROOT%\src\vehicle\Obd.cpp" ^
  "%ROOT%\src\vehicle\VehicleHealth.cpp" ^
  "%ROOT%\src\vehicle\ObdLog.cpp" ^
  /Fo"%OUT%\\" /Fe"%OUT%\host_tests.exe"
if errorlevel 1 exit /b 1

"%OUT%\host_tests.exe"
exit /b %errorlevel%
