import { useState } from 'react';

/**
 * First-run screen: collect the Google OAuth client ID.
 *
 * The ID can also be baked in at build time via `VITE_GOOGLE_CLIENT_ID`. We
 * accept it at runtime too so the app can be deployed once and configured on
 * the phone, without a rebuild. A client ID is not a secret — it's public by
 * design in browser OAuth flows.
 */
export function SetupScreen({
  origin,
  onSave,
  error,
}: {
  origin: string;
  onSave: (clientId: string) => void;
  error?: string | null;
}) {
  const [value, setValue] = useState('');
  const trimmed = value.trim();
  const looksValid = /\.apps\.googleusercontent\.com$/.test(trimmed);

  return (
    <div className="card">
      <h2 style={{ marginTop: 0 }}>One-time setup</h2>
      <p className="note" style={{ marginTop: 0 }}>
        Mailstrom talks to Gmail directly from this device. To let it, you create a free Google
        OAuth client — it takes about three minutes and you only do it once.
      </p>

      {error ? <div className="alert alert-error">{error}</div> : null}

      <ol className="setup-steps">
        <li>
          Open <code>console.cloud.google.com</code> and create a project.
        </li>
        <li>
          Under <b>APIs &amp; Services → Library</b>, enable the <b>Gmail API</b>.
        </li>
        <li>
          Under <b>OAuth consent screen</b>, choose <b>External</b>, fill in the app name and your
          email, and add yourself under <b>Test users</b>.
        </li>
        <li>
          Add the scopes <code>gmail.modify</code> and <code>gmail.settings.basic</code>.
        </li>
        <li>
          Under <b>Credentials</b>, create an <b>OAuth client ID</b> of type <b>Web application</b>,
          and add this exact URL under <b>Authorised JavaScript origins</b>:
          <br />
          <code>{origin}</code>
        </li>
        <li>Paste the client ID below.</li>
      </ol>

      <label className="lbl" htmlFor="client-id" style={{ marginTop: 14 }}>
        OAuth client ID
      </label>
      <input
        id="client-id"
        className="field"
        value={value}
        onChange={(e) => setValue(e.target.value)}
        placeholder="1234-abc.apps.googleusercontent.com"
        autoCapitalize="off"
        autoCorrect="off"
        spellCheck={false}
        inputMode="url"
        data-testid="client-id-input"
      />

      <button
        className="btn btn-primary btn-block"
        style={{ marginTop: 12 }}
        disabled={!trimmed}
        onClick={() => onSave(trimmed)}
        data-testid="save-client-id"
      >
        Save and continue
      </button>

      {trimmed && !looksValid ? (
        <p className="note">
          That doesn't look like a Google client ID — they normally end in
          <code>.apps.googleusercontent.com</code>. You can still try it.
        </p>
      ) : null}
    </div>
  );
}
