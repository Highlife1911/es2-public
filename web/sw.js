// The site's service worker (docs/web_build.md sections 8.5 and 13): it gives every same-origin
// response the COOP/COEP headers that make the page cross-origin isolated (SharedArrayBuffer)
// on a static host that sends none, and has the browser revalidate the site's files whatever
// caching headers the host sends. The page registers it and reloads once (web/isolation.mjs).
//
// A classic script, not a module: it registers in every browser the page supports.
// web/node/page_test.mjs runs it in a context with a fake `self`.
'use strict';

const ISOLATION_HEADERS = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
};

// The response with the isolation headers set; opaque and redirect responses stay as they are
// (their headers cannot be read or copied).
function isolate(response) {
  if (response.status === 0 || response.type === 'opaque' || response.type === 'opaqueredirect') return response;
  const headers = new Headers(response.headers);
  for (const [name, value] of Object.entries(ISOLATION_HEADERS)) headers.set(name, value);
  return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
}

// Whether the worker answers a request: same-origin only, and never "only-if-cached" outside
// mode "same-origin", which fetch() refuses (Chrome sends such requests from DevTools).
function handles(request, origin) {
  if (request.cache === 'only-if-cached' && request.mode !== 'same-origin') return false;
  return new URL(request.url).origin === origin;
}

// The site's files keep their names from one build to the next. A navigation's request cannot be
// copied with new options, and a request with its own cache mode (gamefiles.zst's no-store)
// keeps it.
function revalidating(request) {
  if (request.mode === 'navigate' || request.cache !== 'default') return request;
  return new Request(request, { cache: 'no-cache' });
}

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch', (event) => {
  if (!handles(event.request, self.location.origin)) return;
  event.respondWith(fetch(revalidating(event.request)).then(isolate));
});
