import type { Page, Route } from '@playwright/test';

/**
 * A fake Gmail, good enough to drive the real app end to end.
 *
 * Everything the app touches goes through here: the Google Identity Services
 * global, the profile/list/batch read path, and the batchModify / labels /
 * filters write path. Calls are recorded so tests can assert on what the app
 * actually asked Gmail to do.
 */

export interface FakeMessage {
  id: string;
  from: string;
  subject?: string;
  size?: number;
  /** Days before "now". */
  ageDays?: number;
  labels?: string[];
  listUnsubscribe?: string;
  oneClick?: boolean;
}

export interface RecordedCalls {
  batchModify: { ids: string[]; addLabelIds?: string[]; removeLabelIds?: string[] }[];
  filters: { criteria: { from?: string }; action: Record<string, unknown> }[];
  labelsCreated: string[];
  unsubscribePosts: string[];
}

export const SAMPLE_MESSAGES: FakeMessage[] = [
  // A newsletter with one-click unsubscribe, and the biggest group.
  { id: 'm1', from: 'Acme Weekly <news@acme.com>', size: 40_000, ageDays: 1, labels: ['INBOX', 'UNREAD'], listUnsubscribe: '<https://acme.com/u/1>', oneClick: true },
  { id: 'm2', from: 'Acme Weekly <news@acme.com>', size: 42_000, ageDays: 8, labels: ['INBOX'], listUnsubscribe: '<https://acme.com/u/1>', oneClick: true },
  { id: 'm3', from: 'Acme Weekly <news@acme.com>', size: 38_000, ageDays: 15, labels: ['INBOX', 'UNREAD'], listUnsubscribe: '<https://acme.com/u/1>', oneClick: true },
  // A second address on the same domain, for the by-domain grouping test.
  { id: 'm4', from: 'Acme Billing <billing@acme.com>', size: 9_000, ageDays: 3, labels: ['INBOX'] },
  // A large single message, for the sort-by-size test.
  { id: 'm5', from: 'Photos <share@bigpix.io>', size: 5_000_000, ageDays: 30, labels: ['INBOX'] },
  // A sender with a mailto-only unsubscribe.
  { id: 'm6', from: 'Old List <list@legacy.org>', size: 12_000, ageDays: 400, labels: ['INBOX'], listUnsubscribe: '<mailto:stop@legacy.org>' },
  // A plain human sender with no unsubscribe at all.
  { id: 'm7', from: 'Jane Doe <jane@friend.net>', size: 3_000, ageDays: 2, labels: ['INBOX', 'UNREAD'] },
];

const DAY = 86_400_000;

function toApiMessage(m: FakeMessage) {
  const headers = [
    { name: 'From', value: m.from },
    { name: 'Subject', value: m.subject ?? 'Subject' },
  ];
  if (m.listUnsubscribe) {
    headers.push({ name: 'List-Unsubscribe', value: m.listUnsubscribe });
    if (m.oneClick) {
      headers.push({ name: 'List-Unsubscribe-Post', value: 'List-Unsubscribe=One-Click' });
    }
  }
  return {
    id: m.id,
    threadId: `t-${m.id}`,
    labelIds: m.labels ?? ['INBOX'],
    sizeEstimate: m.size ?? 1000,
    internalDate: String(Date.now() - (m.ageDays ?? 0) * DAY),
    payload: { headers },
  };
}

/** Build a multipart/mixed batch response in Gmail's exact shape. */
function buildBatchResponse(messages: FakeMessage[]): { body: string; contentType: string } {
  const boundary = 'batch_test_boundary';
  const parts = messages
    .map(
      (m, i) =>
        `--${boundary}\r\n` +
        'Content-Type: application/http\r\n' +
        `Content-ID: <response-item-${i}>\r\n\r\n` +
        'HTTP/1.1 200 OK\r\n' +
        'Content-Type: application/json; charset=UTF-8\r\n\r\n' +
        `${JSON.stringify(toApiMessage(m))}\r\n`,
    )
    .join('');

  return {
    body: `${parts}--${boundary}--\r\n`,
    contentType: `multipart/mixed; boundary=${boundary}`,
  };
}

/**
 * Install the GIS stub. Silent auth (`prompt: ''`) fails until an interactive
 * grant has happened, mirroring how Google actually behaves on a first visit —
 * that keeps the sign-in screen reachable in tests.
 */
