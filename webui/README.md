# iot_hx711 WebUI

This WebUI is built with Vite and embedded into the ESP-IDF firmware as gzipped static assets.

## Local development

Install dependencies manually from this directory, then start the local simulator:

```sh
npm install
npm run dev
```

The dev server serves the WebUI and mocks configured REST endpoints from `mock-api.config.json`.
Fixture-backed GET requests read JSON from matching files under `mock/`.
For example, `GET /api/info` returns `mock/api/info.json`.

All other configured REST requests are logged by the local server and return a small mocked success response when configured.
The same mock middleware is active for `npm run preview`.

## Firmware assets

```sh
npm run build
```

The build writes `dist/`, then gzip-compresses each generated asset. ESP-IDF embeds only the `.gz` files.
The ESP-IDF build also runs this command automatically when building firmware.
