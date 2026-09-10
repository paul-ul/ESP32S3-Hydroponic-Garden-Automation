# Smart Garden Controller

Firmware for the light controller of the Smart Growbox Bierkiste. Runs on a
**Seeed XIAO ESP32-S3** and drives up to three LED strips (zones A/B/C)
automatically, following the real sunrise/sunset at the configured location.

## Features

- **Sun-based schedule instead of a timer**: Sunrise/sunset are calculated
  locally using the NOAA/USNO formula (no internet service needed, only the
  clock comes from NTP). Each zone has its own offset relative to sunrise/
  sunset, a maximum brightness, a fade-in/fade-out duration, and a minimum
  and maximum light duration.
- **Plant-type presets**: Instead of entering individual numeric values, you
  pick a plant type per zone from a dropdown in the web interface (e.g.
  shade plant, sun plant, herbs, succulent/cactus …).
- **1 or 3 LED strips**: Switchable from the web interface. In "1 strip"
  mode only zone A is active (same output, pin D1); zones B/C are hidden
  and switched off.
- **Light test switch**: Immediately sets all active zones to 100%
  brightness, for quickly checking the wiring without waiting for the sun
  schedule.
- **German/English**: The web interface can be switched with one click; the
  choice is saved permanently.
- **WiFi setup via Bluetooth (BLE provisioning)**: No WiFi credentials in
  the code. Setup/re-setup is done via an app directly on the device.
- **Boot animation ("fluorescent tube flicker")**: On startup, each zone
  briefly flickers on like an old fluorescent tube, staggered per zone. If
  it's currently "night" according to the sun schedule, the zone only
  flickers unsuccessfully and stays dark (instead of turning on).
- **Status LEDs**: The red LED blinks in a heartbeat rhythm while no WiFi is
  connected, and turns off once connected. The yellow LED lights up while
  BLE provisioning is active.
- **OTA updates** and a local web interface at `http://smartgarden.local`.

## Hardware

Board: **Seeed XIAO ESP32-S3**

| Function                          | Pin  | GPIO  | Note                              |
|-------------------------------------|------|-------|------------------------------------|
| PWM channel A (zone 1)              | D1   | GPIO2 |                                    |
| PWM channel B (zone 2)              | D2   | GPIO3 | S3 boot strapping pin              |
| PWM channel C (zone 3)              | D10  | GPIO9 |                                    |
| Red status LED (heartbeat)          | D3   | GPIO4 |                                    |
| Yellow status LED (BLE active)      | D4   | GPIO5 |                                    |

## Arduino IDE setup

1. **Board**: Tools → Board → select "XIAO_ESP32S3".
2. **Board manager version**: Tools → Board → Boards Manager → "esp32"
   (Espressif Systems) → install/select **version 3.3.6**.
   > ⚠️ Version 3.3.7 has a known bug (crash during BLE provisioning on
   > ESP32-S3, see [espressif/arduino-esp32#12436](https://github.com/espressif/arduino-esp32/issues/12436)).
   > Do not update to 3.3.7 until that bug is fixed.
3. **Partition Scheme**: Tools → Partition Scheme → pick a variant with more
   app storage (e.g. "Minimal SPIFFS (1.9MB APP/190KB SPIFFS)"), otherwise
   the compiler reports "Sketch too big".
4. Required libraries (already bundled with the board package, no separate
   install needed): `WiFi`, `WiFiProv`, `WebServer`, `Preferences`,
   `ArduinoOTA`, `ESPmDNS`.

## First-time setup

1. Upload the sketch.
2. Install the **"ESP BLE Provisioning"** app (Espressif, free,
   Android/iOS) on your smartphone.
3. In the app, look for the device **`PROV_SMARTGARDEN`** and connect.
4. Enter **`abcd1234`** as the PIN (Proof of Possession) (changeable in the
   code under `PROV_POP`).
5. Send your WiFi SSID and password to the device via the app.
6. The device connects; afterwards the web interface is reachable at
   `http://smartgarden.local`.

If the WiFi password changes later, just click "Reconfigure WiFi
credentials via Bluetooth" in the web interface — no re-flashing needed.

## Web interface

Reachable at `http://smartgarden.local` (on the same WiFi network):

- Current time, sunrise/sunset, and current brightness per zone
- Select and save a plant type per zone
- Switch the number of LED strips (1 or 3)
- Start/stop the light test (100% on all active zones)
- Switch the language between German and English
- Reconfigure WiFi credentials via Bluetooth

## Known issues & troubleshooting

- **Crash on boot ("Guru Meditation Error", "HLI Magic mismatch")**: Occurs
  with esp32 board package version 3.3.7 on ESP32-S3 (see above). Fix:
  downgrade to version 3.3.6 and re-upload.
- **`Failed to connect to ESP32-S3: No serial data received` during upload**:
  Can happen when the native USB connection has become unstable due to a
  crash loop. Workaround: hold the BOOT button while plugging in USB (or
  hold BOOT and briefly press RESET), then upload. Press RESET once after
  uploading to start the firmware normally. Close the Serial Monitor before
  uploading.
