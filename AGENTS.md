# Project Information

## AGENT Limitations and Project Constraints

- Always keep `show_greetings` in `main.c` intact
- Always run `show_greetings` as the first instruction in `app_main`
- Never automatically install any additional libraries in esp-idf, forward these tasks to the human developer
- Project changes are grouped as iterations in AGENTS.md or CHANGELOG.md when requested. Only update README.md or CONTRIBUTING.md if the iteration changes human-facing setup, behavior, API, structure, workflow, or extension guidance.

## Human Documentation Rules

README.md and CONTRIBUTING.md are for human developers, not for agent memory.

When updating README.md:

- Treat it as the first-time project entrypoint.
- Keep it focused on what the project is, hardware/toolchain prerequisites, setup, build/flash, first boot/provisioning, WebUI usage, and the public HTTP API.
- Include practical facts a new contributor needs, such as GPIO defaults, provisioning URL, dependency setup, generated files, and important security notes.
- Do not mention AGENTS.md, Codex, agents, iteration history, or project-memory bookkeeping.
- Do not include implementation details unless they help a human build, run, configure, or integrate with the project.

When updating CONTRIBUTING.md:

- Treat it as the human contributor guide.
- Keep it focused on firmware structure, WebUI structure, module boundaries, route conventions, extension patterns, dependency policy, and checks before submitting.
- Preserve the rule that public API routes are root-level and no new `/api/...` routes should be added.
- Preserve the `show_greetings()` constraint in human-readable wording.
- Prefer contributor wording such as "discuss new firmware dependencies before adding them" over agent wording such as "do not install automatically".
- Do not mention AGENTS.md, Codex, agents, iteration history, or project-memory bookkeeping.

When project facts change:

- Update README.md only for human-facing setup, hardware, API, behavior, or usage changes.
- Update CONTRIBUTING.md only for workflow, structure, extension-pattern, dependency, or contribution-check changes.
- Do not update human docs just because AGENTS.md iteration history changed, unless the change also affects human-facing project information.

# Current Project State

This is an ESP-IDF HX711 scale node with WiFi provisioning, mDNS discovery, a gzipped Vite WebUI, root-level HTTP API routes, OTA updates with rollback support, and a configurable sampling loop.

## Firmware Structure

- `main/main.c` owns startup order and must keep `show_greetings();` first in `app_main`.
- `main/app_wifi.*` owns WiFi STA credentials, SoftAP fallback, WiFi reset, and scan results for the WebUI SSID datalist.
- `main/app_web.*` owns the HTTP server, static gzipped WebUI assets, root-level API routes, OTA upload, partition reporting, and restart actions.
- `main/app_sample.*` owns HX711 setup, calibrated sample caching, and sample interval persistence.
- `main/app_mdns.*` advertises the device as `iot-XXXXXX.local`.
- `main/app_activity_led.*` pulses the active-low status LED on web activity.
- `main/Kconfig.projbuild` owns project options such as SoftAP password, HX711 pins, and status LED pin.

## WebUI Structure

- Source lives in `webui/src`.
- The frontend uses Vite, ES modules, and uPlot.
- Local development and preview use a mock API middleware driven by `webui/mock-api.config.json` and JSON fixtures under `webui/mock`.
- The ESP-IDF build runs the WebUI build first, embeds only gzip-compressed output from `webui/dist`, and serves those assets with `Content-Encoding: gzip`.

## Current HTTP API

- `GET /`
- `GET /assets/index.js`
- `GET /assets/index.css`
- `GET /info`
- `GET /wifi`
- `POST /wifi`
- `POST /wifi/reset`
- `GET /partitions`
- `GET /config`
- `POST /config/sample`
- `GET /sample`
- `POST /update`
- `POST /reboot`
- `POST /config/reset`

There are no `/api/...` compatibility routes.

# Compact Iteration History

## Iteration 1 - Initial Web-IOT Skeleton

Added the first embedded WebUI, NVS-backed WiFi credentials, STA connection, SoftAP fallback provisioning, HTTP routes, and unauthenticated OTA upload through `POST /update`. Added an OTA partition table with `nvs`, `otadata`, `phy_init`, `ota_0`, and `ota_1`.

## Iteration 2 - Configuration And Discovery

Added project Kconfig options for provisioning SoftAP password, HX711 GPIOs, and status LED GPIO. Added mDNS naming as `iot-XXXXXX.local` and a web activity LED module.

## Iteration 3 - HX711 Sampling

Added the HX711 sample service using configured GPIOs, cached JSON samples, NVS-backed incarnation tracking, per-boot sequence numbers, and fixed calibration from raw counts to grams. Exposed the current sample through `GET /sample`.

## Iteration 4 - Update Tab, Partitions, And OTA Rollback

Moved firmware update controls into a dedicated WebUI Update screen, added runtime partition reporting, increased HTTP handler capacity, and enabled OTA rollback with a 60-second validation guard.

## Iteration 5 - WiFi Reset And Active-Low LED

Added a WiFi credential reset action that erases only the `wifi_config` NVS namespace and restarts the device. Changed activity LED behavior to active-low polarity.

## Iteration 6 - Vite Build, Mock API, And Gzip Assets

Moved the WebUI to a Vite project under `webui/src`, added local dev and preview scripts with fixture-backed mock API responses, and integrated the WebUI build into ESP-IDF so gzip-compressed assets are embedded into firmware.

## Iteration 7 - Root-Level API Paths

Removed the `/api` prefix from firmware, WebUI, and local mock routes. Current routes use paths such as `/info`, `/wifi`, `/partitions`, `/config`, `/sample`, and `/update`.

## Iteration 8 - Chart, Sample Interval, And Session Tabs

Replaced the Home scalar sample display with a uPlot chart using a fixed 0 g to 5500 g range and rolling sample history. Added configurable sample interval persistence through `/config` and `/config/sample`. Added browser-session tab persistence with `sessionStorage`.

## Iteration 9 - Compact Project Documentation

Compacted the verbose historical project plans into a shorter current-state overview, root-level API reference, concise iteration ledger, and explicit test policy. Preserved the agent constraints and current firmware/WebUI structure notes so future changes have less duplicated history to scan.

## Iteration 10 - Brand Logo Update

Replaced the temporary WebUI firmware mark with a cleaned, brand-colored SVG derived from `converted_graphic_smooth.svg`. Kept the logo as an image-only mark, integrated it through the existing `firmware-logo.svg?raw` import path, adjusted the header logo sizing for the wider shape, and removed the root-level source SVG after moving its usable content into the WebUI asset.

# Test Policy

Codex must not run ESP-IDF tests, builds, compile checks, flashes, or automatic ESP-IDF dependency installs. Human developer should run ESP-IDF verification. Frontend-only checks such as `npm run build` may be run when WebUI files are changed and dependencies are already installed.
