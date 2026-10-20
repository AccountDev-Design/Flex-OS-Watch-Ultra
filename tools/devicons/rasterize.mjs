// Rasteriza un SVG a PNG con fondo transparente usando el Chromium de Playwright.
// Uso: node rasterize.mjs <entrada.svg> <salida.png> <lado_px>
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
const require = createRequire('/opt/node-tools/');
const { chromium } = require('playwright');
const [, , input, output, sizeArg] = process.argv;
const size = parseInt(sizeArg, 10);
const svg = readFileSync(input, 'utf8')
  .replace(/width="[0-9.]+"/, `width="${size}"`)
  .replace(/height="[0-9.]+"/, `height="${size}"`);
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || undefined });
const page = await browser.newPage({ viewport: { width: size, height: size }, deviceScaleFactor: 1 });
await page.setContent(`<html><body style="margin:0;background:transparent">${svg}</body></html>`);
await page.locator('svg').screenshot({ path: output, omitBackground: true });
await browser.close();
