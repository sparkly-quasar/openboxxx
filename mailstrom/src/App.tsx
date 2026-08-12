import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { GmailClient, GmailError } from './lib/gmail';
import { clearSession, ensureToken, requestToken, revoke, type Session } from './lib/auth';
import { scanMailbox, type ScanProgress } from './lib/scan';
import { filterGroups, groupMessages, sortGroups, totals, type GroupBy, type SortBy } from './lib/group';
import { formatSize, type MessageMeta } from './lib/parse';
import {
  applyBulkAction,
  createFilterFromPlan,
  unsubscribe as runUnsubscribe,
  type BulkAction,
  type FilterPlan,
} from './lib/actions';
import { clearAccount } from './lib/cache';
import { SenderRow } from './components/SenderRow';
import { SenderSheet } from './components/SenderSheet';
import { SetupScreen } from './components/SetupScreen';

const CLIENT_ID_KEY = 'mailstrom.clientId';
const BUILD_TIME_CLIENT_ID = (import.meta.env.VITE_GOOGLE_CLIENT_ID as string | undefined) ?? '';

/** Preset mailbox scopes, as Gmail search queries. */
const SCOPES_QUERY: { id: string; label: string; query: string }[] = [
  { id: 'inbox', label: 'Inbox', query: 'in:inbox' },
  { id: 'all', label: 'All mail', query: '' },
  { id: 'unread', label: 'Unread', query: 'is:unread' },
  { id: 'old', label: 'Over 1y old', query: 'older_than:1y' },
  { id: 'big', label: 'Large', query: 'larger:1m' },
  { id: 'lists', label: 'Newsletters', query: 'category:promotions OR category:updates OR category:forums' },
];

const SORTS: { id: SortBy; label: string }[] = [
  { id: 'count', label: 'Most mail' },
  { id: 'size', label: 'Biggest' },
  { id: 'unread', label: 'Unread' },
  { id: 'recent', label: 'Recent' },
];

