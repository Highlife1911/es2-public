// The game's file system for the "es2os" imports (platform/src/os_node.js's Module.es2osFiles):
// a Vfs (vfs.mjs) as the Win32 rules of os_node.js see a file system. Used by the browser's game
// worker and by the Node hosts (web/node/os_host.mjs).
import { keyOf } from './vfs.mjs';

// A Vfs as the host's file system. The VFS stores whole files, so an open file's bytes live
// here from its first write to its last close, shared by its handles; a file the program
// creates or truncates is written at the open. The VFS keeps no times: a file written in this
// session has the time of its last write, every other one `time` (ms since 1970). Nothing is
// read-only.
export function vfsFiles(vfs, { cwd = 'C:\\ES2', time = Date.now() } = {}) {
  const open = new Map();    // key -> {path, data, length, dirty, handles}
  const written = new Map(); // key -> ms
  const timesOf = (key) => {
    const t = written.get(key) ?? time;
    return [t, t, t];
  };
  const join = (dir, name) => (dir.endsWith('\\') ? dir + name : dir + '\\' + name);
  const describe = (key, e) => ({
    dir: e.dir, size: e.dir ? 0 : open.get(key)?.data ? open.get(key).length : e.size, readonly: false,
    times: timesOf(key),
  });
  const flush = (key, file) => {
    if (!file.dirty) return;
    vfs.write(file.path, file.data.subarray(0, file.length));
    file.dirty = false;
    written.set(key, Date.now());
  };
  return {
    cwd: () => cwd,
    stat(path) {
      const e = vfs.stat(path);
      return e && describe(keyOf(path), e);
    },
    list: (dir) => vfs.list(dir).map((e) => ({ name: e.name, ...describe(keyOf(join(dir, e.name)), e) })),
    open(path, { create, truncate }) {
      const key = keyOf(path);
      if (create || truncate) {
        vfs.write(path, new Uint8Array(0));
        written.set(key, Date.now());
      }
      let file = open.get(key);
      if (!file) {
        file = { path, data: null, length: 0, dirty: false, handles: 0 };
        open.set(key, file);
      }
      if (truncate && file.data) file.length = 0;
      file.handles++;
      const keyNow = () => keyOf(file.path);
      return {
        read(target, at, length, position) {
          if (file.data) {
            const n = Math.max(0, Math.min(length, file.length - position));
            if (n) target.set(file.data.subarray(position, position + n), at);
            return n;
          }
          const bytes = vfs.read(file.path, position, length);
          target.set(bytes, at);
          return bytes.length;
        },
        write(source, position) {
          if (!file.data) {
            file.data = vfs.readAll(file.path);
            file.length = file.data.length;
          }
          const end = position + source.length;
          if (end > file.data.length) {
            const grown = new Uint8Array(Math.max(end, file.data.length * 2));
            grown.set(file.data.subarray(0, file.length));
            file.data = grown;
          }
          if (position > file.length) file.data.fill(0, file.length, position);
          file.data.set(source, position);
          file.length = Math.max(file.length, end);
          file.dirty = true;
          written.set(keyNow(), Date.now());
          return source.length;
        },
        size: () => (file.data ? file.length : vfs.stat(file.path)?.size ?? 0),
        // A sparse view over the VFS's parts; a file with bytes of this session is read whole.
        map: (view) => (file.data ? null : vfs.createMapping(file.path, view)),
        close() {
          if (--file.handles) return;
          if (!file.removed) flush(keyNow(), file);
          if (open.get(keyNow()) === file) open.delete(keyNow());
        },
      };
    },
    remove(path) {
      const key = keyOf(path);
      vfs.remove(path);
      const file = open.get(key);
      if (file) {
        file.removed = true;
        open.delete(key);
      }
      written.delete(key);
    },
    // MoveFileEx onto an open target fails with ERROR_ACCESS_DENIED (os_node.js answers so
    // first); a replace here would let the target's last close write its bytes over the move.
    rename(from, to) {
      const fromKey = keyOf(from), toKey = keyOf(to);
      if (fromKey !== toKey && open.has(toKey)) throw Object.assign(new Error(`${to} is open`), { code: 'EACCES' });
      const file = open.get(fromKey);
      if (file) flush(fromKey, file);
      vfs.rename(from, to, { replace: true });
      if (fromKey === toKey) return;
      if (file) {
        open.delete(fromKey);
        file.path = to;
        open.set(toKey, file);
      }
      if (written.has(fromKey)) written.set(toKey, written.get(fromKey));
      else written.delete(toKey);
      written.delete(fromKey);
    },
    mkdir: (path) => vfs.mkdir(path),
  };
}
