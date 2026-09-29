# 🚗 Accident Detection & Emergency Dispatch Robotic Car

An IoT-powered autonomous safety and accident detection robotic vehicle built on the **ESP32** microcontroller. The system combines real-time sensor fusion (MPU6050 IMU + dual ultrasonic obstacle radars) with an onboard **Black Box** incident recorder, automated **Twilio emergency dispatch** (SMS with Google Maps location and automated voice calls), and a modular, responsive **HTML5/WebSocket telemetry dashboard**.

---

## 📌 Table of Contents
- [System Architecture](#-system-architecture)
- [Key Features](#-key-features)
- [Hardware Requirements & Pinout](#-hardware-requirements--pinout)
- [Software Prerequisites & Libraries](#-software-prerequisites--libraries)
- [Repository Structure](#-repository-structure)
- [Configuration & Setup Guide](#-configuration--setup-guide)
  - [1. Mosquitto Broker Configuration](#1-mosquitto-broker-configuration)
  - [2. ESP32 Firmware Configuration](#2-esp32-firmware-configuration)
  - [3. Running the Server & WebApp](#3-running-the-server--webapp)
- [Web Dashboard Guide](#-web-dashboard-guide)
- [Emergency Dispatch](#-emergency-dispatch)
- [Troubleshooting](#-troubleshooting)

---

## 🏗 System Architecture

```text
                 +-----------------------------------------+
                 |          ESP32 Robotic Vehicle          |
                 |  - MPU6050 (Collision G-Force Detection)|
                 |  - Dual Ultrasonic Radars (Front/Back)  |
                 |  - L298N Motor Driver + PD Controller   |
                 |  - Black Box Pre/Crash/Post Buffer      |
                 +--------------------+--------------------+
                                      |
                     Wi-Fi / TCP      |      HTTPS / REST
                    (Port 1883)       |     (Twilio Cloud)
                                      v            |
            +--------------------------------+     |
            |     Local Mosquitto Broker     |     v
            |   (Host PC: 192.168.0.x:1883)  |  [Emergency SMS + Voice Call]
            +---------------+----------------+  [Live Google Maps Link]
                            |
                 WebSockets | (Port 9001)
                            v
       +--------------------------------------------+
       |   Responsive Web Dashboard (CAR.html)      |
       |  - Real-Time D-Pad Car Remote Control      |
       |  - 500 Hz Sensor Visualizer (Chart.js)     |
       |  - Black Box Incident & Hunter ML Reports  |
       |  - GPS Geolocation Sync over MQTT          |
       +--------------------------------------------+

```

---

## ⚡ Key Features

* **Multi-Sensor Collision Evaluation**: Computes net deceleration vectors ($G$-force) along the vehicle's direction of travel via MPU6050 6-DOF IMU coupled with distance derivatives from front and rear ultrasonic sensors.
* **Onboard Black Box Flight Data Recorder**: Continuously retains a rolling 13-point snapshot buffer ($5$ pre-crash points, $3$ impact points, and $5$ post-crash aftermath points) tagged with IST timestamps, G-forces, radar distances, and motor actuation states.
* **10-Second Abortable Grace Period**: Visual and audible countdown upon impact detection. Operators can abort telecom dispatch using physical controls, the dashboard button, or the spacebar key if the event was a non-emergency.
* **Responsive HTML5 Telemetry Dashboard**:
* **PC Mode**: Full multi-column view with charts, telemetry panels, accident logs, serial terminal, and ML sandbox visible simultaneously.
* **Mobile Mode**: Clean, driving-centric interface with collapsible tuning drawer and slide-out menu to toggle heavy modules on demand.
* **Live Connection Indicators**: 💻 Server connection, 🚗 ESP32 telemetry watchdog, and 🌍 Background GPS tracking status.


* **Hunter ML Sandbox**: Adaptive calibration mode allowing empirical testing of impact thresholds with visual plotted curves and PDF report exports.

---

## 🔌 Hardware Requirements & Pinout

### Required Components

* **ESP32 DevKit V1** (30-pin)
* **MPU6050** Accelerometer & Gyroscope module
* **2x HC-SR04** Ultrasonic Distance Sensors
* **L298N Dual H-Bridge Motor Driver**
* **4WD Robot Chassis** with DC Gear Motors
* **Active Buzzer**
* **External Power Supply**

### Pin Configuration Table

| Component | Pin Function | ESP32 GPIO |
| --- | --- | --- |
| **MPU6050** | SDA | `GPIO 21` |
| **MPU6050** | SCL | `GPIO 22` |
| **Ultrasonic Sensors** | Shared Trigger (`trigPin`) | `GPIO 19` |
| **Front Ultrasonic** | Echo (`echoFront`) | `GPIO 34` (Input only) |
| **Rear Ultrasonic** | Echo (`echoBack`) | `GPIO 35` (Input only) |
| **Active Buzzer** | Positive Signal | `GPIO 18` |
| **L298N Motor Driver** | Left Motor PWM (`PIN_ENA`) | `GPIO 14` |
| **L298N Motor Driver** | Left Motor Dir 1 (`PIN_IN1`) | `GPIO 27` |
| **L298N Motor Driver** | Left Motor Dir 2 (`PIN_IN2`) | `GPIO 26` |
| **L298N Motor Driver** | Right Motor Dir 1 (`PIN_IN3`) | `GPIO 25` |
| **L298N Motor Driver** | Right Motor Dir 2 (`PIN_IN4`) | `GPIO 33` |
| **L298N Motor Driver** | Right Motor PWM (`PIN_ENB`) | `GPIO 32` |

> **Note:** Ensure all grounds (ESP32 GND, L298N GND, and Battery GND) are tied together.

---

## 📦 Software Prerequisites & Libraries

### 1. Arduino IDE Setup

Install the **ESP32 Board Package** via the Arduino Boards Manager:

* URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

Install the following libraries via **Sketch > Include Library > Manage Libraries**:

* **PubSubClient** by Nick O'Leary
* **ArduinoJson** by Benoît Blanchon (v6.x or v7.x)
* **Adafruit MPU6050** by Adafruit
* **Adafruit Unified Sensor** by Adafruit

*(The following required libraries are bundled with the ESP32 core: `WiFi.h`, `HTTPClient.h`, `Wire.h`, `Preferences.h`, `time.h`)*

### 2. PC / Host Machine Requirements

* **Python 3.x** (Used for serving the web app via standard `http.server`)
* **Eclipse Mosquitto MQTT Broker**: [Download Mosquitto](https://mosquitto.org/download/)

### 3. Frontend Web Dependencies (Loaded via CDN in `CAR.html`)

* `mqtt.min.js` (v4/v5 - MQTT client over WebSockets)
* `Chart.js` (Real-time telemetry and incident plots)
* `jspdf.umd.min.js` (Client-side PDF report generation)

---

## 📁 Repository Structure

```text
├── Car/
│   └── car_accident_final.ino     # ESP32 C++ firmware
├── WEB-APP/
│   ├── Accident Detection Car.html
│   ├── CAR.conf                   # Mosquitto broker dual-listener configuration
│   ├── CAR.html                   # Responsive telemetry & control dashboard
│   ├── Start_Demo.bat             # 1-click startup automation for broker & HTTP server
└── README.md

```

---

## 🚀 Configuration & Setup Guide

### 1. Mosquitto Broker Configuration

Ensure your `CAR.conf` file (located in the `WEB-APP/` folder) contains the following configuration for dual-listener support:

```text
# Standard MQTT TCP port for ESP32
listener 1883 0.0.0.0
allow_anonymous true

# WebSocket port for Browser Dashboard
listener 9001 0.0.0.0
protocol websockets

```

### 2. ESP32 Firmware Configuration

Open `car_accident_final.ino` (located in the `Car/` folder) in Arduino IDE. Update the network and broker definitions to match your local setup:

```cpp
// --- NETWORK & BROKER ---
const char* ssid        = "YOUR_WIFI_SSID";
const char* password    = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "192.168.0.xxx"; // Static IPv4 of your Host PC

```

Select your ESP32 board, choose the correct COM port, and upload the sketch.

### 3. Running the Server & WebApp

#### Option A: 1-Click Launch (Windows Batch File)

Navigate to the `WEB-APP/` folder and double-click `Start_Demo.bat`. This script automatically boots both the Mosquitto broker (using `CAR.conf`) and the Python local web server.

#### Option B: Manual Startup (Command Line / Linux / macOS)

1. **Start Mosquitto Broker** (from inside the `WEB-APP/` folder):
```bash
mosquitto -c CAR.conf -v

```


2. **Start Web Server** (from inside the `WEB-APP/` folder):
```bash
python -m http.server 8000

```


3. **Open the Dashboard**:
* On PC: Open `http://localhost:8000/CAR.html`
* On Mobile: Connect to the same Wi-Fi and open `http://<YOUR_PC_IP>:8000/CAR.html`



---

## 📱 Web Dashboard Guide

### Keyboard & Touchpad Controls (PC)

* **`W`** / **`A`** / **`S`** / **`D`**: Forward / Left / Reverse / Right
* **`Spacebar`**: Instant Stop / Emergency Alarm Reset
* **`Enter`**: Arm / Disarm Hunter Training Mode (or Accept captured threshold)
* **`Esc`**: Discard captured Hunter target

### Mobile Controls

* Onboard touch-friendly D-Pad with multi-touch cancellation.
* **Hamburger Menu (☰)**: Toggle visibility of live charts, crash logs, and the Hunter sandbox to maintain low rendering overhead.
* **Collapsible Drawer**: Tap **⚙️ System Config & GPS** under the driving panel to adjust speeds, PD tuning, or toggle Twilio dispatch without cluttering the screen.

---

## 📞 Emergency Dispatch

When a collision is confirmed, the vehicle halts all drive motors and enters an alarm state. A 10-second countdown begins on both the hardware buzzer and the web dashboard. If the operator does not cancel the alarm within the grace period, the ESP32 directly invokes the Twilio REST API via HTTPS to automatically dispatch:

* An **SMS Alert** containing the incident timestamp and a live Google Maps tracking link synchronized from the dashboard.
* An automated **Voice Call** to designated emergency contacts utilizing Twilio's TwiML speech synthesis.

---

## 🛠 Troubleshooting

* **Dashboard shows Green 💻, but Red 🚗**:
* Verify your ESP32 code has the exact static IP address of your laptop.
* Ensure the laptop network profile is set to **Private** in Windows Settings.
* Verify Windows Firewall allows inbound traffic on TCP **Port 1883**.
* Check the ESP32 Serial Monitor (`115200 baud`) to confirm Wi-Fi association and NTP time synchronization.


* **Ultrasonic Distance reads constant `100 cm` or `0 cm**`:
* Ensure `echoFront` (GPIO 34) and `echoBack` (GPIO 35) are wired without voltage divider issues.
* Click the **🔄 RESTART** button in the dashboard radar panel to execute a soft reset on the sensor pins.



```

```
