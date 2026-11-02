// Cross-origin isolation through the service worker (docs/web_build.md section 13, step 1): the
// page registers sw.js, which adds COOP/COEP to every response, and reloads once so that the
// document itself comes through the worker. A session flag stops a second reload: when the
// reloaded page is still not isolated, the result is "failed".
//
//   const state = await ensureIsolation(globalThis);   // 'isolated', 'reloading' or a failure
//
// `env` gives crossOriginIsolated, isSecureContext, navigator (serviceWorker), location
// (reload) and sessionStorage; web/node/page_test.mjs passes fakes.
export const SERVICE_WORKER = 'sw.js';
export const RELOAD_FLAG = 'es2-isolation-reload';

// The states that keep the game from starting, with their text keys (texts.mjs).
export const FAILURES = {
  insecure: 'isolation.insecure',
  'no-service-worker': 'isolation.no-service-worker',
  failed: 'isolation.failed',
};

function storage(env) {
  try {
    return env.sessionStorage ?? null;
  } catch {
    return null; // a page with blocked storage throws on the getter
  }
}

function getFlag(env) {
  try {
    const s = storage(env);
    return s !== null && s.getItem(RELOAD_FLAG) !== null;
  } catch {
    return false;
  }
}

function setFlag(env, on) {
  try {
    const s = storage(env);
    if (!s) return false;
    if (on) s.setItem(RELOAD_FLAG, '1');
    else s.removeItem(RELOAD_FLAG);
    return true;
  } catch {
    return false;
  }
}

// Resolves when the registration's worker is activated, false when it became redundant.
function activated(registration) {
  const worker = registration.installing ?? registration.waiting ?? registration.active;
  if (!worker) return Promise.resolve(false);
  return new Promise((resolve) => {
    const done = () => worker.state === 'activated' || worker.state === 'redundant';
    if (done()) return resolve(worker.state === 'activated');
    const step = () => {
      if (!done()) return;
      worker.removeEventListener('statechange', step);
      resolve(worker.state === 'activated');
    };
    worker.addEventListener('statechange', step);
  });
}

export async function ensureIsolation(env) {
  const sw = env.navigator?.serviceWorker;
  if (env.crossOriginIsolated === true) {
    setFlag(env, false);
    // Keeps the worker registered (and updated) for the next visit; the page needs nothing of it now.
    sw?.register(SERVICE_WORKER).catch(() => {});
    return 'isolated';
  }
  if (env.isSecureContext === false) return 'insecure';
  if (!sw) return 'no-service-worker';
  let registration;
  try {
    registration = await sw.register(SERVICE_WORKER);
  } catch {
    return 'no-service-worker';
  }
  if (getFlag(env)) {
    setFlag(env, false);
    return 'failed';
  }
  if (!(await activated(registration))) return 'failed';
  if (!setFlag(env, true)) return 'failed'; // without the flag a reload could repeat forever
  env.location.reload();
  return 'reloading';
}
