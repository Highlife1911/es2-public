// Fakes of the browser objects the page modules use (app.js, selftest.mjs, isolation.mjs,
// checks.mjs, sw.js), for the page tests under Node.
import fs from 'node:fs';
import vm from 'node:vm';

export class Events {
  constructor() { this.listeners = {}; }
  addEventListener(name, fn) { (this.listeners[name] ??= []).push(fn); }
  removeEventListener(name, fn) { this.listeners[name] = (this.listeners[name] ?? []).filter((f) => f !== fn); }
  fire(name, event = {}) { for (const fn of [...(this.listeners[name] ?? [])]) fn(event); }
}

class Element extends Events {
  constructor(tag) {
    super();
    this.tag = tag;
    this.children = [];
    this.textContent = '';
    this.hidden = false;
    this.className = '';
  }
  append(...c) { this.children.push(...c); }
  prepend(...c) { this.children.unshift(...c); }
  replaceChildren(...c) { this.children = c; }
  click() { this.clicked = (this.clicked ?? 0) + 1; this.fire('click'); }
  remove() { this.removed = true; }
  setAttribute(name, value) { (this.attributes ??= {})[name] = String(value); }
  // The text of the element's subtree, children joined by '|'.
  text() { return this.children.length ? this.children.map((c) => c.text()).join('|') : this.textContent; }
}

// A document with the elements of index.html and selftest.html that the modules look up.
export class FakeDocument extends Events {
  constructor() {
    super();
    this.title = '';
    this.documentElement = { lang: '' };
    this.ids = {};
    for (const id of ['title', 'checks', 'steps', 'language', 'saves', 'volume', 'start', 'results', 'game-title', 'game-results', 'keys-title', 'layout', 'key-rows']) this.ids[id] = new Element('div');
    this.ids.steps.hidden = true;
    this.body = new Element('body');
  }
  getElementById(id) { return this.ids[id] ?? null; }
  createElement(tag) { return new Element(tag); }
}

// A service worker container: register() installs a worker that activates in a later task
// (or becomes redundant with `fail`); `controller` is set as clients.claim() sets it.
export class FakeServiceWorker {
  constructor({ fail = false, refuse = false } = {}) {
    this.fail = fail;
    this.refuse = refuse;
    this.registered = [];
    this.controller = null;
    this.registration = null;
  }

  register(url) {
    this.registered.push(url);
    if (this.refuse) return Promise.reject(new Error('SecurityError'));
    if (this.registration) return Promise.resolve(this.registration);
    const worker = new Events();
    worker.state = 'installing';
    this.registration = { installing: worker, waiting: null, active: null };
    setTimeout(() => {
      worker.state = this.fail ? 'redundant' : 'activated';
      if (!this.fail) {
        this.registration.installing = null;
        this.registration.active = worker;
        this.controller = worker;
      }
      worker.fire('statechange');
    }, 1);
    return Promise.resolve(this.registration);
  }
}

// navigator.locks with ifAvailable: a held name gives the callback null.
export class FakeLocks {
  constructor() { this.held = new Set(); }
  async request(name, options, callback) {
    if (this.held.has(name)) return callback(null);
    this.held.add(name);
    try {
      return await callback({ name, mode: 'exclusive' });
    } finally {
      this.held.delete(name);
    }
  }
}

export class FakeStorage {
  constructor() { this.items = new Map(); }
  getItem(k) { return this.items.has(k) ? this.items.get(k) : null; }
  setItem(k, v) { this.items.set(k, String(v)); }
  removeItem(k) { this.items.delete(k); }
}

// A browser global with every feature the checks look for. `supported` lists the MediaSource
// types and the MediaCapabilities video types it accepts.
export function fakeBrowser({ isolated = true, serviceWorker = new FakeServiceWorker(), sessionStorage = new FakeStorage(),
  languages = ['en-US'], supported = null } = {}) {
  const types = supported ?? ['video/webm; codecs="av01.0.08M.08"', 'audio/webm; codecs="opus"'];
  class AudioContext {}
  Object.defineProperty(AudioContext.prototype, 'audioWorklet', { get() { return {}; } });
  const opened = [];
  const env = {
    crossOriginIsolated: isolated,
    isSecureContext: true,
    SharedArrayBuffer,
    WebAssembly,
    AudioContext,
    AudioWorkletNode: class AudioWorkletNode {},
    MediaSource: { isTypeSupported: (t) => types.includes(t) },
    indexedDB: {
      opened,
      deleted: [],
      open(name, version) {
        opened.push([name, version]);
        const r = { result: { close() { r.closed = true; } } };
        setTimeout(() => r.onsuccess?.(), 0);
        return r;
      },
      deleteDatabase(name) { this.deleted.push(name); },
    },
    navigator: {
      language: languages[0],
      languages,
      serviceWorker,
      mediaCapabilities: { decodingInfo: async (c) => ({ supported: types.includes(c.video.contentType) }) },
    },
    sessionStorage,
    reloads: 0,
  };
  env.location = { reload: () => { env.reloads++; } };
  return env;
}

// web/sw.js (its path, or its source with `source`) in its own context: `self` records the
// listeners, skipWaiting and clients.claim; `fetch` is the given function. Returns the context's self.
export function loadServiceWorker(path, fetch, origin = 'http://127.0.0.1:8000', source = null) {
  const self = new Events();
  self.location = { origin };
  self.skipped = 0;
  self.claimed = 0;
  self.skipWaiting = () => { self.skipped++; return Promise.resolve(); };
  self.clients = { claim: () => { self.claimed++; return Promise.resolve(); } };
  const context = vm.createContext({ self, fetch, Headers, Request, Response, URL, Promise, Object });
  vm.runInContext(source ?? fs.readFileSync(path, 'utf8'), context, { filename: path });
  return self;
}

// A fetch event through the loaded worker: the response it answered with, or null when it
// left the request to the browser.
export async function swFetch(self, request) {
  let answer = null;
  self.fire('fetch', { request, respondWith: (p) => { answer = p; } });
  return answer === null ? null : await answer;
}

// The browser's rule for a document: isolated with COOP same-origin and COEP require-corp
// (or credentialless).
export function isolatedBy(headers) {
  return headers.get('cross-origin-opener-policy') === 'same-origin'
    && ['require-corp', 'credentialless'].includes(headers.get('cross-origin-embedder-policy'));
}
