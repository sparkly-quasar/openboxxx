/**
 * Render public/icons/icon.svg to the PNG sizes the manifest and iOS need.
 *
 * Uses the Playwright Chromium that's already installed rather than pulling in
 * a native image dependency. Run with `npm run icons` after editing icon.svg.
 */
import { chromium } from '@playwright/test';
import { readFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const iconsDir = resolve(here, '../public/icons');
const svg = readFileSync(resolve(iconsDir, 'icon.svg'), 'utf8');

/**
 * `maskable` icons get cropped to whatever shape the platform likes, so the
 * artwork has to sit inside a safe circle covering the middle 80%. We scale the
 * source down and pad it with the brand colour rather than drawing a second SVG.
 */
const targets = [
  { file: 'icon-192.png', size: 192, padding: 0 },
  { file: 'icon-512.png', size: 512, padding: 0 },
  { file: 'icon-maskable-512.png', size: 512, padding: 0.1 },
  // iOS ignores the manifest for the home-screen icon and uses this one.
  { file: 'apple-touch-icon.png', size: 180, padding: 0 },
];

mkdirSync(iconsDir, { recursive: true });

// The preinstalled Chromium may not match this Playwright build's expected
// revision, so point at it directly when it's present.
const { existsSync } = await import('node:fs');
const preinstalled = '/opt/pw-browsers/chromium';
const browser = await chromium.launch(
  existsSync(preinstalled) ? { executablePath: preinstalled } : {},
);

for (const { file, size, padding } of targets) {
  const page = await browser.newPage({
    viewport: { width: size, height: size },
    deviceScaleFactor: 1,
  });

  const inset = Math.round(size * padding);
  await page.setContent(
    `<html><body style="margin:0;width:${size}px;height:${size}px;background:#2f5fd8;
       display:flex;align-items:center;justify-content:center;overflow:hidden">
       <div style="width:${size - inset * 2}px;height:${size - inset * 2}px">${svg}</div>
     </body></html>`,
    { waitUntil: 'load' },
  );

  await page.screenshot({ path: resolve(iconsDir, file), omitBackground: false });
  await page.close();
  console.log(`wrote ${file} (${size}x${size})`);
}

await browser.close();
