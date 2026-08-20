# ESP32 Reference Firmware (Optional Architecture)

This folder contains an optional reference implementation for running the central WebSocket controller on an **ESP32 microcontroller** (e.g., ESP32 DevKit V1, NodeMCU-32S) instead of an Ubuntu PC/server.

> **Note**: The primary production architecture uses the **Ubuntu Linux Node.js/Express Central Controller (`server.ts`)**. This ESP32 firmware is provided strictly for standalone IoT/embedded reference setups.

### Files
- `esp32_miner_controller.ino`: Arduino C++ sketch utilizing `ESPAsyncWebServer`, `AsyncTCP`, and `ArduinoJson v6` to route WebSocket messages between S905X workers and browser dashboards.
