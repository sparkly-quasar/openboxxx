/**
 * Google OAuth via Google Identity Services, entirely in the browser.
 *
 * We use the implicit token flow rather than an authorization-code flow with a
 * backend. The trade-off is deliberate:
 *
 *   + No server exists, so there is no server to leak a refresh token from and
 *     nothing between the user's phone and Google holding their mail.
 *   + The whole app deploys as static files.
 *   - Access tokens last ~1 hour and there is no refresh token, so long
 *     sessions need a re-tap. `ensureToken` makes that a silent re-auth
 *     wherever Google allows it.
 */

const GIS_SRC = 'https://accounts.google.com/gsi/client';

export const SCOPES = [
  // Read metadata, apply labels, and move to trash.
  'https://www.googleapis.com/auth/gmail.modify',
  // Create/list/delete filters.
  'https://www.googleapis.com/auth/gmail.settings.basic',
].join(' ');

interface TokenResponse {
  access_token?: string;
  expires_in?: number;
  error?: string;
  error_description?: string;
}

interface TokenClient {
  requestAccessToken(overrides?: { prompt?: string }): void;
  callback: (response: TokenResponse) => void;
}

declare global {
  interface Window {
    google?: {
      accounts: {
        oauth2: {
          initTokenClient(config: {
            client_id: string;
            scope: string;
            prompt?: string;
            callback: (response: TokenResponse) => void;
            error_callback?: (error: { type?: string; message?: string }) => void;
          }): TokenClient;
          revoke(token: string, done?: () => void): void;
        };
      };
    };
  }
}

let gisPromise: Promise<void> | null = null;

/** Load the GIS script once, shared across callers. */
function loadGis(): Promise<void> {
  if (gisPromise) return gisPromise;

  gisPromise = new Promise<void>((resolve, reject) => {
    if (window.google?.accounts?.oauth2) return resolve();

    const existing = document.querySelector<HTMLScriptElement>(`script[src="${GIS_SRC}"]`);
    const script = existing ?? document.createElement('script');
    script.src = GIS_SRC;
    script.async = true;
    script.defer = true;
    script.addEventListener('load', () => resolve());
    script.addEventListener('error', () =>
      reject(new Error('Could not load Google sign-in. Check your connection and any content blockers.')),
    );
    if (!existing) document.head.appendChild(script);
  }).catch((err) => {
    // Allow a later attempt to retry rather than caching the failure forever.
    gisPromise = null;
    throw err;
  });

  return gisPromise;
}

export interface Session {
  token: string;
  /** Epoch millis at which the token stops working. */
  expiresAt: number;
}

const STORAGE_KEY = 'mailstrom.session';
/** Treat a token as dead this long before it truly expires. */
const EXPIRY_MARGIN_MS = 60_000;

export function loadSession(): Session | null {
  try {
    const raw = sessionStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    const session = JSON.parse(raw) as Session;
    if (!session?.token || session.expiresAt - EXPIRY_MARGIN_MS < Date.now()) {
      sessionStorage.removeItem(STORAGE_KEY);
      return null;
    }
    return session;
  } catch {
    return null;
  }
}

function saveSession(session: Session) {
  try {
    // sessionStorage, not localStorage: the token dies with the tab.
    sessionStorage.setItem(STORAGE_KEY, JSON.stringify(session));
  } catch {
    /* private mode / quota — the in-memory session still works */
  }
}

export function clearSession() {
  try {
    sessionStorage.removeItem(STORAGE_KEY);
  } catch {
    /* ignore */
  }
}

/**
 * Request an access token.
 *
 * @param interactive when false, attempts a silent refresh (no account
 *        chooser). Google only honours this if the user already granted
 *        consent in this browser.
 */
export async function requestToken(clientId: string, interactive = true): Promise<Session> {
  await loadGis();

  const oauth2 = window.google?.accounts?.oauth2;
  if (!oauth2) throw new Error('Google sign-in unavailable.');

  return new Promise<Session>((resolve, reject) => {
    const client = oauth2.initTokenClient({
      client_id: clientId,
      scope: SCOPES,
      callback: (response) => {
        if (response.error || !response.access_token) {
          reject(new Error(response.error_description || response.error || 'Authorization failed.'));
          return;
        }
        const session: Session = {
          token: response.access_token,
          expiresAt: Date.now() + (response.expires_in ?? 3600) * 1000,
        };
        saveSession(session);
        resolve(session);
      },
      error_callback: (error) => {
        reject(new Error(error.message || error.type || 'Authorization was dismissed.'));
      },
    });

    client.requestAccessToken({ prompt: interactive ? 'consent' : '' });
  });
}

/**
 * Return a live session, refreshing silently if the stored one has expired.
 * Throws if only an interactive prompt would do.
 */
export async function ensureToken(clientId: string): Promise<Session> {
  const existing = loadSession();
  if (existing) return existing;
  return requestToken(clientId, false);
}

export function revoke(token: string) {
  try {
    window.google?.accounts?.oauth2?.revoke(token);
  } catch {
    /* best effort */
  }
  clearSession();
}
