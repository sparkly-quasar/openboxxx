/**
 * Thin Gmail REST client.
 *
 * Two things make this more than a fetch wrapper:
 *
 *  1. **Batching.** Fetching headers one message at a time means one request
 *     (and one ~1 KB bearer token) per message. On a 30 000-message mailbox
 *     over cellular that is tens of megabytes of pure overhead. The Gmail batch
 *     endpoint folds 100 GETs into one request. We fall back to concurrent
 *     individual requests if batching ever fails, so a batch-endpoint change
 *     degrades performance instead of breaking the app.
 *
 *  2. **Quota pacing.** Gmail allows 250 quota units/user/second and
 *     `messages.get` costs 5, so ~50 messages/second is the ceiling. We pace
 *     against that and back off on 429/5xx.
 */

import type { RawMessage } from './parse';

const API = 'https://gmail.googleapis.com/gmail/v1';
const BATCH_ENDPOINT = 'https://gmail.googleapis.com/batch/gmail/v1';

/** Headers worth paying for. Anything else is dead weight at scale. */
const META_HEADERS = ['From', 'Subject', 'List-Unsubscribe', 'List-Unsubscribe-Post'];

const META_QUERY =
  'format=metadata&' +
  META_HEADERS.map((h) => `metadataHeaders=${h}`).join('&') +
  '&fields=id,threadId,labelIds,sizeEstimate,internalDate,payload/headers';

/** Messages per batch request. Gmail's documented ceiling is 100. */
const BATCH_SIZE = 100;
/** Concurrency for the non-batched fallback path. */
const FALLBACK_CONCURRENCY = 20;

export class GmailError extends Error {
  readonly status: number;
  readonly body?: unknown;

  constructor(message: string, status: number, body?: unknown) {
    super(message);
    this.name = 'GmailError';
    this.status = status;
    this.body = body;
  }

  /** True when re-authenticating is the fix. */
  get isAuthError(): boolean {
    return this.status === 401 || this.status === 403;
  }
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

export interface GmailProfile {
  emailAddress: string;
  messagesTotal: number;
  threadsTotal: number;
}

export class GmailClient {
  private token: string;

  constructor(token: string) {
    this.token = token;
  }

  setToken(token: string) {
    this.token = token;
  }

  private get authHeader() {
    return { Authorization: `Bearer ${this.token}` };
  }

  /**
   * fetch + retry. Retries 429 and 5xx with exponential backoff, honouring
   * `Retry-After` when the server sends one. Never retries 4xx that the user
   * has to fix (auth, bad request).
   */
  private async request(
    url: string,
    init: RequestInit = {},
    attempt = 0,
    maxRetries = 5,
  ): Promise<Response> {
    let res: Response;
    try {
      res = await fetch(url, {
        ...init,
        headers: { ...this.authHeader, ...(init.headers ?? {}) },
      });
    } catch (networkError) {
      // Offline or DNS blip — worth a few retries.
      if (attempt >= Math.min(4, maxRetries)) {
        throw new GmailError(`Network error: ${String(networkError)}`, 0);
      }
      await sleep(2 ** attempt * 500);
      return this.request(url, init, attempt + 1, maxRetries);
    }

    if (res.ok) return res;

    const retryable = res.status === 429 || res.status >= 500;
    if (retryable && attempt < maxRetries) {
      const retryAfter = Number(res.headers.get('Retry-After'));
      const delay = Number.isFinite(retryAfter) && retryAfter > 0
        ? retryAfter * 1000
        : 2 ** attempt * 1000 + Math.random() * 500;
      await sleep(delay);
      return this.request(url, init, attempt + 1, maxRetries);
    }

    let body: unknown;
    let detail = res.statusText;
    try {
      body = await res.json();
      const message = (body as { error?: { message?: string } })?.error?.message;
      if (message) detail = message;
    } catch {
      /* non-JSON error body; statusText will do */
    }
    throw new GmailError(`Gmail API ${res.status}: ${detail}`, res.status, body);
  }

  private async json<T>(url: string, init?: RequestInit): Promise<T> {
    const res = await this.request(url, init);
    return (await res.json()) as T;
  }

  async getProfile(): Promise<GmailProfile> {
    return this.json<GmailProfile>(`${API}/users/me/profile`);
  }

