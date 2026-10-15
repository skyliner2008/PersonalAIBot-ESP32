@echo off
rem ESP32-AI Studio - PC companion app (settings, tools, debug log, screen mirror, firmware)
cd /d "%~dp0"
python -c "import serial" 2>nul || (
    echo Installing pyserial...
    python -m pip install -r requirements.txt || goto :error
)
start "" pythonw esp32_ai_studio.py
exit /b 0

:error
echo.
echo Could not install pyserial. Install Python 3.9+ from python.org, then run:
echo     python -m pip install pyserial
pause
exit /b 1
