# Pegasus TV

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](https://opensource.org/licenses/MIT) [![version](https://img.shields.io/badge/version-v1.0.0-blue.svg)](https://github.com/debsahu/PegasusTV/) [![LastCommit](https://img.shields.io/github/last-commit/debsahu/PegasusTV.svg?style=social)](https://github.com/debsahu/PegasusTV/commits/master)

Goal:
- Control a "dumb" TV using virtual assistants via Home Assistant

Features:

- Uses D2 to control 2N2222 transistor
- Control using Web and MQTT API
- Completely Async
- WiFiManager Captive Portal to get WiFi credentials (Compile with `-DUSE_EADNS` for ESP8266)
- Connect **PIN_CTRL** pin to base of 2N2222, collector to Probe and emitter to GND

## YouTube

[![PegasusTV](https://img.youtube.com/vi/MmY-NLU-UN0/0.jpg)](https://www.youtube.com/watch?v=MmY-NLU-UN0)

## Hardware

![Hardware Setup](https://github.com/debsahu/PegasusTV/blob/master/hardware.png)

## Software Overview

![Software Setup](https://github.com/debsahu/PegasusTV/blob/master/software.png)


### Libraries Needed

[platformio.ini](https://github.com/debsahu/PegasusTV/blob/master/platformio.ini) is included, use [PlatformIO](https://platformio.org/platformio-ide) and it will take care of installing the following libraries.

| Library                   | Link                                                       |
|---------------------------|------------------------------------------------------------|
|ESPAsyncUDP                |https://github.com/me-no-dev/ESPAsyncUDP                    |
|ESPAsyncTCP                |https://github.com/me-no-dev/ESPAsyncTCP                    |
|ESPAsyncWiFiManager        |https://github.com/alanswx/ESPAsyncWiFiManager              |
|ESPAsyncDNSServer          |https://github.com/devyte/ESPAsyncDNSServer                 |
|ESP Async WebServer        |https://github.com/me-no-dev/ESPAsyncWebServer              |
|AsyncMqttClient            |https://github.com/marvinroger/async-mqtt-client            |
