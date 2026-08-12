/**
 * IndexedDB cache of scanned message metadata, keyed by mailbox address.
 *
 * Scanning a large mailbox costs thousands of API calls, so we never want to
 * do it twice. On a re-scan we ask Gmail for the current message ID list
 * (cheap: 500 IDs per request) and only fetch metadata for IDs we haven't
 * seen, then drop cached entries whose IDs are gone.
 */

import type { MessageMeta } from './parse';

const DB_NAME = 'mailstrom';
const DB_VERSION = 1;
const STORE = 'messages';

function open(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, DB_VERSION);
    req.onupgradeneeded = () => {
      const db = req.result;
      if (!db.objectStoreNames.contains(STORE)) {
        // Key is `${account}:${messageId}` so several mailboxes can coexist.
        const store = db.createObjectStore(STORE, { keyPath: 'key' });
        store.createIndex('account', 'account', { unique: false });
      }
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error ?? new Error('Could not open the local cache.'));
  });
}

interface CacheRow {
  key: string;
  account: string;
  meta: MessageMeta;
}

const rowKey = (account: string, id: string) => `${account}:${id}`;

function tx(db: IDBDatabase, mode: IDBTransactionMode) {
  return db.transaction(STORE, mode).objectStore(STORE);
}

function promisify<T>(req: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

/** Every cached message for a mailbox. */
export async function loadCached(account: string): Promise<MessageMeta[]> {
  try {
    const db = await open();
    const store = tx(db, 'readonly');
    const rows = await promisify(store.index('account').getAll(account) as IDBRequest<CacheRow[]>);
    db.close();
    return rows.map((r) => r.meta);
  } catch (err) {
    console.warn('Cache read failed; continuing without it', err);
    return [];
  }
}

/** Insert or update metadata rows. */
export async function saveCached(account: string, metas: MessageMeta[]): Promise<void> {
  if (!metas.length) return;
  try {
    const db = await open();
    const transaction = db.transaction(STORE, 'readwrite');
    const store = transaction.objectStore(STORE);
    for (const meta of metas) {
      store.put({ key: rowKey(account, meta.id), account, meta } satisfies CacheRow);
    }
    await new Promise<void>((resolve, reject) => {
      transaction.oncomplete = () => resolve();
      transaction.onerror = () => reject(transaction.error);
      transaction.onabort = () => reject(transaction.error);
    });
    db.close();
  } catch (err) {
    // A failed cache write costs a slower next scan, nothing more.
    console.warn('Cache write failed', err);
  }
}

/** Forget specific messages (deleted upstream, or acted on locally). */
export async function removeCached(account: string, ids: string[]): Promise<void> {
  if (!ids.length) return;
  try {
    const db = await open();
    const transaction = db.transaction(STORE, 'readwrite');
    const store = transaction.objectStore(STORE);
    for (const id of ids) store.delete(rowKey(account, id));
    await new Promise<void>((resolve, reject) => {
      transaction.oncomplete = () => resolve();
      transaction.onerror = () => reject(transaction.error);
    });
    db.close();
  } catch (err) {
    console.warn('Cache delete failed', err);
  }
}

/** Drop an entire mailbox's cache. */
export async function clearAccount(account: string): Promise<void> {
  const cached = await loadCached(account);
  await removeCached(account, cached.map((m) => m.id));
}
