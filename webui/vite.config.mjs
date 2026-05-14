import { existsSync } from "node:fs";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";

const projectRoot = fileURLToPath(new URL(".", import.meta.url));
const sourceRoot = resolve(projectRoot, "src");
const mockConfigPath = resolve(projectRoot, "mock-api.config.json");
const apiDocsPath = resolve(projectRoot, "dist", "api.html");

async function readRequestBody(req) {
  const chunks = [];
  for await (const chunk of req) {
    chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
  }
  return Buffer.concat(chunks);
}

function jsonResponse(res, statusCode, payload) {
  res.statusCode = statusCode;
  res.setHeader("Content-Type", "application/json");
  res.setHeader("Cache-Control", "no-store");
  res.end(JSON.stringify(payload));
}

async function serveApiDocs(res) {
  if (!existsSync(apiDocsPath)) {
    res.statusCode = 404;
    res.setHeader("Content-Type", "text/plain");
    res.setHeader("Cache-Control", "no-store");
    res.end("API documentation has not been generated. Run 'npm run docs:build' or 'npm run build' in webui/.");
    return;
  }

  const html = await readFile(apiDocsPath);
  res.statusCode = 200;
  res.setHeader("Content-Type", "text/html");
  res.setHeader("Cache-Control", "no-store");
  res.end(html);
}

async function loadMockConfig() {
  const raw = await readFile(mockConfigPath, "utf8");
  const config = JSON.parse(raw);
  return {
    fixtureRoutes: new Set(config.fixtureRoutes || []),
    restPrefixes: config.restPrefixes || ["/info", "/wifi", "/partitions", "/sample", "/update", "/reboot", "/config"],
    successResponses: config.successResponses || {},
  };
}

function isRestPath(pathname, prefixes) {
  return prefixes.some((prefix) => pathname === prefix || pathname.startsWith(prefix));
}

function fixturePathFor(pathname) {
  return resolve(projectRoot, "mock", `${pathname.replace(/^\/+/, "")}.json`);
}

function mockApiPlugin() {
  const installMiddleware = (middlewares, logger) => {
    middlewares.use(async (req, res, next) => {
      const url = new URL(req.url || "/", "http://localhost");
      const pathname = url.pathname;
      const config = await loadMockConfig();

      if (req.method === "GET" && pathname === "/api.html") {
        await serveApiDocs(res);
        return;
      }

      if (!isRestPath(pathname, config.restPrefixes)) {
        next();
        return;
      }

      if (req.method === "GET" && config.fixtureRoutes.has(pathname)) {
        const fixturePath = fixturePathFor(pathname);
        if (!existsSync(fixturePath)) {
          jsonResponse(res, 404, {
            ok: false,
            error: `Missing mock fixture for ${pathname}`,
            fixture: fixturePath,
          });
          return;
        }

        const fixture = await readFile(fixturePath);
        res.statusCode = 200;
        res.setHeader("Content-Type", "application/json");
        res.setHeader("Cache-Control", "no-store");
        res.end(fixture);
        return;
      }

      const body = await readRequestBody(req);
      const preview = body.length > 512 ? `${body.subarray(0, 512).toString("utf8")}...` : body.toString("utf8");
      const headers = JSON.stringify(req.headers);
      logger.info(
        `[mock-api] ${req.method} ${pathname} headers=${headers} content-length=${body.length}${preview ? ` body=${preview}` : ""}`,
      );

      if (req.method === "GET") {
        jsonResponse(res, 404, {
          ok: false,
          mocked: true,
          error: `No fixture route configured for ${pathname}`,
        });
        return;
      }

      const response = config.successResponses[pathname] || { ok: true, mocked: true };
      jsonResponse(res, 200, response);
    });
  };

  return {
    name: "iot-hx711-mock-api",
    configureServer(server) {
      installMiddleware(server.middlewares, server.config.logger);
    },
    configurePreviewServer(server) {
      installMiddleware(server.middlewares, server.config.logger);
    },
  };
}

export default defineConfig({
  root: sourceRoot,
  publicDir: false,
  base: "/",
  plugins: [mockApiPlugin()],
  server: {
    host: "127.0.0.1",
    port: 5173,
    strictPort: false,
  },
  preview: {
    host: "127.0.0.1",
    port: 4173,
    strictPort: false,
  },
  build: {
    outDir: resolve(projectRoot, "dist"),
    emptyOutDir: true,
    assetsDir: "assets",
    rollupOptions: {
      output: {
        entryFileNames: "assets/[name].js",
        chunkFileNames: "assets/[name].js",
        assetFileNames: "assets/[name][extname]",
      },
    },
  },
});
