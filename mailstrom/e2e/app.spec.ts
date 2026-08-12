import { expect, test, type Page } from '@playwright/test';
import { seedClientId, stubGmailApi, stubGoogleAuth, SAMPLE_MESSAGES } from './fixtures';

/** Sign in and wait for the first scan to settle. */
async function signInAndScan(page: Page) {
  await page.goto('/');
  await page.getByTestId('sign-in').click();
  // The stats row only fills in once messages have been grouped.
  await expect(page.getByTestId('sender-row').first()).toBeVisible({ timeout: 15_000 });
}

test.describe('setup', () => {
  test('asks for an OAuth client ID on first run and remembers it', async ({ page }) => {
    await stubGoogleAuth(page);
    await stubGmailApi(page);
    await page.goto('/');

    await expect(page.getByTestId('client-id-input')).toBeVisible();
    // The origin the user must whitelist is shown verbatim.
    await expect(page.getByText('http://127.0.0.1:5173')).toBeVisible();

    await page.getByTestId('client-id-input').fill('abc.apps.googleusercontent.com');
    await page.getByTestId('save-client-id').click();

    await expect(page.getByTestId('sign-in')).toBeVisible();

    // Reloading keeps the saved ID rather than re-asking.
    await page.reload();
    await expect(page.getByTestId('client-id-input')).toHaveCount(0);
  });
});

test.describe('scanning and grouping', () => {
  test.beforeEach(async ({ page }) => {
    await stubGoogleAuth(page);
    await seedClientId(page);
    await stubGmailApi(page);
  });

  test('groups the mailbox by sender with counts and sizes', async ({ page }) => {
    await signInAndScan(page);

    const rows = page.getByTestId('sender-row');
    // 5 distinct senders across the 7 sample messages.
    await expect(rows).toHaveCount(5);

    // Default sort is most-mail-first, and Acme Weekly has 3.
    const first = rows.first();
    await expect(first).toContainText('Acme Weekly');
    await expect(first).toContainText('3');
    // Sizes are summed and humanised: 40k + 42k + 38k ≈ 117 KB.
    await expect(first).toContainText('KB');

    // The account the app is signed into is shown in the header.
    await expect(page.getByText('tester@example.com')).toBeVisible();
  });

  test('marks senders that offer an unsubscribe link', async ({ page }) => {
    await signInAndScan(page);

    const acme = page.getByTestId('sender-row').filter({ hasText: 'Acme Weekly' });
    await expect(acme.getByText('unsub')).toBeVisible();

    // A human sender with no List-Unsubscribe header gets no badge.
    const jane = page.getByTestId('sender-row').filter({ hasText: 'Jane Doe' });
    await expect(jane.getByText('unsub')).toHaveCount(0);
  });

  test('sorts by size', async ({ page }) => {
    await signInAndScan(page);

    await page.getByRole('button', { name: 'Biggest' }).click();
    // The 5 MB photo share outweighs Acme's 120 KB despite having fewer messages.
    await expect(page.getByTestId('sender-row').first()).toContainText('Photos');
  });

  test('folds addresses together when grouping by domain', async ({ page }) => {
    await signInAndScan(page);

    await page.getByRole('button', { name: 'By domain' }).click();

    const acme = page.getByTestId('sender-row').filter({ hasText: 'acme.com' });
    await expect(acme).toHaveCount(1);
    // news@ (3) + billing@ (1)
    await expect(acme).toContainText('4');
    await expect(acme).toContainText('2 addresses');
  });

  test('filters the sender list as you type', async ({ page }) => {
    await signInAndScan(page);

    await page.getByLabel('Filter senders').fill('jane');
    await expect(page.getByTestId('sender-row')).toHaveCount(1);
    await expect(page.getByTestId('sender-row')).toContainText('Jane Doe');

    await page.getByLabel('Filter senders').fill('nothing-matches-this');
    await expect(page.getByText('No senders match that filter.')).toBeVisible();
  });
});

