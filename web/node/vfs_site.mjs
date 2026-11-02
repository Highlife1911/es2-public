// A built site's file system under Node (web/vfs.mjs): manifest.json and gamefiles.zst read with
// node:fs in place of the page's load (web/store.mjs GameFiles).
//
//   const codec = await loadCodecFrom('bazel-bin/web/vfs.js');
//   const vfs = openSite('build/web/site', codec, { poison: true });
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { Vfs, decodeGameFiles, loadCodec } from '../vfs.mjs';

export async function loadCodecFrom(vfsJs) {
  const factory = (await import(pathToFileURL(path.resolve(vfsJs)).href)).default;
  return loadCodec(factory);
}

export function openSite(site, codec, options = {}) {
  const manifest = JSON.parse(fs.readFileSync(path.join(site, 'manifest.json'), 'utf8'));
  const gamefiles = new Uint8Array(fs.readFileSync(path.join(site, manifest.gamefiles.path)));
  return new Vfs({ manifest, content: decodeGameFiles(codec, manifest, gamefiles), ...options });
}
