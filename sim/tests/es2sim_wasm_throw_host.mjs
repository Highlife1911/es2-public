// A host.mjs host (--host) for es2sim_wasm_exits_test.mjs: the "es2os" import ES2_THROW_AT
// throws on its first call, a WebAssembly.RuntimeError with the message ES2_THROW, or with
// ES2_THROW=media the VFS's media-bytes error (web/vfs.mjs).
export default function setup(all) {
  const inner = all.es2os;
  const at = process.env.ES2_THROW_AT;
  const what = process.env.ES2_THROW;
  all.es2os = (memory, runtime) => {
    const imports = { ...inner(memory, runtime) };
    const f = imports[at];
    if (!f) throw new Error(`no es2os import ${at}`);
    imports[at] = () => {
      if (what === 'media') throw Object.assign(new Error(`media read`), { code: 'media-bytes' });
      throw new WebAssembly.RuntimeError(what);
    };
    return imports;
  };
}