export async function stubGoogleAuth(page: Page) {
  await page.addInitScript(() => {
    let granted = false;
    (window as unknown as { google: unknown }).google = {
      accounts: {
        oauth2: {
          initTokenClient(config: {
            callback: (r: Record<string, unknown>) => void;
            error_callback?: (e: Record<string, unknown>) => void;
          }) {
            return {
              requestAccessToken(overrides?: { prompt?: string }) {
                const silent = overrides?.prompt === '';
                if (silent && !granted) {
                  config.error_callback?.({ type: 'popup_failed_to_open', message: 'no session' });
                  return;
                }
                granted = true;
                config.callback({ access_token: 'fake-token-123', expires_in: 3600 });
              },
            };
          },
          revoke() {
            granted = false;
          },
        },
      },
    };
  });
}

/** Route every Gmail endpoint the app uses, and record the writes. */
export async function stubGmailApi(
  page: Page,
  messages: FakeMessage[] = SAMPLE_MESSAGES,
): Promise<RecordedCalls> {
  const calls: RecordedCalls = {
    batchModify: [],
    filters: [],
    labelsCreated: [],
    unsubscribePosts: [],
  };

  const json = (route: Route, body: unknown, status = 200) =>
    route.fulfill({ status, contentType: 'application/json', body: JSON.stringify(body) });

  await page.route('**/gmail/v1/users/me/profile*', (route) =>
    json(route, { emailAddress: 'tester@example.com', messagesTotal: messages.length, threadsTotal: messages.length }),
  );

  await page.route('**/gmail/v1/users/me/messages?*', (route) =>
    json(route, { messages: messages.map((m) => ({ id: m.id })) }),
  );

  // Individual message GETs, used when the batch endpoint is unavailable.
  //
  // Registered before the specific routes below, because Playwright gives the
  // most recently added route priority — otherwise this catch-all would
  // swallow `messages/batchModify`.
  //
  // A regex rather than a glob: these URLs carry `fields=...,payload/headers`,
  // and a glob `*` will not match across the literal `/` in the query string,
  // which would let the request escape to the real network.
  await page.route(/\/gmail\/v1\/users\/me\/messages\/[^/?]+\?/, async (route) => {
    const id = new URL(route.request().url()).pathname.split('/').pop() ?? '';
    const found = messages.find((m) => m.id === id);
    if (!found) return json(route, { error: { message: 'Not Found' } }, 404);
    return json(route, toApiMessage(found));
  });

  await page.route('**/batch/gmail/v1', async (route) => {
    // Reply with exactly the messages this batch asked for.
    const requested = [...(route.request().postData() ?? '').matchAll(/messages\/([^?]+)\?/g)].map(
      (m) => decodeURIComponent(m[1]),
    );
    const wanted = messages.filter((m) => requested.includes(m.id));
    const { body, contentType } = buildBatchResponse(wanted.length ? wanted : messages);
    await route.fulfill({ status: 200, contentType, body });
  });

  await page.route('**/gmail/v1/users/me/messages/batchModify', async (route) => {
    calls.batchModify.push(JSON.parse(route.request().postData() ?? '{}'));
    await route.fulfill({ status: 204, body: '' });
  });

  await page.route('**/gmail/v1/users/me/labels', async (route) => {
    if (route.request().method() === 'POST') {
      const created = JSON.parse(route.request().postData() ?? '{}') as { name: string };
      calls.labelsCreated.push(created.name);
      return json(route, { id: `Label_${calls.labelsCreated.length}`, name: created.name });
    }
    return json(route, { labels: [{ id: 'Label_existing', name: 'Bulk', type: 'user' }] });
  });

  await page.route('**/gmail/v1/users/me/settings/filters*', async (route) => {
    if (route.request().method() === 'POST') {
      const spec = JSON.parse(route.request().postData() ?? '{}');
      calls.filters.push(spec);
      return json(route, { id: `filter_${calls.filters.length}`, ...spec });
    }
    return json(route, { filter: [] });
  });

  // Catch outbound one-click unsubscribe POSTs.
  await page.route('https://acme.com/**', async (route) => {
    calls.unsubscribePosts.push(route.request().url());
    await route.fulfill({ status: 200, body: 'ok' });
  });

  return calls;
}

/** Pre-seed the OAuth client ID so tests skip the setup screen. */
export async function seedClientId(page: Page, clientId = 'test.apps.googleusercontent.com') {
  await page.addInitScript((id) => {
    localStorage.setItem('mailstrom.clientId', id);
  }, clientId);
}
