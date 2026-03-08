# Observatory-Power-Safety-Monitor
A high-performance, fully ASCOM Alpaca-compliant Safety Monitor built on the ESP32 for astrophotography observatories. It bridges commercial hardware modules (Grid Power detection and Battery Voltage monitoring) with professional sequence automation software like N.I.N.A.

✓ ASCOM Conform Universal Verified
Passes strict Conform Universal validation with 0 Errors and 0 Issues. Fully compliant with 32-bit unsigned transaction IDs and edge-case testing.

🌟 Features

ASCOM Alpaca Native: No ASCOM platform or Windows COM drivers required. Communicates directly over Wi-Fi (Port 8080).
Multi-Threaded Async Web Server: High-performance architecture using ESPAsyncWebServer guarantees zero-latency Alpaca responses. N.I.N.A. will never drop the connection.
Grid Loss Grace Period: When wall power fails, a customizable countdown timer starts. The monitor only reports "Unsafe" to N.I.N.A. when the timer expires, preventing false alarms from brief power blips.
Battery Voltage Monitoring: Reads UPS / Battery voltage in real-time.
Hardware Low-Voltage Cutoff: Overrides the grace timer and instantly triggers an "Unsafe" condition if your battery drops below a defined safe threshold.
Real-Time Web Dashboard: View safety status, battery voltage, grid state, calibrate voltage offset, and adjust settings via a browser without recompiling code.

⚙️ Hardware & Wiring

This build uses inexpensive, pre-built breakout modules (commonly found on AliExpress or Amazon) to make wiring plug-and-play without soldering complex custom circuits.

1. Grid Power Detection (Digital)
   This circuit tells the ESP32 exactly when wall power is lost, initiating the grace period timer.

   <img width="863" height="673" alt="image" src="https://github.com/user-attachments/assets/f6a8b672-f580-4cd0-bcd1-6a0a4c32be37" />


Component: Multiple Output Voltage Conversion Module (Black board with DC Barrel Jack).

Grid Side (Input): Plug a standard 12V DC wall adapter (connected to your mains power) into the barrel jack.

ESP32 Side (Output): Connect a jumper wire from the module's 3.3V Output Pin directly to the ESP32's GPIO 14.
Grounding: Connect the module's GND to the ESP32's GND.

Note: The ESP32 code uses an internal pull-down resistor on GPIO 14. As long as the grid is up, 3.3V flows to the pin (HIGH). If grid power cuts out, the pin goes LOW.

2. Battery Monitoring (Analog)

   This uses a standard 0-25V divider module to step down the 12V/24V battery output so the ESP32's ADC can safely read it.
   
<img width="122" height="121" alt="image" src="https://github.com/user-attachments/assets/815f220b-3095-40b6-bda8-fda31315d773" />

Component: High Accuracy DC Voltage Sensor Module (Blue board with green terminal blocks).

Pecron/Battery Side: Connect your battery's 12V(+) line to the VOL terminal block. Connect the 12V(-) line to the GND terminal block.

ESP32 Side: Connect the SIG (Signal) pin to the ESP32's GPIO 19. Connect the module's GND pin to the ESP32's GND.

Note: The VCC pin on the 3-pin header side of the sensor module is left disconnected, as it is not needed to divide the voltage.

🧠 System Logic

The core logic of the Safety Monitor is determined by the isSystemSafe() function. N.I.N.A. queries this endpoint to decide whether to continue imaging or initiate an emergency observatory park/shutdown.

Grid is UP: System returns Safe (True). Timer is reset to 0.

Grid goes DOWN: The system remains Safe, but the Grace Period Timer starts counting down.

Grace Period Expires: System returns Unsafe (False).

Low Battery Cutoff (If Enabled): If the battery voltage drops below the configured threshold (e.g., 11.0V), the system immediately returns Unsafe (False), completely bypassing the Grace Period timer.

💻 Web Dashboard & Configuration

Navigate to the ESP32's IP Address in any web browser to access the Async Dashboard. The UI allows you to monitor telemetry and configure parameters on the fly, saving them permanently to the ESP32's Flash memory.

<img width="473" height="628" alt="image" src="https://github.com/user-attachments/assets/39ba650e-01b6-4299-91eb-e3eaee65a102" />



Voltage Calibration
Because inexpensive resistor modules and ESP32 ADCs are not perfectly linear, software calibration is provided directly in the web UI.

Measure your physical battery output with a trusted Multimeter (e.g., 12.40V).

Check the Web Dashboard reading (e.g., 12.15V).

Enter the difference (0.25) into the Offset (V) field and hit Save & Restart. The dashboard and Alpaca API will now report the perfectly calibrated voltage.

Router Tip: The ESP32 broadcasts its hostname as Observatory-UPS. Use your router's app (like TP-Link Deco) to assign it a Static/Reserved IP address. This ensures N.I.N.A. always knows where to find the Safety Monitor even after power cycles.

🚀 Installation & Setup

1. Install the Arduino IDE and configure it for the ESP32 board manager.

2. Install the following required libraries via the Library Manager or GitHub:
   
ESPAsyncWebServer (by me-no-dev / lacamera)
AsyncTCP (by me-no-dev)

4. Open observatory_power_monitor.ino.
   
5. Update your Wi-Fi credentials:
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";

6. Compile and upload to your ESP32.
   
7. In N.I.N.A., go to Equipment > Safety Monitor. Select Alpaca, and enter the ESP32's IP address and Port 8080.

🔧 ASCOM Alpaca Server Details

This project uses a custom, highly optimized Alpaca REST API built from the ground up for the ESP32 architecture.
Port: 8080
Device Type: SafetyMonitor
Compliance: Handles edge-case HTTP PUT body parsing and unsigned 32-bit transaction ID bit-wrapping (e.g., handling negative client IDs generated by validation tools without underflowing).
Resource Management: Actively utilizes Connection: close headers and disabled Wi-Fi sleep (WiFi.setSleep(false)) to prevent socket exhaustion and latency spikes over multi-day imaging sessions.