export default function App() {
  const [clientId, setClientId] = useState<string>(
    () => BUILD_TIME_CLIENT_ID || localStorage.getItem(CLIENT_ID_KEY) || '',
  );
  const [session, setSession] = useState<Session | null>(null);
  const [account, setAccount] = useState<string>('');
  const [messages, setMessages] = useState<MessageMeta[]>([]);
  const [progress, setProgress] = useState<ScanProgress | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [selectedKey, setSelectedKey] = useState<string | null>(null);

  const [scopeId, setScopeId] = useState('inbox');
  const [groupBy, setGroupBy] = useState<GroupBy>('sender');
  const [sortBy, setSortBy] = useState<SortBy>('count');
  const [query, setQuery] = useState('');

  const abort = useRef<AbortController | null>(null);
  const client = useMemo(() => (session ? new GmailClient(session.token) : null), [session]);

  /* ---------- auth ---------- */

  // Try a silent sign-in on load so a returning user lands straight in the app.
  useEffect(() => {
    if (!clientId || session) return;
    let cancelled = false;
    ensureToken(clientId)
      .then((s) => {
        if (!cancelled) setSession(s);
      })
      .catch(() => {
        /* expected on first visit — the user taps sign in */
      });
    return () => {
      cancelled = true;
    };
  }, [clientId, session]);

  // Resolve which mailbox we're looking at; the cache is keyed on it.
  useEffect(() => {
    if (!client) return;
    let cancelled = false;
    client
      .getProfile()
      .then((p) => {
        if (!cancelled) setAccount(p.emailAddress);
      })
      .catch((err) => {
        if (!cancelled) setError(explain(err));
      });
    return () => {
      cancelled = true;
    };
  }, [client]);

  const signIn = async () => {
    setError(null);
    try {
      setSession(await requestToken(clientId, true));
    } catch (err) {
      setError(explain(err));
    }
  };

  const signOut = () => {
    if (session) revoke(session.token);
    clearSession();
    setSession(null);
    setMessages([]);
    setAccount('');
    setProgress(null);
  };

  /* ---------- scanning ---------- */

  const scan = useCallback(
    async (force = false) => {
      if (!client || !account) return;
      setError(null);
      abort.current?.abort();
      const controller = new AbortController();
      abort.current = controller;

      const scopeQuery = SCOPES_QUERY.find((s) => s.id === scopeId)?.query ?? '';

      try {
        const result = await scanMailbox(client, account, {
          query: scopeQuery,
          force,
          signal: controller.signal,
          onProgress: (p) => {
            // Ignore progress from a scan that has since been superseded.
            if (abort.current === controller) setProgress(p);
          },
        });

        // Only the newest scan may publish results. A superseded scan — from
        // StrictMode's double-invoked effect, a scope change, or a rapid
        // rescan — still resolves with whatever it had, and applying that
        // would clobber newer state (including messages the user just
        // trashed). Pressing Stop leaves this scan current, so its partial
        // results are still applied, which is what the button should do.
        if (abort.current !== controller) return;
        setMessages(result);
      } catch (err) {
        if (abort.current !== controller) return;
        setError(explain(err));
        setProgress((p) => (p ? { ...p, phase: 'error' } : null));
        if (err instanceof GmailError && err.isAuthError) {
          clearSession();
          setSession(null);
        }
      }
    },
    [client, account, scopeId],
  );

  // Kick off a scan whenever the mailbox or the chosen scope changes.
  useEffect(() => {
    if (client && account) void scan(false);
    return () => abort.current?.abort();
  }, [client, account, scopeId, scan]);

  /* ---------- derived data ---------- */

  const groups = useMemo(() => groupMessages(messages, groupBy), [messages, groupBy]);
  const visible = useMemo(
    () => sortGroups(filterGroups(groups, query), sortBy),
    [groups, query, sortBy],
  );
  const summary = useMemo(() => totals(groups), [groups]);
  const selected = useMemo(
    () => visible.find((g) => g.key === selectedKey) ?? groups.find((g) => g.key === selectedKey) ?? null,
    [visible, groups, selectedKey],
  );

  /* ---------- actions ---------- */

  const handleBulk = async (action: BulkAction) => {
    if (!client || !selected) return;
    setBusy(true);
    try {
      const ids = new Set(selected.messageIds);
      await applyBulkAction(client, account, selected.messageIds, action);

      // Update the local view rather than re-scanning: instant, and a re-scan
      // of a big mailbox costs thousands of calls.
      setMessages((prev) =>
        action === 'trash'
          ? prev.filter((m) => !ids.has(m.id))
          : prev.map((m) =>
              ids.has(m.id)
                ? {
                    ...m,
                    inInbox: action === 'archive' ? false : m.inInbox,
                    unread: action === 'markRead' ? false : m.unread,
                  }
                : m,
            ),
      );
      setSelectedKey(null);
    } finally {
      setBusy(false);
    }
  };

  const handleUnsubscribe = async () => {
    if (!selected) return { kind: 'unavailable' as const };
    return runUnsubscribe(selected.unsubscribe);
  };

  const handleCreateFilter = async (plan: FilterPlan) => {
    if (!client) throw new Error('Not signed in.');
    setBusy(true);
    try {
      await createFilterFromPlan(client, plan);
    } finally {
      setBusy(false);
    }
  };

  const resetCache = async () => {
    if (!account) return;
    await clearAccount(account);
    setMessages([]);
    void scan(true);
  };

  /* ---------- render ---------- */

  if (!clientId) {
    return (
      <Shell>
        <SetupScreen
          origin={window.location.origin}
          error={error}
          onSave={(id) => {
            localStorage.setItem(CLIENT_ID_KEY, id);
            setClientId(id);
            setError(null);
          }}
        />
      </Shell>
    );
  }

  if (!session) {
    return (
      <Shell>
        <div className="card">
          <h2 style={{ marginTop: 0 }}>Sign in to Gmail</h2>
          <p className="note" style={{ marginTop: 0 }}>
            Your mail is read on this device and sent nowhere else. There is no server.
          </p>
          {error ? <div className="alert alert-error" style={{ marginTop: 12 }}>{error}</div> : null}
          <button className="btn btn-primary btn-block" style={{ marginTop: 14 }} onClick={signIn} data-testid="sign-in">
            Sign in with Google
          </button>
          <button
            className="btn btn-block"
            style={{ marginTop: 8 }}
            onClick={() => {
              localStorage.removeItem(CLIENT_ID_KEY);
              setClientId('');
            }}
          >
            Change OAuth client ID
          </button>
        </div>
      </Shell>
    );
  }

  const scanning = progress?.phase === 'listing' || progress?.phase === 'fetching';
  const pct =
    progress && progress.total > 0 ? Math.min(100, (progress.done / progress.total) * 100) : 0;

  return (
    <Shell
      account={account}
      onSignOut={signOut}
      onRescan={() => void scan(false)}
      scanning={scanning}
    >
      {error ? <div className="alert alert-error">{error}</div> : null}

      <div className="segmented" role="group" aria-label="Mailbox scope">
        {SCOPES_QUERY.map((s) => (
          <button
            key={s.id}
            className="seg"
            aria-pressed={scopeId === s.id}
            onClick={() => setScopeId(s.id)}
            disabled={scanning}
          >
            {s.label}
          </button>
        ))}
      </div>

      {scanning ? (
        <div className="card" style={{ marginTop: 12 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
            <span className="spinner" />
            <span>{progress?.message ?? 'Scanning…'}</span>
          </div>
          {progress && progress.total > 0 ? (
            <div className="progress">
              <i style={{ width: `${pct}%` }} />
            </div>
          ) : null}
          {progress && progress.fromCache > 0 ? (
            <p className="note">{progress.fromCache.toLocaleString()} reused from this device's cache.</p>
          ) : null}
          <button
            className="btn btn-block"
            style={{ marginTop: 10 }}
            onClick={() => abort.current?.abort()}
          >
            Stop
          </button>
        </div>
      ) : null}

      <div className="stats" style={{ marginTop: 12 }}>
        <div className="stat">
          <b>{summary.senders.toLocaleString()}</b>
          <span>Senders</span>
        </div>
        <div className="stat">
          <b>{summary.messages.toLocaleString()}</b>
          <span>Mail</span>
        </div>
        <div className="stat">
          <b>{formatSize(summary.size)}</b>
          <span>Size</span>
        </div>
        <div className="stat">
          <b>{summary.unsubscribable.toLocaleString()}</b>
          <span>Unsub</span>
        </div>
      </div>

      <input
        className="field"
        value={query}
        onChange={(e) => setQuery(e.target.value)}
        placeholder="Filter senders"
        aria-label="Filter senders"
        autoCapitalize="off"
        autoCorrect="off"
        spellCheck={false}
        type="search"
      />

      <div className="segmented" style={{ marginTop: 10 }} role="group" aria-label="Sort and group">
        {SORTS.map((s) => (
          <button key={s.id} className="seg" aria-pressed={sortBy === s.id} onClick={() => setSortBy(s.id)}>
            {s.label}
          </button>
        ))}
        <button
          className="seg"
          aria-pressed={groupBy === 'domain'}
          onClick={() => setGroupBy((g) => (g === 'domain' ? 'sender' : 'domain'))}
        >
          By domain
        </button>
      </div>

      {visible.length > 0 ? (
        <ul className="sender-list">
          {visible.slice(0, 300).map((g) => (
            <SenderRow key={g.key} group={g} onSelect={() => setSelectedKey(g.key)} />
          ))}
        </ul>
      ) : !scanning ? (
        <div className="empty">
          <p>{messages.length ? 'No senders match that filter.' : 'Nothing scanned yet.'}</p>
          {!messages.length ? (
            <button className="btn btn-primary" onClick={() => void scan(false)}>
              Scan mailbox
            </button>
          ) : null}
        </div>
      ) : null}

      {visible.length > 300 ? (
        <p className="note">Showing the top 300 of {visible.length.toLocaleString()} senders.</p>
      ) : null}

      {!scanning && messages.length > 0 ? (
        <button className="btn btn-sm" style={{ marginTop: 14 }} onClick={() => void resetCache()}>
          Clear cache and rescan
        </button>
      ) : null}

      {selected ? (
        <SenderSheet
          group={selected}
          busy={busy}
          onClose={() => setSelectedKey(null)}
          onBulk={handleBulk}
          onUnsubscribe={handleUnsubscribe}
          onCreateFilter={handleCreateFilter}
        />
      ) : null}
    </Shell>
  );
}

function Shell({
  account,
  onSignOut,
  onRescan,
  scanning,
  children,
}: {
  account?: string;
  onSignOut?: () => void;
  onRescan?: () => void;
  scanning?: boolean;
  children: React.ReactNode;
}) {
  return (
    <div className="app">
      <header className="header">
        <div className="header-row">
          <h1 className="brand">
            Mailstrom
            {account ? <small>{account}</small> : null}
          </h1>
          {onRescan ? (
            <button className="btn btn-sm" onClick={onRescan} disabled={scanning}>
              Rescan
            </button>
          ) : null}
          {onSignOut ? (
            <button className="btn btn-sm" onClick={onSignOut}>
              Sign out
            </button>
          ) : null}
        </div>
      </header>
      <main className="content">{children}</main>
    </div>
  );
}

/** Turn an exception into something worth showing a human. */
function explain(err: unknown): string {
  if (err instanceof GmailError) {
    if (err.status === 401) return 'Your Google session expired. Sign in again.';
    if (err.status === 403) {
      return 'Google refused the request. Check that the Gmail API is enabled and that you granted every requested permission.';
    }
    if (err.status === 429) return 'Gmail is rate-limiting this account. Wait a minute and retry.';
    return err.message;
  }
  return err instanceof Error ? err.message : String(err);
}
