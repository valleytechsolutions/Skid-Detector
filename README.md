# Black Wire Militia Blue-Team Nightlight

A wireless LED mood light and passive network security monitor built on the Seeed Studio XIAO ESP32 platform. The device runs a local web server over its own WiFi access point, giving you full control over lighting effects from any phone or laptop. In the background it continuously watches for WiFi deauthentication attacks and Bluetooth advertisement spam, strobing the matrix red and blue when anything suspicious is detected and displaying an alert in the web interface.

This project was written for the Seeed Studio XIAO ESP32-C3 and XIAO ESP32-S3 paired with the official 6x10 RGB Matrix for XIAO. It is open source, free to use, and free to modify.


## Table of Contents

- Hardware requirements
- Software requirements
- Wiring and assembly
- Flashing the C3
- Flashing the S3
- Connecting to the device
- Web interface overview
- Lighting modes
- Speed and brightness controls
- Security monitoring and alerts
- Dismissing an alert
- How deauth detection works
- How Bluetooth spam detection works
- Known limitations
- Troubleshooting
- Project structure
- License and credits


## Hardware Requirements

- Seeed Studio [XIAO ESP32-C3](https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html) or [XIAO ESP32-S3](https://www.seeedstudio.com/XIAO-ESP32S3-p-5627.html)
- Seeed Studio [6x10 RGB Matrix for XIAO](https://www.seeedstudio.com/6x10-RGB-MATRIX-for-XIAO-p-5771.html) (60 WS2812B LEDs)
- USB-C cable with data lines (not a charge-only cable)
- A phone, tablet, or laptop with WiFi to use the web interface


## Software Requirements

- Arduino IDE 2.x (download from arduino.cc)
- ESP32 board support package by Espressif, version 2.0.11 or later
- Adafruit NeoPixel library by Adafruit (used on the S3 build)
- FastLED library by Daniel Garcia (used on the C3 build)

The board support package is installed through the Arduino IDE boards manager. The NeoPixel and FastLED libraries are installed through the Arduino IDE library manager.


## Wiring and Assembly

The 6x10 RGB Matrix for XIAO is designed to plug directly onto the XIAO header pins with no additional wiring. Line up the board so the USB-C port on the XIAO is accessible and press the two boards together firmly. The data line, power, and ground are all carried through the header.

If you want to use the matrix with an external power supply instead of USB, there are solder pads on the matrix board labeled VCC, GND, and DIN. DIN connects to A0 on the XIAO.

Do not run the matrix at full brightness for extended periods without adequate airflow. At maximum brightness all 60 LEDs draw roughly 3.6 amps at 5 volts. Keeping brightness at or below 80 out of 255 is recommended for USB-powered operation.


## Flashing the C3

Step 1. Open Arduino IDE and go to File, then Preferences. In the field labeled Additional boards manager URLs, add the following on its own line:

    https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

Step 2. Go to Tools, then Board, then Boards Manager. Search for esp32 and install the package titled "esp32 by Espressif Systems." Wait for installation to complete.

Step 3. Go to Tools, then Manage Libraries. Search for FastLED and install the library by Daniel Garcia.

Step 4. Under Tools, set the following options:

    Board: XIAO_ESP32C3
    USB CDC On Boot: Enabled
    Port: whichever COM port appears when the XIAO is plugged in

Step 5. Open the C3 sketch file. Click the upload button. If the upload fails with a port error, hold the BOOT button on the XIAO, tap RESET while holding BOOT, release BOOT after about one second, then click upload immediately.

Step 6. Open the Serial Monitor at 115200 baud. You should see the message "Ready" followed by the IP address 192.168.4.1. The matrix will begin cycling through colors.


## Flashing the S3

The S3 uses the Adafruit NeoPixel library instead of FastLED. This is intentional. The S3's RMT peripheral conflicts with the WiFi driver when FastLED is used, which causes the LEDs to produce no output once the access point starts. NeoPixel uses a different signaling approach that does not have this conflict.

Step 1. Follow steps 1 and 2 from the C3 instructions above to install the ESP32 board support package if you have not done so already.

Step 2. Go to Tools, then Manage Libraries. Search for Adafruit NeoPixel and install the library by Adafruit. Do not install FastLED for the S3 build.

Step 3. Under Tools, set the following options:

    Board: XIAO_ESP32S3
    USB CDC On Boot: Enabled
    Partition Scheme: Huge APP (3MB No OTA)
    Port: whichever COM port appears when the XIAO is plugged in

The partition scheme change is required because the sketch with the full web interface and BLE scanning code is large enough that it does not fit in the default partition layout.

Step 4. Open the S3 sketch file. Click the upload button.

Step 5. Open the Serial Monitor at 115200 baud. You should see "LEDs ready" followed by the access point IP address. The matrix will start in solid shift mode, cycling through the full color spectrum.

If you see the message "LEDs ready" but nothing lights up, the most common cause is a charge-only USB cable. Swap the cable and try again before changing any code.


## Connecting to the Device

Once the sketch is running, the XIAO broadcasts a WiFi network with the following credentials:

    Network name: xiaoNightlight or skiddetector
    Password: 12345678

Connect to this network from any device. Open a browser and navigate to 192.168.4.1. The web interface will load. No internet connection is required. The device does not connect to any external network.


## Web Interface Overview

The interface is divided into cards. Each card controls one aspect of the device. Changes take effect immediately without needing to refresh the page. The interface polls the device every two seconds for alert status and updates the display automatically.

The background of the interface displays a matrix rain animation in green and white characters. This is purely decorative and has no effect on device performance.

At the very bottom of the page is the footer: "i love your face" followed by a credit line to Your Pal Kal and the handle @valleytechsolutions.


## Lighting Modes

The device supports eight lighting modes. Only one mode runs at a time. Switching modes is instant.

Solid Color sets every LED to the same fixed color. Use the color picker to choose a color, then press the Apply Color button. The color is preserved until you change it or switch modes.

Wave cycles through the color spectrum with each LED slightly offset from its neighbor, creating a flowing rainbow that appears to move across the matrix.

Solid Shift sets every LED to the same hue at once and slowly rotates that hue through the full color spectrum. The entire matrix changes color together. This is the default mode at startup.

Chase sends a short trail of colored light sweeping around the matrix. The trail fades to black behind the leading pixel and the hue advances continuously, so the color of the trail changes over time.

Sparkle randomly lights individual pixels in different colors and lets them fade out. The overall hue drifts slowly, so the sparkle pattern gradually shifts in color temperature.

Meteor sweeps a comet-style trail around the matrix. The leading pixel is the brightest and the tail fades over eight pixels. The hue advances continuously.

Breathe fades the entire matrix in and out smoothly. Each time the brightness reaches zero and begins rising again, the hue advances by about an eighth of the spectrum, so each breath is a different color.

Smooth Color Cycle sets all LEDs to the same color and advances the hue in very small increments per tick. Unlike Solid Shift, the hue step is floating point and the speed slider has a strong effect on how fast the color changes. At the ambient setting this mode changes color almost imperceptibly slowly, which is useful for background lighting in a room.


## Speed and Brightness Controls

The speed slider controls how frequently the animation updates. The slider runs from left to right, where the leftmost position is ambient mode and the rightmost position is the fastest setting.

The labeled positions from left to right are: Ambient, Very Slow, Slow, Relaxed, Medium-Slow, Medium, Medium-Fast, Fast, Very Fast, Max, and then Ambient again at the far right. Both ends of the slider produce the ambient timing, which updates every 800 milliseconds. This creates an extremely smooth, almost imperceptible transition that works well as background mood lighting.

The brightness slider runs from 5 to 255. A value of 80 is the default and is appropriate for USB-powered operation. Higher values produce more heat and draw more current. Values above 150 are not recommended for extended use without external power.

Both sliders take effect immediately on release.


## Security Monitoring and Alerts

The device passively monitors two attack surfaces while the nightlight is running. No configuration is required. Monitoring begins automatically when the device boots and runs continuously in the background.

These features are intended for awareness and education in controlled or personal environments. The detection is based on statistical heuristics and is not a substitute for dedicated network security equipment.


## How Deauth Detection Works

The XIAO's WiFi radio is placed in promiscuous mode, which means it receives all management frames in the air regardless of whether they are addressed to the device. The firmware inspects each management frame and counts deauthentication frames, which are the type of frame used in WiFi jamming and forced disconnection attacks.

If five or more deauthentication frames arrive within a three-second window, the firmware raises the deauth alert. The matrix stops its current mode and begins strobing red and blue alternately at approximately five cycles per second. The web interface displays a large red alert box with the message "DEAUTH ATTACK DETECTED" and a description of what is happening.

The device hops between WiFi channels 1, 6, and 11 every three seconds. These are the three non-overlapping channels in the 2.4 GHz band and are the most common channels used by both legitimate networks and attackers. Channel hopping allows the device to detect attacks happening on channels other than the one the access point is broadcasting on.

The alert clears automatically when two consecutive three-second windows pass with fewer than five deauthentication frames. Once the attack stops, the alert box disappears and the matrix returns to its previous lighting mode. The dismissed state is also reset so the next attack will trigger a full alert again.


## How Bluetooth Spam Detection Works

The device runs a BLE scan every four seconds. During each scan it collects the MAC addresses of every advertising Bluetooth device nearby. If fifteen or more unique MAC addresses are observed in a single four-second scan, the firmware raises the Bluetooth alert.

Fifteen unique devices in four seconds is an unusually high number in normal environments and is consistent with the behavior of BLE spam tools that rapidly cycle through randomized MAC addresses while sending large volumes of advertisement packets. These tools are sometimes used to flood nearby phones with pairing requests or notification prompts.

When a Bluetooth alert is raised the matrix strobes red and blue in the same way as a deauth alert. The web interface displays a blue alert box with the message "BLUETOOTH ATTACK DETECTED."

The Bluetooth alert clears when two consecutive scans return fewer than five unique MAC addresses.


## Dismissing an Alert

Both alert boxes contain a button labeled "DISMISS AND RESUME." Pressing this button sends a dismiss command to the device. The strobe stops immediately and the matrix returns to its previous lighting mode. The alert boxes are hidden.

Dismissing an alert does not mean the attack has stopped. It means you have acknowledged the alert and want the light to return to normal. If the attack is still ongoing when you dismiss, the alert will not reappear until the attack actually stops and then starts again. This prevents the alert from immediately reactivating the moment you dismiss it.

Switching to any lighting mode from the web interface also clears the dismissed state, so future attacks will always trigger a fresh alert regardless of whether you dismissed a previous one.


## Known Limitations

The deauth detection requires the device to be within radio range of the attack. Attacks happening on distant networks or outside the radio range of the XIAO will not be detected.

The Bluetooth spam detection threshold of fifteen unique MAC addresses is a heuristic. Dense environments such as airports, convention halls, or busy public spaces may have enough legitimate Bluetooth devices nearby to trigger a false positive. The threshold is not configurable through the web interface but can be changed in the source code by modifying the value 15 in the checkBLE function.

BLE scanning and WiFi operation run on the same radio hardware. The four-second BLE scan interval is a balance between detection speed and web interface responsiveness. During a scan the web interface may respond more slowly than usual.

The device does not log attacks. When power is removed all detection history is lost.

The device does not send alerts to any external service. It has no internet access. Alerts are only visible on the web interface while you are connected to the device's access point.


## Troubleshooting

LEDs do not light up after uploading: The most common cause is a USB cable that only carries power and not data. Try a different cable. If the Serial Monitor shows "LEDs ready" but the matrix stays dark, the data pin may not be making contact. Remove the matrix board and reseat it firmly.

Buttons in the web interface do not respond: This usually means the HTML was truncated before the JavaScript could load. Reload the page. If it persists, check that the partition scheme is set to Huge APP when using the S3.

The device appears as a COM port but uploads fail: Put the XIAO into manual bootloader mode by holding the BOOT button, tapping RESET, releasing BOOT after one second, then clicking upload.

The web interface does not load at 192.168.4.1: Make sure your device is connected to the xiaoNightlight network and not your home network. Some phones will refuse to use a network with no internet access. On Android you may need to disable the "Switch to mobile data when WiFi has no internet" setting. On iOS the system may show a prompt asking if you want to stay on the network; tap to confirm.

The matrix strobes constantly and cannot be dismissed: The attack is still ongoing. Move to a different location or wait for the source to stop transmitting. Once two consecutive clean windows pass, the alert will clear on its own.

Compilation fails with "Adafruit NeoPixel.h: No such file or directory": The Adafruit NeoPixel library is not installed. Go to Tools, then Manage Libraries, search for Adafruit NeoPixel, and install it.

Compilation fails with errors related to BLEScan or BLEDevice: The ESP32 board support package is not installed or is out of date. Go to Tools, then Boards Manager, search for esp32, and install or update the package by Espressif Systems.


## Project Structure

The project ships as two separate sketch files, one for the C3 and one for the S3. The logic is identical between both files. The differences are limited to the LED library being used, the data pin definition, and a boot delay adjustment for the S3.

    sketch_c3/   contains the C3 version using FastLED
    sketch_s3/   contains the S3 version using Adafruit NeoPixel

Within each sketch the code is organized in the following order:

Library includes and pin definitions come first, followed by global state variables for the LED animation, alert tracking, BLE scan state, and WiFi channel hopping. After that are helper functions for HSV color conversion, LED fill and fade utilities, and the speed-to-delay mapping. The WiFi promiscuous callback and BLE advertised device callback follow. The web interface HTML is assembled and served by the handleRoot function. The remaining route handlers cover toggle, mode selection, speed, brightness, alerts, and dismiss. The animation functions for each lighting mode are collected in runMode. The strobe function, deauth check, and BLE check complete the main logic. Setup and loop are at the end.


## License and Credits

This project is released under the MIT License. You are free to use, modify, and distribute it for any purpose, personal or commercial, with or without attribution. A mention is appreciated but not required.

Project concept, firmware, and web interface by Your Pal Kal.

Valleytech Solutions
@valleytechsolutions

Special thanks to the Seeed Studio team for the XIAO platform and the 6x10 RGB Matrix, to Adafruit for the NeoPixel library, and to the FastLED contributors.