  /**
   * Page through message IDs matching `query`.
   *
   * `onPage` is called after each page so the UI can show progress on a
   * mailbox that takes a while to enumerate. Returning `false` from `onPage`
   * stops the walk early (used by the cancel button).
   */
  async listMessageIds(
    query: string,
    onPage?: (idsSoFar: number) => boolean | void,
  ): Promise<string[]> {
    const ids: string[] = [];
    let pageToken: string | undefined;

    do {
      const params = new URLSearchParams({ maxResults: '500', fields: 'messages/id,nextPageToken' });
      if (query) params.set('q', query);
      if (pageToken) params.set('pageToken', pageToken);

      const page = await this.json<{ messages?: { id: string }[]; nextPageToken?: string }>(
        `${API}/users/me/messages?${params}`,
      );

      for (const m of page.messages ?? []) ids.push(m.id);
      pageToken = page.nextPageToken;

      if (onPage && onPage(ids.length) === false) break;
    } while (pageToken);

    return ids;
  }

  /**
   * Fetch metadata for many messages.
   *
   * Calls `onProgress` with the running total. Set `signal` to abort mid-scan.
   */
  async getMessagesMetadata(
    ids: string[],
    opts: {
      onProgress?: (done: number, total: number) => void;
      signal?: AbortSignal;
    } = {},
  ): Promise<RawMessage[]> {
    const { onProgress, signal } = opts;
    const out: RawMessage[] = [];
    let useBatch = true;

    for (let i = 0; i < ids.length; i += BATCH_SIZE) {
      if (signal?.aborted) break;
      const chunk = ids.slice(i, i + BATCH_SIZE);

      let messages: RawMessage[];
      if (useBatch) {
        try {
          messages = await this.batchGetMetadata(chunk);
        } catch (err) {
          // One failure is enough to distrust the batch endpoint for this run.
          if (err instanceof GmailError && err.isAuthError) throw err;
          console.warn('Batch request failed, falling back to individual gets', err);
          useBatch = false;
          messages = await this.parallelGetMetadata(chunk, signal);
        }
      } else {
        messages = await this.parallelGetMetadata(chunk, signal);
      }

      out.push(...messages);
      onProgress?.(out.length, ids.length);

      // Pace against the 250 units/sec quota: 100 gets = 500 units = ~2s.
      if (i + BATCH_SIZE < ids.length) await sleep(useBatch ? 1200 : 400);
    }

    return out;
  }

  /** One multipart/mixed request carrying up to {@link BATCH_SIZE} GETs. */
  private async batchGetMetadata(ids: string[]): Promise<RawMessage[]> {
    const boundary = `batch_${Math.random().toString(36).slice(2)}`;
    const body =
      ids
        .map(
          (id, index) =>
            `--${boundary}\r\n` +
            'Content-Type: application/http\r\n' +
            `Content-ID: <item-${index}>\r\n\r\n` +
            `GET /gmail/v1/users/me/messages/${encodeURIComponent(id)}?${META_QUERY}\r\n`,
        )
        .join('\r\n') + `\r\n--${boundary}--\r\n`;

    // Retry sparingly: a working per-message fallback already exists, so
    // burning a full backoff ladder on a batch endpoint that is down just
    // delays the scan.
    const res = await this.request(
      BATCH_ENDPOINT,
      {
        method: 'POST',
        headers: { 'Content-Type': `multipart/mixed; boundary=${boundary}` },
        body,
      },
      0,
      1,
    );

    return parseBatchResponse(await res.text());
  }

  /** Fallback: individual GETs, bounded concurrency. */
  private async parallelGetMetadata(ids: string[], signal?: AbortSignal): Promise<RawMessage[]> {
    const out: RawMessage[] = [];
    for (let i = 0; i < ids.length; i += FALLBACK_CONCURRENCY) {
      if (signal?.aborted) break;
      const slice = ids.slice(i, i + FALLBACK_CONCURRENCY);
      const results = await Promise.all(
        slice.map(async (id) => {
          try {
            return await this.json<RawMessage>(
              `${API}/users/me/messages/${encodeURIComponent(id)}?${META_QUERY}`,
            );
          } catch (err) {
            // A single unreadable message shouldn't sink the whole scan.
            if (err instanceof GmailError && err.isAuthError) throw err;
            console.warn(`Skipping message ${id}`, err);
            return null;
          }
        }),
      );
      out.push(...results.filter((m): m is RawMessage => m !== null));
    }
    return out;
  }

