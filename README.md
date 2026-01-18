# **WiGhost**
An educational, experiential system designed to help people understand how Wi-Fi actually works in the real world, including interference, contention, adaptation, and human impact by turning Wi-Fi into something visible, physical, and interactive.


**Prerequisites:**
1. Minumum x2 ESP32's WITH inbuilt WiFi (for secondary nodes)
2. x1 Powerful ESP32 (eg S3 WROOM for primary node and server)
3. Computer with Arduino IDE setup for ESP 32's

**Arduino Libraries required:**
1. ArduinoJson


**To run:**
1. After importing the code, make sure to select your correct ESP 32 model, and the correct port it is connected to.
   In our case, the Master ESP32 was connected to the serial port 12. 
   <img width="407" height="154" alt="image" src="https://github.com/user-attachments/assets/b198e36e-bc8e-4271-a34d-b26c699ed5da" />
2. Update Wi-Fi settings to your SSID and pass
3. Upload and compile all the code to the ESP 32's
4. Wait for a while
5. Open the URL that is shown in the Serial Monitor
