// A built site's file system under Node (web/vfs.mjs): manifest.json and data/<sha256>.zst read
// with node:fs in place of the worker's XMLHttpRequest.
//
//   const codec = await loadCodecFrom('bazel-bin/web/vfs.js');
//   const vfs = openSite('build/web/site', codec, { poison: true });
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { Vfs, loadCodec } from '../vfs.mjs';

export async function loadCodecFrom(vfsJs) {
  const factory = (await import(pathToFileURL(path.resolve(vfsJs)).href)).default;
  return loadCodec(factory);
}

export const siteFetcher = (site) => (sha256) => fs.readFileSync(path.join(site, 'data', `${sha256}.zst`));

export function openSite(site, codec, options = {}) {
  const manifest = JSON.parse(fs.readFileSync(path.join(site, 'manifest.json'), 'utf8'));
  return new Vfs({ manifest, codec, fetchPart: siteFetcher(site), ...options });
}
