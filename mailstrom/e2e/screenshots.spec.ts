/**
 * Not assertions — this spec exists to produce reviewable screenshots of the
 * real UI at iPhone size. Run with: npx playwright test e2e/screenshots.spec.ts
 */
import { test } from '@playwright/test';
import { seedClientId, stubGmailApi, stubGoogleAuth } from './fixtures';

const OUT = 'screenshots';

test.describe('screenshots', () => {
  test.beforeEach(async ({ page }) => {
    await stubGoogleAuth(page);
    await stubGmailApi(page);
  });

  test('setup', async ({ page }) => {
    await page.goto('/');
    await page.screenshot({ path: `${OUT}/01-setup.png`, fullPage: true, animations: 'disabled' });
  });

  test('list, sheet and filter', async ({ page }) => {
    await seedClientId(page);
    await page.goto('/');
    await page.getByTestId('sign-in').click();
    await page.getByTestId('sender-row').first().waitFor({ timeout: 15_000 });

    await page.screenshot({ path: `${OUT}/02-senders.png`, animations: 'disabled' });

    await page.getByTestId('sender-row').filter({ hasText: 'Acme Weekly' }).click();
    await page.screenshot({ path: `${OUT}/03-sender-sheet.png`, animations: 'disabled' });

    await page.getByTestId('action-filter').click();
    await page.screenshot({ path: `${OUT}/04-filter.png`, animations: 'disabled' });
  });

  test('dark mode', async ({ page }) => {
    await seedClientId(page);
    await page.emulateMedia({ colorScheme: 'dark' });
    await page.goto('/');
    await page.getByTestId('sign-in').click();
    await page.getByTestId('sender-row').first().waitFor({ timeout: 15_000 });

    await page.screenshot({ path: `${OUT}/05-dark.png`, animations: 'disabled' });

    await page.getByTestId('sender-row').filter({ hasText: 'Acme Weekly' }).click();
    await page.getByTestId('action-trash').click();
    await page.screenshot({ path: `${OUT}/06-dark-confirm.png`, animations: 'disabled' });
  });
});
