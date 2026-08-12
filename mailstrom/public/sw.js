/*
 * Service worker: makes the app installable and keeps the shell available
 * offline.
 *
 * Scope is deliberately narrow. We cache the app's own static assets and
 * nothing else — never Gmail API responses, never the Google auth scripts.
 * Caching mail data here would put someone's inbox in a store that outlives
 * the tab, which is exactly what this app avoids by keeping tokens in
 * sessionStorage.
 */

const VERSION = 'v1';
const SHELL_CACHE = `mailstrom-shell-${VERSION}`;

const SHELL = ['/', '/index.html', '/manifest.webmanifest', '/icons/icon.svg'];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches
      .open(SHELL_CACHE)
      // addAll fails the whole install if any single entry 404s, so add
      // individually and tolerate misses.
      .then((cache) => Promise.all(SHELL.map((url) => cache.add(url).catch(() => undefined))))
      .then(() => self.skipWaiting()),
  );
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches
      .keys()
      .then((keys) =>
        Promise.all(keys.filter((k) => k !== SHELL_CACHE).map((k) => caches.delete(k))),
      )
      .then(() => self.clients.claim()),
  );
});

self.addEventListener('fetch', (event) => {
  const { request } = event;

  if (request.method !== 'GET') return;

  const url = new URL(request.url);

  // Same-origin only. Gmail, Google auth and any unsubscribe endpoint must
  // always hit the network.
  if (url.origin !== self.location.origin) return;

  // Navigations: network first so a deploy is picked up immediately, falling
  // back to the cached shell when offline.
  if (request.mode === 'navigate') {
    event.respondWith(
      fetch(request)
        .then((response) => {
          const copy = response.clone();
          caches.open(SHELL_CACHE).then((cache) => cache.put('/index.html', copy));
          return response;
        })
        .catch(() => caches.match('/index.html').then((hit) => hit ?? Response.error())),
    );
    return;
  }

  // Static assets: cache first. Vite fingerprints filenames, so a cached hit
  // is always the right content for that URL.
  event.respondWith(
    caches.match(request).then(
      (hit) =>
        hit ??
        fetch(request).then((response) => {
          if (response.ok && response.type === 'basic') {
            const copy = response.clone();
            caches.open(SHELL_CACHE).then((cache) => cache.put(request, copy));
          }
          return response;
        }),
    ),
  );
});
