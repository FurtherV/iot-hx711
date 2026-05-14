import { readdir, readFile, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { gzipSync } from "node:zlib";

const distDir = new URL("../dist/", import.meta.url);

async function gzipFiles(dirUrl) {
  const entries = await readdir(dirUrl, { withFileTypes: true });

  for (const entry of entries) {
    const entryUrl = new URL(entry.name, dirUrl);

    if (entry.isDirectory()) {
      await gzipFiles(new URL(`${entry.name}/`, dirUrl));
      continue;
    }

    if (!entry.isFile() || entry.name.endsWith(".gz")) {
      continue;
    }

    const input = await readFile(entryUrl);
    const output = gzipSync(input, { level: 9 });
    await writeFile(`${fileURLToPath(entryUrl)}.gz`, output);
  }
}

await gzipFiles(distDir);
