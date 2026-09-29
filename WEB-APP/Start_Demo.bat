@echo off
echo Starting Torque Beast Local Network...
cd "C:\Users\arnab\OneDrive\Desktop\Accident Detection Car"

:: Launch Mosquitto in a separate window
start "" "C:\Program Files\mosquitto\mosquitto.exe" -v -c CAR.conf

:: Launch Python Server in this window
python -m http.server 8000