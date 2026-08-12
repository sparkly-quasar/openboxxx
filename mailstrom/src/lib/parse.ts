/**
 * Pure parsing helpers for Gmail message metadata.
 *
 * Everything in here is deliberately side-effect free so it can be unit tested
 * without a network or a browser.
 */

export interface GmailHeader {
  name: string;
  value: string;
}

export interface RawMessage {
  id: string;
  threadId?: string;
  labelIds?: string[];
  sizeEstimate?: number;
  internalDate?: string;
  payload?: { headers?: GmailHeader[] };
}

/** A message reduced to just the fields the app reasons about. */
export interface MessageMeta {
  id: string;
  /** Lower-cased sender address, e.g. `news@example.com`. */
  email: string;
  /** Display name from the From header, or the address if there wasn't one. */
  name: string;
  /** Registrable-ish domain of the sender, e.g. `example.com`. */
  domain: string;
  subject: string;
  /** Bytes, as estimated by Gmail. */
  size: number;
  /** Epoch millis. */
  date: number;
  unread: boolean;
  inInbox: boolean;
  unsubscribe: UnsubscribeInfo | null;
}

export interface UnsubscribeInfo {
  /** HTTPS endpoint, if the sender offered one. */
  http: string | null;
  /** mailto: URI, if the sender offered one. */
  mailto: string | null;
  /** True when the sender advertises RFC 8058 one-click unsubscribe. */
  oneClick: boolean;
}

/** Case-insensitive header lookup. Returns `''` when absent. */
export function header(headers: GmailHeader[] | undefined, name: string): string {
  if (!headers) return '';
  const wanted = name.toLowerCase();
  for (const h of headers) {
    if (h.name?.toLowerCase() === wanted) return h.value ?? '';
  }
  return '';
}

/**
 * Pull the address and display name out of a From header.
 *
 * Handles the shapes that actually turn up in a real mailbox:
 *   `Name <a@b.com>`, `"Last, First" <a@b.com>`, `a@b.com`,
 *   `<a@b.com>`, `Name <a@b.com> (comment)`.
 */
export function parseFrom(value: string): { email: string; name: string } {
  const raw = (value ?? '').trim();
  if (!raw) return { email: '', name: '' };

  const angle = raw.lastIndexOf('<');
  if (angle !== -1) {
    const close = raw.indexOf('>', angle);
    const email = raw.slice(angle + 1, close === -1 ? undefined : close).trim().toLowerCase();
    let name = raw.slice(0, angle).trim();
    // Strip surrounding quotes and unescape the few escapes RFC 5322 allows.
    if (name.startsWith('"') && name.endsWith('"') && name.length > 1) {
      name = name.slice(1, -1).replace(/\\(.)/g, '$1');
    }
    return { email, name: name || email };
  }

  const email = raw.toLowerCase();
  return { email, name: email };
}

/** Everything after the `@`, lower-cased. Empty string if there's no `@`. */
export function domainOf(email: string): string {
  const at = email.lastIndexOf('@');
  return at === -1 ? '' : email.slice(at + 1).toLowerCase();
}

/**
 * Parse `List-Unsubscribe` (RFC 2369) plus `List-Unsubscribe-Post` (RFC 8058).
 *
 * The header is a comma-separated list of angle-bracketed URIs. We keep at most
 * one HTTPS and one mailto target, preferring the first of each.
 */
export function parseUnsubscribe(
  listUnsubscribe: string,
  listUnsubscribePost = '',
): UnsubscribeInfo | null {
  if (!listUnsubscribe?.trim()) return null;

  let http: string | null = null;
  let mailto: string | null = null;

  for (const match of listUnsubscribe.matchAll(/<([^>]+)>/g)) {
    const uri = match[1].trim();
    if (!http && /^https:\/\//i.test(uri)) http = uri;
    else if (!mailto && /^mailto:/i.test(uri)) mailto = uri;
  }

  // Some senders omit the angle brackets entirely.
  if (!http && !mailto) {
    const bare = listUnsubscribe.trim();
    if (/^https:\/\//i.test(bare)) http = bare;
    else if (/^mailto:/i.test(bare)) mailto = bare;
  }

  if (!http && !mailto) return null;

  // One-click is only meaningful over HTTPS.
  const oneClick = !!http && /one-click/i.test(listUnsubscribePost ?? '');
  return { http, mailto, oneClick };
}

/** Reduce a raw Gmail API message to a {@link MessageMeta}. */
export function toMeta(raw: RawMessage): MessageMeta {
  const headers = raw.payload?.headers;
  const { email, name } = parseFrom(header(headers, 'From'));
  const labels = raw.labelIds ?? [];

  return {
    id: raw.id,
    email,
    name,
    domain: domainOf(email),
    subject: header(headers, 'Subject'),
    size: raw.sizeEstimate ?? 0,
    date: Number(raw.internalDate ?? 0),
    unread: labels.includes('UNREAD'),
    inInbox: labels.includes('INBOX'),
    unsubscribe: parseUnsubscribe(
      header(headers, 'List-Unsubscribe'),
      header(headers, 'List-Unsubscribe-Post'),
    ),
  };
}

/** Human-readable byte count. */
export function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  const units = ['KB', 'MB', 'GB', 'TB'];
  let value = bytes / 1024;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return `${value >= 10 || Number.isInteger(value) ? Math.round(value) : value.toFixed(1)} ${units[unit]}`;
}

/** Compact relative age, e.g. `3d`, `2mo`, `4y`. */
export function formatAge(epochMs: number, now = Date.now()): string {
  const seconds = Math.max(0, (now - epochMs) / 1000);
  if (seconds < 60) return 'now';
  const minutes = seconds / 60;
  if (minutes < 60) return `${Math.floor(minutes)}m`;
  const hours = minutes / 60;
  if (hours < 24) return `${Math.floor(hours)}h`;
  // Each bucket hands over only once the next one reads as at least 1, so a
  // 30-day-old message shows "30d" rather than flooring to "0mo".
  const days = hours / 24;
  const months = days / 30.44;
  if (months < 1) return `${Math.floor(days)}d`;
  const years = days / 365.25;
  if (years < 1) return `${Math.floor(months)}mo`;
  return `${Math.floor(years)}y`;
}