test.describe('bulk actions', () => {
  test.beforeEach(async ({ page }) => {
    await stubGoogleAuth(page);
    await seedClientId(page);
  });

  test('trashing a sender confirms first, then batch-modifies and removes the row', async ({ page }) => {
    const calls = await stubGmailApi(page);
    await signInAndScan(page);

    await page.getByTestId('sender-row').filter({ hasText: 'Acme Weekly' }).click();
    await page.getByTestId('action-trash').click();

    // Destructive actions require an explicit confirmation.
    await expect(page.getByText(/Recoverable in Gmail for 30 days/)).toBeVisible();
    await page.getByTestId('confirm-action').click();

    await expect(page.getByTestId('sender-row').filter({ hasText: 'Acme Weekly' })).toHaveCount(0);

    expect(calls.batchModify).toHaveLength(1);
    expect(calls.batchModify[0].ids.sort()).toEqual(['m1', 'm2', 'm3']);
    // Trash means the TRASH label, never a permanent delete.
    expect(calls.batchModify[0].addLabelIds).toEqual(['TRASH']);
  });

  test('cancelling the confirmation touches nothing', async ({ page }) => {
    const calls = await stubGmailApi(page);
    await signInAndScan(page);

    await page.getByTestId('sender-row').filter({ hasText: 'Acme Weekly' }).click();
    await page.getByTestId('action-trash').click();
    await page.getByRole('button', { name: 'Cancel' }).click();

    await page.getByRole('button', { name: 'Close' }).click();
    await expect(page.getByTestId('sender-row').filter({ hasText: 'Acme Weekly' })).toBeVisible();
    expect(calls.batchModify).toHaveLength(0);
  });

  test('archiving removes INBOX and leaves the sender listed', async ({ page }) => {
    const calls = await stubGmailApi(page);
    await signInAndScan(page);

    await page.getByTestId('sender-row').filter({ hasText: 'Acme Weekly' }).click();
    await page.getByRole('button', { name: /^Archive/ }).click();
    await page.getByTestId('confirm-action').click();

    expect(calls.batchModify[0].removeLabelIds).toEqual(['INBOX']);
    // Archived mail still exists, so the sender stays in the list.
    await expect(page.getByTestId('sender-row').filter({ hasText: 'Acme Weekly' })).toBeVisible();
  });

  test('mark-as-read is offered only when there is unread mail', async ({ page }) => {
    const calls = await stubGmailApi(page);
    await signInAndScan(page);

    // Photos has no unread messages, so the button is disabled.
    await page.getByTestId('sender-row').filter({ hasText: 'Photos' }).click();
    await expect(page.getByRole('button', { name: /^Mark read/ })).toBeDisabled();
    await page.getByRole('button', { name: 'Close' }).click();

    await page.getByTestId('sender-row').filter({ hasText: 'Acme Weekly' }).click();
    await page.getByRole('button', { name: /^Mark read/ }).click();
    await page.getByTestId('confirm-action').click();

    expect(calls.batchModify[0].removeLabelIds).toEqual(['UNREAD']);
  });
});

test.describe('unsubscribe', () => {
  test.beforeEach(async ({ page }) => {
    await stubGoogleAuth(page);
    await seedClientId(page);
  });

  test('sends a one-click POST and reports the honest outcome', async ({ page }) => {
    const calls = await stubGmailApi(page);
    await signInAndScan(page);

    await page.getByTestId('sender-row').filter({ hasText: 'Acme Weekly' }).click();
    await expect(page.getByTestId('action-unsubscribe')).toContainText('one-click');
    await page.getByTestId('action-unsubscribe').click();

    await expect(page.getByText(/Unsubscribe request sent/)).toBeVisible();
    expect(calls.unsubscribePosts).toContain('https://acme.com/u/1');
  });

  test('disables the button for senders with no unsubscribe header', async ({ page }) => {
    await stubGmailApi(page);
    await signInAndScan(page);

    await page.getByTestId('sender-row').filter({ hasText: 'Jane Doe' }).click();
    await expect(page.getByTestId('action-unsubscribe')).toBeDisabled();
    await expect(page.getByTestId('action-unsubscribe')).toContainText('No unsubscribe link');
  });
});

test.describe('filters', () => {
  test.beforeEach(async ({ page }) => {
    await stubGoogleAuth(page);
    await seedClientId(page);
  });

  test('creates a skip-inbox + mark-read filter for a sender', async ({ page }) => {
    const calls = await stubGmailApi(page);
    await signInAndScan(page);

    await page.getByTestId('sender-row').filter({ hasText: 'Acme Weekly' }).click();
    await page.getByTestId('action-filter').click();

    // Both toggles default on.
    await page.getByRole('button', { name: 'Create filter' }).click();
    await expect(page.getByText(/Filter created/)).toBeVisible();

    expect(calls.filters).toHaveLength(1);
    expect(calls.filters[0]).toEqual({
      criteria: { from: 'news@acme.com' },
      action: { removeLabelIds: ['INBOX', 'UNREAD'] },
    });
  });

  test('reuses an existing label instead of creating a duplicate', async ({ page }) => {
    const calls = await stubGmailApi(page);
    await signInAndScan(page);

    await page.getByTestId('sender-row').filter({ hasText: 'Acme Weekly' }).click();
    await page.getByTestId('action-filter').click();
    await page.getByText('Apply a label').click();
    // The stub already has a label called "Bulk", which is the default name.
    await page.getByRole('button', { name: 'Create filter' }).click();

    await expect(page.getByText(/Filter created/)).toBeVisible();
    expect(calls.labelsCreated).toHaveLength(0);
    expect(calls.filters[0].action.addLabelIds).toEqual(['Label_existing']);
  });

  test('a delete filter targets TRASH and drops the other actions', async ({ page }) => {
    const calls = await stubGmailApi(page);
    await signInAndScan(page);

    await page.getByTestId('sender-row').filter({ hasText: 'Acme Weekly' }).click();
    await page.getByTestId('action-filter').click();
    await page.getByText('Send straight to Trash').click();
    await page.getByRole('button', { name: 'Create filter' }).click();

    expect(calls.filters[0].action).toEqual({ addLabelIds: ['TRASH'] });
  });

  test('a domain group produces one filter matching every address', async ({ page }) => {
    const calls = await stubGmailApi(page);
    await signInAndScan(page);

    await page.getByRole('button', { name: 'By domain' }).click();
    await page.getByTestId('sender-row').filter({ hasText: 'acme.com' }).click();
    await page.getByTestId('action-filter').click();
    await page.getByRole('button', { name: 'Create filter' }).click();

    // Gmail's OR-list syntax keeps this to a single filter.
    expect(calls.filters[0].criteria.from).toBe('{billing@acme.com news@acme.com}');
  });
});

