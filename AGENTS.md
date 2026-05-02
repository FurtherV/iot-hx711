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

## Iteration 3

HX711 Sampling And Calibrated Sample Output
Summary
Add the HX711 runtime sampler, expose cached sample data through GET /sample, and show the current sample on the WebUI home screen.

Key Changes
Add app_sample module:
initialize esp-idf-lib/hx711 with CONFIG_APP_HX711_DT_GPIO and CONFIG_APP_HX711_SCK_GPIO
use HX711_GAIN_A_64
read averaged HX711 samples once per second with hx711_read_average
cache the JSON representation of the latest successful sample
protect cached JSON with a FreeRTOS mutex
do not advance sequence_number on failed HX711 reads

Sample identity:
store system incarnation in NVS namespace app_state, key incarnation
increment incarnation once on every boot after NVS init
factory reset / NVS erase naturally resets incarnation
sequence_number starts at 0 on boot and increments on each successful sample

Calibration:
empty-scale raw offset is -171000
2.5 kg calibration raw value is 224622
calibration span is 395622 raw counts for 2500 g
convert raw readings to grams with fixed-point math
clamp reported grams to the 0.0 to 5000.0 g range

GET /sample returns JSON with multiple data values:
{
"incarnation": NUMBER,
"sequence_number": NUMBER,
"data": [
{ "value": NUMBER, "unit": "g" },
{ "value": NUMBER, "unit": "raw" }
]
}

Startup Order
Keep show_greetings() intact.
Keep show_greetings(); as the first instruction in app_main.
After NVS init, start:
activity LED
sample service
WiFi
mDNS
web server

WebUI
Poll GET /sample every second with cache: "no-store".
Show the first data value on the Home screen, which is calibrated grams.
Show sequence_number and incarnation near the sample value.
Keep showing the previous value if polling temporarily fails.

Test Plan For Human Developer
Codex must not run tests, builds, compile checks, or automatic dependency installs.
Human developer should run the ESP-IDF build and flash flow.
Human developer should verify:
/sample returns valid JSON with both g and raw data entries
grams read around 0 g with an empty scale
grams read around 2500 g with the calibration weight
grams clamp at 0 g below tare and 5000 g above the configured maximum
sequence_number increases over time after successful samples
incarnation increases after reboot and resets after NVS erase
WebUI home screen updates the sample value every second

## Iteration 4

Update Tab, Partition Overview, And OTA Rollback
Summary
Move firmware update controls into a dedicated Update tab, expose the runtime partition table through the firmware API, and enable OTA rollback with a 60-second validation window.

Key Changes
WebUI:
add Update tab to the primary tab navigation
move the firmware upload form from Home to Update
keep Home focused on live status and sample metrics
add a partition table to Update
show partition label, type, subtype, offset, size, and role/status badges
continue using POST /update for firmware uploads

Partition API:
add GET /api/partitions
enumerate partitions at runtime with esp_partition_find
return label, type, subtype, address, size, encrypted, running, boot, and nextUpdate
compare partitions with esp_ota_get_running_partition, esp_ota_get_boot_partition, and esp_ota_get_next_update_partition
increase HTTP server max_uri_handlers to 12 so all routes can register

OTA rollback:
enable CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE in sdkconfig.defaults
after OTA, booted images remain in ESP_OTA_IMG_PENDING_VERIFY during startup
after NVS, activity LED, sample service, WiFi, mDNS, and web server start, begin a 60-second validation guard
only after the guard delay survives, call esp_ota_mark_app_valid_cancel_rollback
if the app crashes or reboots before validation, bootloader rollback can return to the previous valid OTA slot

Static asset note:
webui/index.html, script.mjs, and style.css are embedded raw through target_add_binary_data
assets are not minified or gzip-compressed yet
gzip serving is possible later by embedding pre-compressed files and setting Content-Encoding: gzip, but current asset size is small enough to keep the simpler raw setup

Test Plan For Human Developer
Codex must not run tests, builds, compile checks, or automatic dependency installs.
Human developer should run the ESP-IDF build and flash flow.
Human developer should verify:
Home no longer contains firmware update controls
Update tab contains firmware upload controls
firmware upload still works through POST /update
/api/partitions returns valid JSON
Update tab shows nvs, otadata, phy_init, ota_0, and ota_1
running, boot, and next OTA roles are shown correctly
HTTP server starts without ESP_ERR_HTTPD_HANDLERS_FULL
after OTA, a new image rolls back if it crashes before the 60-second validation window completes
after OTA, a healthy image is marked valid after surviving the validation window