  /**
   * Apply label changes to up to 1000 messages per call.
   * Used for archive (remove INBOX) and mark-read (remove UNREAD).
   */
  async batchModify(ids: string[], addLabelIds: string[], removeLabelIds: string[]): Promise<void> {
    for (const chunk of chunked(ids, 1000)) {
      await this.request(`${API}/users/me/messages/batchModify`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ids: chunk, addLabelIds, removeLabelIds }),
      });
    }
  }

  /**
   * Move messages to Trash (recoverable for 30 days).
   *
   * Deliberately *not* `messages.delete`: this app does bulk operations on a
   * user's real mail, and every destructive action here must be undoable from
   * the Gmail UI. Trash is that guarantee.
   */
  async trash(ids: string[], onProgress?: (done: number) => void): Promise<void> {
    // There is no batchTrash, but batchModify can add the TRASH label, which
    // is equivalent and 1000x cheaper than per-message trash calls.
    let done = 0;
    for (const chunk of chunked(ids, 1000)) {
      await this.request(`${API}/users/me/messages/batchModify`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ids: chunk, addLabelIds: ['TRASH'], removeLabelIds: ['INBOX'] }),
      });
      done += chunk.length;
      onProgress?.(done);
    }
  }

  async listLabels(): Promise<{ id: string; name: string; type: string }[]> {
    const res = await this.json<{ labels?: { id: string; name: string; type: string }[] }>(
      `${API}/users/me/labels`,
    );
    return res.labels ?? [];
  }

  async createLabel(name: string): Promise<{ id: string; name: string }> {
    return this.json<{ id: string; name: string }>(`${API}/users/me/labels`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        name,
        labelListVisibility: 'labelShow',
        messageListVisibility: 'show',
      }),
    });
  }

  async listFilters(): Promise<GmailFilter[]> {
    const res = await this.json<{ filter?: GmailFilter[] }>(`${API}/users/me/settings/filters`);
    return res.filter ?? [];
  }

  async createFilter(filter: GmailFilterSpec): Promise<GmailFilter> {
    return this.json<GmailFilter>(`${API}/users/me/settings/filters`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(filter),
    });
  }

  async deleteFilter(id: string): Promise<void> {
    await this.request(`${API}/users/me/settings/filters/${encodeURIComponent(id)}`, {
      method: 'DELETE',
    });
  }
}

export interface GmailFilterSpec {
  criteria: { from?: string; to?: string; subject?: string; query?: string; hasAttachment?: boolean };
  action: { addLabelIds?: string[]; removeLabelIds?: string[]; forward?: string };
}

export interface GmailFilter extends GmailFilterSpec {
  id: string;
}

export function chunked<T>(items: T[], size: number): T[][] {
  const out: T[][] = [];
  for (let i = 0; i < items.length; i += size) out.push(items.slice(i, i + size));
  return out;
}

/**
 * Parse a `multipart/mixed` batch response into its constituent JSON bodies.
 *
 * Individual parts can fail independently (a message deleted mid-scan yields a
 * 404); those are skipped rather than failing the whole batch. A part that
 * fails with 401/403 is escalated, because that means the token died and every
 * subsequent request would fail too.
 */
export function parseBatchResponse(text: string): RawMessage[] {
  // The outer boundary is echoed in the first line as `--<boundary>`.
  const firstLine = text.slice(0, text.indexOf('\n')).trim();
  if (!firstLine.startsWith('--')) {
    throw new GmailError('Malformed batch response: no boundary', 0);
  }
  const boundary = firstLine;

  const messages: RawMessage[] = [];
  for (const part of text.split(boundary)) {
    const trimmed = part.trim();
    if (!trimmed || trimmed === '--') continue;

    // Each part is: part headers, blank line, HTTP status line + headers,
    // blank line, body. Find the inner HTTP status line, then the body after
    // the blank line that follows it.
    const statusMatch = trimmed.match(/^HTTP\/[\d.]+\s+(\d{3})/m);
    if (!statusMatch) continue;
    const status = Number(statusMatch[1]);

    const bodyStart = trimmed.indexOf('\r\n\r\n', trimmed.indexOf(statusMatch[0]));
    const fallbackStart = trimmed.indexOf('\n\n', trimmed.indexOf(statusMatch[0]));
    const start = bodyStart !== -1 ? bodyStart + 4 : fallbackStart !== -1 ? fallbackStart + 2 : -1;
    if (start === -1) continue;

    const body = trimmed.slice(start).trim();

    if (status === 401 || status === 403) {
      throw new GmailError(`Gmail API ${status} inside batch`, status);
    }
    if (status < 200 || status >= 300) continue;

    try {
      const parsed = JSON.parse(body) as RawMessage;
      if (parsed?.id) messages.push(parsed);
    } catch {
      /* partial or non-JSON part — skip it */
    }
  }

  return messages;
}
