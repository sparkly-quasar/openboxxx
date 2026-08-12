import { defineConfig, devices } from '@playwright/test';
import { existsSync } from 'node:fs';

// The sandbox ships a pinned Chromium; use it directly when it exists so the
// suite doesn't try to download its own copy.
const preinstalled = '/opt/pw-browsers/chromium';

export default defineConfig({
  testDir: './e2e',
  fullyParallel: true,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 1 : 0,
  reporter: process.env.CI ? 'line' : 'list',
  use: {
    baseURL: 'http://127.0.0.1:5173',
    trace: 'retain-on-failure',
    launchOptions: {
      // CI containers run as root, where Chromium's sandbox refuses to start.
      args: ['--no-sandbox', '--disable-dev-shm-usage'],
      ...(existsSync(preinstalled) ? { executablePath: preinstalled } : {}),
    },
  },
  projects: [
    {
      name: 'iphone',
      use: {
        // iPhone 13 metrics — viewport, DPR, touch, mobile UA — but on
        // Chromium. `devices['iPhone 13']` defaults to WebKit, which is closer
        // to real Safari but is not installed in this environment. These tests
        // check layout, tap targets and app logic, none of which are
        // engine-specific; verify real Safari behaviour on the device itself.
        ...devices['iPhone 13'],
        defaultBrowserType: 'chromium',
        browserName: 'chromium',
      },
    },
  ],
  webServer: {
    command: 'npm run dev -- --port 5173 --host 127.0.0.1',
    url: 'http://127.0.0.1:5173',
    reuseExistingServer: !process.env.CI,
    timeout: 60_000,
  },
});
