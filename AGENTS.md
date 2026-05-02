# Project Information

- Always keep `show_greetings` in `main.c` intact
- Always run `show_greetings` as the first instruction in `app_main`
- Never run any tests or compile checks, forward these tasks to the human developer
- Never automatically install any additional libraries, forward these tasks to the human developer

# Previous Project Plans

## Iteration 1

Initial Web-IOT Skeleton
Summary
Build a small ESP-IDF Web-IOT foundation around the existing HX711 project while preserving show_greetings() exactly and keeping show_greetings(); as the first instruction in app_main.

The skeleton will use SoftAP web provisioning, NVS credential storage, an embedded single-page WebUI, and unauthenticated OTA uploads through both the WebUI and POST /update, per your selected defaults.

Key Changes
Add root-level webui/ containing exactly:
index.html
script.mjs
style.css
Embed those three files into firmware via main/CMakeLists.txt EMBED_FILES.
Add firmware modules for:
NVS initialization and WiFi credential storage
STA connection using stored credentials
fallback SoftAP provisioning when credentials are missing or STA connect fails
HTTP server routes
OTA upload handling
Keep main/main.c as the startup owner:
leave show_greetings() intact
first line inside app_main remains show_greetings();
remove the current restart countdown behavior
initialize NVS, WiFi, and Web server after greetings
Firmware Behavior
WiFi provisioning:
On boot, load ssid and password from NVS namespace wifi_config.
If credentials exist, start STA mode and attempt connection.
If credentials are missing or connection fails after a bounded retry count, start SoftAP mode.
SoftAP SSID: iot-hx711-XXXXXX, using the last 3 MAC bytes.
SoftAP password: configureme for the initial skeleton.
WebUI configuration tab posts credentials to POST /api/wifi, firmware stores them in NVS, then restarts.
Web server routes:
GET / serves embedded index.html
GET /script.mjs serves embedded JS with application/javascript
GET /style.css serves embedded CSS with text/css
GET /api/info returns JSON with app name, IDF version, chip target, free heap, WiFi mode/status/IP when available, and active OTA partition label.
GET /api/wifi returns current WiFi status without exposing the password.
POST /api/wifi accepts JSON { "ssid": "...", "password": "..." }.
POST /update accepts a raw firmware binary upload and performs OTA using esp_ota_begin, esp_ota_write, esp_ota_end, and esp_ota_set_boot_partition, then restarts on success.
OTA support:
Add a custom partitions.csv with nvs, otadata, phy_init, ota_0, and ota_1.
Update sdkconfig defaults to use the custom OTA partition table.
Leave rollback/signature policy out of v1 unless added later.
WebUI
Build a compact, modern industrial-style UI inspired by Perinet-style dashboard surfaces, using orange accents instead of green.
Use tab navigation only for:
Home: status summary and OTA upload control
Information: device/build/network details from /api/info
Configuration: WiFi credential form and connection status
Keep the WebUI dependency-free: plain HTML, CSS, and ES module JavaScript.
OTA WebUI flow:
file input accepts .bin
upload uses fetch('/update', { method: 'POST', body: file })
show progress/status text and success/failure result
Test Plan For Human Developer
Do not run tests, builds, compiles, or automatic dependency installs from Codex.
Human developer should run:
idf.py menuconfig only if they want to inspect partition settings
idf.py build
flash and verify first boot starts SoftAP when no credentials exist
submit WiFi credentials through WebUI and confirm reboot into STA mode
open WebUI over STA IP and verify all three tabs
upload a valid firmware .bin through WebUI
upload the same .bin with curl -X POST --data-binary @firmware.bin http://DEVICE_IP/update
Assumptions
ESP-IDF v6.0.1 APIs from the current build tree are the target.
No authentication is added for OTA in this first skeleton.
No new external libraries are installed.
WebUI code means frontend assets only; firmware HTTP/WiFi/OTA C code may live under main/.
Perinet visual reference is treated as a general industrial dashboard cue; source checked: 
https://perinet.io/en/news/product-updates/perinets-new-dashboard-container-real-time-and-historic-data-visualization-no-configuration-necessary

## Iteration 2

Configuration And Device Discovery Delta
Summary
Add project-specific sdkconfig options and small runtime helpers beyond the initial Web-IOT skeleton.

Key Changes
Add main/Kconfig.projbuild with custom project options for:
initial provisioning SoftAP password
HX711 SCK GPIO
HX711 DT GPIO
on-board status LED GPIO
Use CONFIG_APP_WIFI_AP_PASSWORD for the provisioning SoftAP:
empty value keeps the AP open
non-empty value enables WPA2-PSK
reject non-empty SoftAP passwords outside the ESP-IDF 8-63 character range
Use default HX711 pin config values:
CONFIG_APP_HX711_SCK_GPIO defaults to 18
CONFIG_APP_HX711_DT_GPIO defaults to 19
Store the confirmed on-board LED pin:
CONFIG_APP_STATUS_LED_GPIO defaults to 2
Add mDNS support:
new app_mdns module
advertise the HTTP service as http://iot-XXXXXX.local
derive XXXXXX from the last three SoftAP MAC bytes, matching the iot-XXXXXX device naming pattern
Add web activity LED behavior:
new app_activity_led module
configure CONFIG_APP_STATUS_LED_GPIO as output
pulse the LED on web requests and while POST bodies are being received
keep LED pulsing in a background task so HTTP handlers are not delayed

Startup Order
Keep show_greetings() intact.
Keep show_greetings(); as the first instruction in app_main.
After NVS init, start:
activity LED
WiFi
mDNS
web server

Test Plan For Human Developer
Codex must not run tests, builds, compile checks, or automatic dependency installs.
Human developer should run the ESP-IDF build and flash flow.
Human developer should verify:
configured SoftAP password behavior
HX711 GPIO values in menuconfig
status LED GPIO default is 2
web UI is reachable by IP and by http://iot-XXXXXX.local where supported by the client network
LED pulses during web activity
