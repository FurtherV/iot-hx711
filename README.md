# iot_hx711

`iot_hx711` is ESP-IDF firmware for an ESP32-based HX711 scale node. It reads an HX711 load-cell amplifier, exposes measurements through a small HTTP API, and serves an embedded WebUI for status, configuration, sampling, and OTA firmware updates.

## Features

- HX711 sampling with calibrated gram output and raw-count output.
- WiFi station mode with credentials stored in NVS.
- First-boot provisioning through a fallback SoftAP.
- Embedded WebUI built with Vite and served as compressed static assets.
- Local WebUI simulator with fixture-backed API responses.
- OpenAPI REST API reference generated with Redocly and embedded at `/api.html`.
- OTA firmware upload through the WebUI or `POST /update`.
- OTA rollback support with a validation delay after boot.
- mDNS advertisement for local network discovery.
- Active-low web activity LED pulse.

## Hardware

Default GPIO assignments are configured in `main/Kconfig.projbuild`:

- HX711 `SCK` / `PD_SCK`: GPIO 18
- HX711 `DT` / `DOUT`: GPIO 19
- Status LED: GPIO 2, active-low

The default calibration is compiled into the sampler:

- Empty-scale raw offset: `-171000`
- Calibration span: `395622` raw counts equals `2500 g`
- Reported gram values are clamped to `0 g` through `5000 g`

## Prerequisites

- ESP-IDF with Component Manager support. The component manifest currently declares `idf >=4.1.0`.
- Node.js compatible with Vite 5, meaning Node.js `18.x` or `20.x` and newer, plus npm `8` or newer.
- A serial connection to the ESP32 board for flashing and monitoring.

Firmware dependencies are declared in `main/idf_component.yml` and are resolved by the ESP-IDF Component Manager:

- `esp-idf-lib/hx711`
- `espressif/mdns`

Install WebUI dependencies once before the first firmware build:

```sh
cd webui
npm install
```

The ESP-IDF build runs the WebUI build automatically, so missing `webui/node_modules` will make `idf.py build` fail.

## Repository Layout

- `main/`: ESP-IDF firmware component.
- `main/main.c`: startup owner and service initialization.
- `main/app_wifi.*`: WiFi credentials, STA/SoftAP operation, SSID scans.
- `main/app_web.*`: HTTP server, REST routes, embedded WebUI assets, OTA upload.
- `main/app_sample.*`: HX711 sampling, calibration, sample JSON, sample interval storage.
- `main/app_mdns.*`: mDNS hostname and HTTP service advertisement.
- `main/app_activity_led.*`: activity LED handling.
- `webui/`: Vite WebUI project.
- `webui/src/`: WebUI HTML, JavaScript, CSS, and logo source.
- `webui/mock/`: JSON fixtures for local API simulation.
- `partitions.csv`: NVS, OTA data, PHY, and dual OTA app partitions.
- `CONTRIBUTING.md`: developer notes for firmware and WebUI changes.

Generated output is ignored by git, including ESP-IDF build directories, `managed_components/`, `node_modules/`, and `webui/dist/`.

## Build And Flash

Build the firmware from the repository root:

```sh
idf.py build
```

Flash and monitor:

```sh
idf.py -p PORT flash monitor
```

Replace `PORT` with the serial port for the board.

## Release Artifacts

GitHub release firmware artifacts are created for tags that match `vMAJOR.MINOR.PATCH`, such as `v1.0.0`. Pushing such a tag or manually publishing a GitHub Release for such a tag builds the firmware and attaches:

- `iot_hx711-VERSION-ota.bin`: the app image for WebUI OTA upload.
- `iot_hx711-VERSION-esp32-flash-bundle.zip`: bootloader, partition table, OTA data image, app image, flash metadata, and checksums for first-time flashing.

## First Boot And WiFi Provisioning

On boot, the firmware starts NVS, the activity LED, the HX711 sampler, WiFi, mDNS, and the HTTP/WebUI server. If the running OTA image is pending verification, it is marked valid only after the validation delay.

WiFi behavior:

- If stored station credentials exist and work, the device connects to that network.
- If credentials are missing or connection fails, the device starts a provisioning SoftAP.
- The SoftAP SSID is `iot-XXXXXX`, where `XXXXXX` comes from the last three MAC address bytes.
- The SoftAP password is controlled by `CONFIG_APP_WIFI_AP_PASSWORD`.
- An empty provisioning password means the SoftAP is open.
- A non-empty provisioning password must be 8 to 63 characters.

After connecting to the provisioning network, open `http://192.168.4.1/`. On station networks that support mDNS, the device is also advertised as `iot-XXXXXX.local`.

## WebUI

The WebUI is a Vite project in `webui/`. It uses browser ES modules and `uPlot`; there is no frontend framework.

Main screens:

- Home: live sample plot.
- Information: device information and partition table.
- Update: firmware binary upload.
- Configuration: WiFi credentials, scanned SSID suggestions, sample interval, reset and reboot actions.

Local development:

```sh
cd webui
npm run dev
```

Build the WebUI:

```sh
cd webui
npm run build
```

The local dev and preview servers include mock API middleware. Fixture-backed `GET` requests read matching files from `webui/mock`; for example, `GET /wifi` reads `webui/mock/wifi.json`. Other configured REST requests are logged and return mock success JSON.

The API reference source is `api/openapi.yaml`. `npm run dev` and `npm run build` generate `webui/dist/api.html`, and the WebUI footer links to that rendered OpenAPI reference.

## HTTP API

The firmware uses root-level API paths, not `/api/...` paths.

- `GET /`: embedded WebUI.
- `GET /api.html`: rendered OpenAPI REST API reference.
- `GET /assets/index.js`: WebUI JavaScript.
- `GET /assets/index.css`: WebUI CSS.
- `GET /info`: device, build, heap, partition, and WiFi summary.
- `GET /wifi`: WiFi status, stored credentials, and scanned SSID suggestions.
- `POST /wifi`: save WiFi credentials and restart.
- `POST /wifi/reset`: clear stored WiFi credentials and restart.
- `GET /partitions`: runtime partition table and OTA roles.
- `GET /config`: current configurable values.
- `POST /config/sample`: save sample interval and restart.
- `GET /sample`: latest cached HX711 sample.
- `POST /update`: raw firmware binary OTA upload.
- `POST /reboot`: restart the device.
- `POST /config/reset`: erase stored configuration and restart.

Example `GET /sample` response:

```json
{
  "incarnation": 7,
  "sequence_number": 42,
  "data": [
    { "value": 2498.6, "unit": "g" },
    { "value": 224401, "unit": "raw" }
  ]
}
```

Example `GET /config` response:

```json
{
  "sampleIntervalMs": 1000,
  "sampleIntervalMinMs": 100,
  "sampleIntervalMaxMs": 10000
}
```

Example `GET /wifi` response:

```json
{
  "hasCredentials": true,
  "connected": true,
  "softapActive": false,
  "ssid": "lab-wifi",
  "password": "configureme",
  "ip": "192.168.1.91",
  "apSsid": "",
  "availableSsids": ["lab-wifi", "workshop", "guest"]
}
```

Mutating endpoints generally return JSON such as:

```json
{ "ok": true, "restarting": true }
```

OTA upload is currently unauthenticated. Keep that in mind when deciding which networks can reach the device.

## Contributing

Developer workflow notes are in `CONTRIBUTING.md`.
