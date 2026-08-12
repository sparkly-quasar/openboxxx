# Mailstrom

A bulk Gmail cleanup tool that installs to an iPhone home screen. Groups your mailbox by
sender, then lets you trash or archive a sender's entire history in one tap, unsubscribe
from the ones that offer it, and create a Gmail filter so they never come back.

> **Note on this repository.** `openboxxx` is a DJ software project (Mixxx → rekordbox USB
> export). This app is unrelated to it and shares no code; it lives here only because that's
> where it was asked for. It is entirely self-contained in `mailstrom/` and can be lifted
> into its own repository by copying the directory.

## What it does

- **Group by sender or domain.** Every message is reduced to sender, size, age, unread and
  inbox state, then bucketed. Domain mode folds `news@acme.com` and `billing@acme.com`
  into one row.
- **Bulk actions.** Trash, archive, or mark-read an entire sender at once, behind a
  confirmation step. Trash means Gmail's Trash — recoverable for 30 days. The app never
  calls the permanent-delete endpoint.
- **Unsubscribe.** Senders advertising `List-Unsubscribe` are badged. One-click (RFC 8058)
  senders get a POST; everyone else hands you their link.
- **Filters.** Create a Gmail filter for a sender — skip inbox, mark read, apply a label, or
  send straight to trash. A domain group produces a single filter matching every address.
- **Scopes.** Inbox, all mail, unread, older than a year, large, or newsletters.

## Architecture, and why

**There is no backend.** The app is static files. It talks to the Gmail REST API directly
from the browser using Google Identity Services' implicit token flow.

The trade-off is deliberate. A server would let it hold refresh tokens and run scans in the
background, but it would also mean a machine that holds an access token to your entire
mailbox. There isn't one. Your mail is never transmitted anywhere except between your phone
and Google. The cost you pay: access tokens last about an hour and cannot be silently
refreshed forever, so a long session will occasionally ask you to sign in again.

A few other decisions worth knowing:

- **Tokens live in `sessionStorage`**, not `localStorage`, so they die with the tab rather
  than persisting on disk.
- **Scanning is batched and incremental.** Reading headers one message at a time costs one
  request — and one ~1 KB bearer token — per message, which on a 30 000-message mailbox is
  tens of megabytes of pure overhead on cellular. The Gmail batch endpoint folds 100 reads
  into one request. If batching ever fails the app falls back to concurrent individual
  requests, so a Google-side change degrades speed rather than breaking the app.
- **Message metadata is cached in IndexedDB**, keyed by mailbox. A rescan only fetches IDs
  it has not seen and evicts ones that disappeared, so the expensive full scan happens once.
- **Requests are paced to Gmail's quota** (250 units/sec; `messages.get` costs 5) and back
  off on 429/5xx, honouring `Retry-After`.
- **The service worker caches the app shell only** — never API responses. Caching mail in a
  store that outlives the tab would undo the point of keeping tokens in `sessionStorage`.

## Setup

You need your own Google OAuth client. This is unavoidable: Gmail's read/modify scopes are
"restricted", and an app cannot ship a shared client ID for them without going through
Google's verification and a security assessment. Your own client, used only by you, needs
none of that — but it does cap you at 100 test users, which is fine for personal use.

It takes about three minutes, once:

1. At [console.cloud.google.com](https://console.cloud.google.com), create a project.
2. **APIs & Services → Library** → enable the **Gmail API**.
3. **OAuth consent screen** → **External**. Fill in an app name and your email. Add your own
   Google account under **Test users**.
4. Add these scopes:
   - `https://www.googleapis.com/auth/gmail.modify` — read headers, apply labels, trash
   - `https://www.googleapis.com/auth/gmail.settings.basic` — create filters
5. **Credentials** → **Create credentials** → **OAuth client ID** → **Web application**.
   Under **Authorised JavaScript origins**, add the exact URL you'll serve the app from
   (e.g. `https://mailstrom.you.vercel.app`). No trailing slash.
6. Open the app and paste the client ID into the setup screen.

The client ID is not a secret — browser OAuth clients are public by design. You can also
bake it in at build time with `VITE_GOOGLE_CLIENT_ID` instead of pasting it.

> Google will show an "unverified app" warning on first sign-in. That is expected for a
> personal OAuth client with you as the sole test user.

## Deploying, and installing on the iPhone

The build output is static, so any static host works. It **must** be served over HTTPS —
both Google OAuth and service workers require it. `localhost` is exempt for development.

```bash
npm install
npm run build      # → dist/
```

Then deploy `dist/` (Vercel, Netlify, Cloudflare Pages, GitHub Pages, or your own nginx).
Add the deployed origin to your OAuth client's authorised origins.

To install on the phone:

1. Open the URL in **Safari** (not Chrome — only Safari can install to the home screen on iOS).
2. Tap **Share** → **Add to Home Screen**.
3. Launch it from the icon. It runs full-screen with no browser chrome.

## Development

```bash
npm install
npm run dev          # http://localhost:5173, reachable from your phone on the same network
npm test             # unit tests (vitest)
npm run test:e2e     # end-to-end tests (playwright, iPhone viewport, stubbed Gmail)
npm run lint
npm run build
npm run icons        # regenerate PNG icons from public/icons/icon.svg
```

### Tests

70 unit tests cover the pure logic — header parsing (quoted names, unbracketed addresses,
`List-Unsubscribe` shapes), sender grouping and display-name selection, sorting, the
multipart batch-response parser including partial failures, and filter construction.

23 end-to-end tests drive the real UI at iPhone viewport against a stubbed Gmail API,
covering the scan, grouping and sorting, every bulk action and its confirmation, unsubscribe,
filter creation, the IndexedDB cache, expired-session handling, the batch→individual
fallback, plus iPhone-specific checks (no horizontal scroll, 44px tap targets, 16px inputs
so Safari doesn't zoom, and an installable manifest whose icons all resolve).

The e2e suite runs on **Chromium with iPhone 13 metrics**, not WebKit — WebKit isn't
installed in this environment. It verifies layout, tap targets and app logic, none of which
are engine-specific. Real Safari behaviour is worth a check on the device itself.

## Known limits

- **Unsubscribe cannot be confirmed.** A browser sends a cross-origin one-click POST but is
  not allowed to read the response, so the app reports the request as *sent*, not
  *succeeded*. Rescan in a few days to see whether it took.
- **Filters are not retroactive.** Gmail applies them only to mail that arrives after
  creation. Use Trash or Archive for what's already there — the sheet says so.
- **Sessions expire hourly.** A consequence of having no backend; see above.
- **A first scan of a very large mailbox takes minutes** and is quota-bound at roughly 50
  messages/second. It is cached afterwards, and it is resumable — partial progress is banked
  as it goes, so stopping and restarting doesn't start over.
