# 🚗 Accident Detection & Emergency Dispatch Robotic Car

An IoT-powered autonomous safety and accident detection robotic vehicle built on the **ESP32** microcontroller. The system combines real-time sensor fusion (MPU6050 IMU + dual ultrasonic obstacle radars) with an onboard **Black Box** incident recorder, automated **Twilio telecom dispatch** (SMS with Google Maps location and automated voice calls), and a modular, responsive **HTML5/WebSocket telemetry dashboard**.

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
- [Twilio Emergency Dispatch Setup](#-twilio-emergency-dispatch-setup)
- [Troubleshooting](#-troubleshooting)

---

## 🏗 System Architecture
