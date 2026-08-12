/**
 * Scan orchestration: list IDs, reconcile against the cache, fetch only what's
 * missing, persist the result.
 */

import { GmailClient } from './gmail';
import { loadCached, removeCached, saveCached } from './cache';
import { toMeta, type MessageMeta } from './parse';

export type ScanPhase = 'idle' | 'listing' | 'fetching' | 'done' | 'error' | 'cancelled';

export interface ScanProgress {
  phase: ScanPhase;
  /** Messages whose metadata we have. */
  done: number;
  /** Messages we intend to fetch this run (0 while still listing). */
  total: number;
  /** IDs enumerated so far during the listing phase. */
  listed: number;
  /** Messages served from cache rather than the network. */
  fromCache: number;
  message?: string;
}

export interface ScanOptions {
  /** Gmail search query. Empty string means the whole mailbox. */
  query: string;
  onProgress?: (progress: ScanProgress) => void;
  signal?: AbortSignal;
  /** Skip the cache and refetch everything. */
  force?: boolean;
}

/**
 * Run a scan and return every message's metadata.
 *
 * Incremental by default: cached entries are reused, IDs that vanished from
 * the mailbox are evicted, and only genuinely new IDs cost API calls.
 */
export async function scanMailbox(
  client: GmailClient,
  account: string,
  opts: ScanOptions,
): Promise<MessageMeta[]> {
  const { query, onProgress, signal, force = false } = opts;

  const progress: ScanProgress = {
    phase: 'listing',
    done: 0,
    total: 0,
    listed: 0,
    fromCache: 0,
  };
  const report = (patch: Partial<ScanProgress>) => {
    Object.assign(progress, patch);
    onProgress?.({ ...progress });
  };

  report({ phase: 'listing', message: 'Counting messages…' });

  const ids = await client.listMessageIds(query, (listed) => {
    report({ listed, message: `Found ${listed.toLocaleString()} messages…` });
    return !signal?.aborted;
  });

  if (signal?.aborted) {
    report({ phase: 'cancelled' });
    return [];
  }

  const liveIds = new Set(ids);

  // Reconcile with what we already know.
  const cached = force ? [] : await loadCached(account);
  const cachedById = new Map(cached.map((m) => [m.id, m]));

  const stale = cached.filter((m) => !liveIds.has(m.id)).map((m) => m.id);
  if (stale.length) await removeCached(account, stale);

  const reused: MessageMeta[] = [];
  const missing: string[] = [];
  for (const id of ids) {
    const hit = cachedById.get(id);
    if (hit) reused.push(hit);
    else missing.push(id);
  }

  report({
    phase: 'fetching',
    total: missing.length,
    fromCache: reused.length,
    done: 0,
    message: missing.length
      ? `Fetching ${missing.length.toLocaleString()} new messages…`
      : 'Up to date.',
  });

  const fetched: MessageMeta[] = [];
  if (missing.length) {
    // Persist in slices so a cancelled or crashed scan still banks its work.
    const PERSIST_EVERY = 500;
    let unsaved: MessageMeta[] = [];

    const raw = await client.getMessagesMetadata(missing, {
      signal,
      onProgress: (done) => report({ done, message: `Fetched ${done.toLocaleString()} of ${missing.length.toLocaleString()}…` }),
    });

    for (const r of raw) {
      const meta = toMeta(r);
      fetched.push(meta);
      unsaved.push(meta);
      if (unsaved.length >= PERSIST_EVERY) {
        await saveCached(account, unsaved);
        unsaved = [];
      }
    }
    if (unsaved.length) await saveCached(account, unsaved);
  }

  if (signal?.aborted) {
    report({ phase: 'cancelled' });
    // Partial results are still useful — hand back what we got.
    return [...reused, ...fetched];
  }

  report({ phase: 'done', message: undefined });
  return [...reused, ...fetched];
}
