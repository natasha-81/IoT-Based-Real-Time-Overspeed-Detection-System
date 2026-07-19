# IoT-Based-Real-Time-Overspeed-Detection-System

📌 Overview

This project implements a real-time embedded system for detecting overspeeding vehicles in a controlled environment (e.g., campus roads). The system uses sensor inputs and edge processing on an ESP32 to identify speed violations and trigger alerts.

🎯 Key Features

Real-time vehicle speed detection using ultrasonic sensors
GPS-based location tracking
Edge processing on ESP32 (no cloud dependency)
Speed display on LCD module
Camera monitoring via IP Webcam
Alert generation for overspeed events

⚙️ Tech Stack

Software:
  Embedded C++ (Arduino IDE)
  ESP32 Microcontroller Programming
Hardware:
  ESP32
  Ultrasonic Sensors
  GPS Module
  LCD Display (16x2)
  Power Supply (USB)

🔍 Working Principle

1. Distance Measurement
   Ultrasonic sensors measure distance of the vehicle over time
2. Speed Calculation
   ESP32 computes speed using distance/time
3. Threshold Detection
   If speed exceeds limit → violation detected
4. Action Triggered
   GPS location captured
   Alert generated
   Speed shown on LCD display
   Camera feed used for monitoring

📊 Performance (Approx.)

Detection Latency: ~1–2 seconds
Accuracy: ~93-96% (sensor-dependent)
Sensor Range: ~2–4 meters

🚧 Challenges Faced

GPS signal acquisition issues
Dynamic IP issue with IP Webcam
ESP32 library compatibility issues

🔮 Future Improvements

AI-based vehicle detection using camera
Mobile app for alerts
Cloud dashboard integration
More efficient power management

👩‍💻 Author

Natasha Singla
Electronics & Communication Engineering
Thapar Institute of Engineering and Technology

⭐ If you found this useful, consider starring the repo!
