// The "es2os" imports (os_wasm.c) for a module run under Node, over a file system of choice:
// the Win32 rules of platform/src/os_node.js over node:fs (nodeFiles) or over a site's
// web/vfs.mjs file system (vfsFiles), or both by path (routedFiles). es2osHost(files) is a
// Module.es2os value.
import { createRequire } from 'node:module';
import { vfsFiles } from '../vfs_files.mjs';

const require = createRequire(import.meta.url);
const { es2osImports, nodeFiles, fullPath, ERROR } = require('../../platform/src/os_node.js');

export { es2osImports, nodeFiles, fullPath, ERROR, vfsFiles };

export const es2osHost = (files) => (memory, runtime) => es2osImports(memory, runtime, files);

// `primary` with the full paths at or below each of `prefixes` (host directories such as a
// test's output directory, which may lie on the VFS's D:) served by `host` instead; the working
// directory is the primary's. A rename between the two fails (ACCESS_DENIED).
export function routedFiles(primary, host, prefixes) {
  const keys = prefixes.map((p) => fullPath(p, process.cwd()).replace(/\\+$/, '').toUpperCase());
  const of = (path) => {
    const key = path.toUpperCase();
    return keys.some((k) => key === k || key.startsWith(k + '\\')) ? host : primary;
  };
  return {
    cwd: () => primary.cwd(),
    stat: (path) => of(path).stat(path),
    list: (dir) => of(dir).list(dir),
    open: (path, mode) => of(path).open(path, mode),
    remove: (path) => of(path).remove(path),
    rename(from, to) {
      if (of(from) !== of(to)) throw Object.assign(new Error(`rename across file systems: ${from} -> ${to}`), { code: 'EXDEV' });
      of(from).rename(from, to);
    },
    mkdir: (path) => of(path).mkdir(path),
  };
}