test.describe('iPhone fitness', () => {
  test.beforeEach(async ({ page }) => {
    await stubGoogleAuth(page);
    await seedClientId(page);
    await stubGmailApi(page);
  });

  test('never scrolls horizontally at iPhone width', async ({ page }) => {
    await signInAndScan(page);

    const overflow = await page.evaluate(
      () => document.documentElement.scrollWidth - document.documentElement.clientWidth,
    );
    expect(overflow).toBeLessThanOrEqual(0);
  });

  test('tap targets meet the 44px minimum', async ({ page }) => {
    await signInAndScan(page);

    await page.getByTestId('sender-row').first().click();
    for (const testId of ['action-trash', 'action-unsubscribe', 'action-filter']) {
      const box = await page.getByTestId(testId).boundingBox();
      // Apple's HIG minimum is 44pt. Measured heights land a hair under at
      // devicePixelRatio 3 (43.99997 for a 44px box), so allow the rounding.
      expect(box?.height ?? 0).toBeGreaterThanOrEqual(43.9);
    }
  });

  test('text inputs are 16px so iOS does not zoom on focus', async ({ page }) => {
    await signInAndScan(page);

    const size = await page
      .getByLabel('Filter senders')
      .evaluate((el) => parseFloat(getComputedStyle(el).fontSize));
    expect(size).toBeGreaterThanOrEqual(16);
  });

  test('serves an installable manifest and iOS icon', async ({ page, request }) => {
    await page.goto('/');

    await expect(page.locator('link[rel="manifest"]')).toHaveAttribute(
      'href',
      '/manifest.webmanifest',
    );
    await expect(page.locator('link[rel="apple-touch-icon"]')).toHaveCount(1);
    await expect(page.locator('meta[name="apple-mobile-web-app-capable"]')).toHaveAttribute(
      'content',
      'yes',
    );

    const manifest = await request.get('/manifest.webmanifest');
    expect(manifest.ok()).toBeTruthy();
    const parsed = await manifest.json();
    expect(parsed.display).toBe('standalone');
    expect(parsed.icons.length).toBeGreaterThanOrEqual(3);

    // The icons the manifest promises must actually exist.
    for (const icon of parsed.icons) {
      const res = await request.get(icon.src);
      expect(res.ok(), `${icon.src} should be served`).toBeTruthy();
    }
    expect((await request.get('/icons/apple-touch-icon.png')).ok()).toBeTruthy();
  });
});

test.describe('caching', () => {
  test('a second visit reuses cached metadata instead of refetching', async ({ page }) => {
    await stubGoogleAuth(page);
    await seedClientId(page);
    await stubGmailApi(page);

    let batchRequests = 0;
    page.on('request', (req) => {
      if (req.url().includes('/batch/gmail/v1')) batchRequests++;
    });

    await signInAndScan(page);
    const afterFirst = batchRequests;
    expect(afterFirst).toBeGreaterThan(0);

    // The token lives in sessionStorage, so a reload resumes without a new
    // sign-in and goes straight back to the sender list.
    await page.reload();
    await expect(page.getByTestId('sender-row').first()).toBeVisible({ timeout: 15_000 });

    // Same message IDs, all already in IndexedDB — no new metadata fetches.
    expect(batchRequests).toBe(afterFirst);
    await expect(page.getByTestId('sender-row')).toHaveCount(5);
  });
});

test.describe('error handling', () => {
  test('surfaces an expired session rather than failing silently', async ({ page }) => {
    await stubGoogleAuth(page);
    await seedClientId(page);
    await stubGmailApi(page);

    // Break the message list after the app has signed in.
    await page.route('**/gmail/v1/users/me/messages?*', (route) =>
      route.fulfill({
        status: 401,
        contentType: 'application/json',
        body: JSON.stringify({ error: { message: 'Invalid Credentials' } }),
      }),
    );

    await page.goto('/');
    await page.getByTestId('sign-in').click();

    await expect(page.getByText(/session expired/i)).toBeVisible({ timeout: 15_000 });
  });

  test('keeps working when some messages fail to load', async ({ page }) => {
    await stubGoogleAuth(page);
    await seedClientId(page);
    await stubGmailApi(page, SAMPLE_MESSAGES);

    // Batch endpoint dies; the app should fall back to individual GETs.
    await page.route('**/batch/gmail/v1', (route) => route.fulfill({ status: 500, body: 'nope' }));

    await page.goto('/');
    await page.getByTestId('sign-in').click();

    await expect(page.getByTestId('sender-row').first()).toBeVisible({ timeout: 20_000 });
    await expect(page.getByTestId('sender-row')).toHaveCount(5);
  });
});
