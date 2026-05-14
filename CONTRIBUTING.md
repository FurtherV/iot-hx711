# Contributing

This project combines ESP-IDF firmware with an embedded Vite WebUI. Keep changes small, testable, and aligned with the existing module boundaries.

## Firmware Structure

The firmware entry point is `main/main.c`. Keep `show_greetings()` intact and keep `show_greetings();` as the first instruction in `app_main()`.

Startup currently initializes services in this order:

1. NVS
2. Activity LED
3. HX711 sample service
4. WiFi
5. mDNS
6. HTTP/WebUI server
7. OTA rollback validation guard

Main firmware modules:

- `app_wifi`: WiFi credential storage, STA connection, provisioning SoftAP, WiFi scan results.
- `app_web`: HTTP routes, JSON APIs, compressed WebUI assets, OTA upload.
- `app_sample`: HX711 sampling, calibration, cached `/sample` JSON, sample interval config.
- `app_mdns`: local hostname and HTTP service advertisement.
- `app_activity_led`: active-low web activity LED pulses.

Public HTTP API routes are root-level paths such as `/wifi`, `/info`, `/sample`, `/partitions`, `/config`, `/update`, `/reboot`, and `/config/reset`. Do not add new `/api/...` routes.

New firmware dependencies should be discussed before they are added or vendored. Current dependencies are declared in `main/idf_component.yml`.

## WebUI Structure

The WebUI lives in `webui/` and is built with Vite.

- `webui/src/index.html`: screen layout, navigation, forms, dialogs.
- `webui/src/script.mjs`: UI state, API calls, rendering, event handlers.
- `webui/src/style.css`: visual styling.
- `webui/src/firmware-logo.svg`: logo source imported by JavaScript.
- `webui/mock/*.json`: fixture JSON for local `GET` requests.
- `webui/mock-api.config.json`: local mock route configuration.
- `webui/vite.config.mjs`: Vite build plus mock API middleware.
- `webui/scripts/gzip-dist.mjs`: compression step after `npm run build`.

Useful commands:

```sh
cd webui
npm install
npm run dev
npm run build
npm run preview
```

`npm run dev` and `npm run preview` serve fixture-backed `GET` responses from `webui/mock`. Non-GET REST requests are logged and return configured mock success responses.

The ESP-IDF build runs `npm run build` automatically and embeds the generated compressed assets from `webui/dist`. Run `npm install` in `webui/` before building firmware on a fresh checkout.

## Adding Configuration Sections

Configuration UI sections live inside the `#configuration .config-stack` container in `webui/src/index.html`. Follow the existing section pattern:

```html
<section class="config-section" data-config-section="example">
  <div class="section-heading">
    <div>
      <h2>Example</h2>
    </div>
  </div>

  <form id="exampleConfigForm" class="form config-form">
    <label>
      <span>Example value</span>
      <input id="exampleValue" name="exampleValue" type="number" required>
    </label>
    <div class="form-actions">
      <button class="primary" type="submit">Save and Restart</button>
      <p class="message hidden" id="exampleConfigMessage"></p>
    </div>
  </form>
</section>
```

Then update `webui/src/script.mjs`:

- Add default state if the value is loaded from `/config`.
- Extend `renderConfig()` to populate the new input.
- Add a setup function that validates input, posts JSON, and updates the message element.
- Call the setup function from the `DOMContentLoaded` handler.
- Use existing helpers such as `readJson()`, `postJson()`, `setText()`, and `showAlert()`.

For backend support, add or extend a root-level endpoint in `main/app_web.c`. Small settings can usually be returned by `GET /config` and saved through a focused `POST /config/...` route. Store persistent values in NVS from the owning firmware module, not directly from the WebUI layer.

For local simulation, update:

- `webui/mock/config.json` for new `GET /config` fields.
- `webui/mock-api.config.json` for any new `POST /config/...` mock response.

## Before Submitting Changes

- For WebUI changes, run `cd webui && npm run build`.
- For firmware changes, run `idf.py build` from the repository root.
- For changes that affect embedded WebUI assets, run both checks.
- Do not commit generated output such as ESP-IDF build directories, `managed_components/`, `node_modules/`, or `webui/dist/`.
- Update `README.md` or this file when changing setup steps, public API routes, firmware structure, or WebUI extension patterns.
