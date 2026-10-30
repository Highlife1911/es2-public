// The game's volume (docs/web_build.md section 13): one value from 0 to 1, set on the landing
// page and kept in localStorage; game.mjs applies it to the sound worklet, the CD music and the
// movies. A storage that is missing, throws or holds something else gives the full volume.
export const VOLUME_KEY = 'es2.volume';
export const DEFAULT_VOLUME = 1;

export function loadVolume(storage) {
  try {
    const raw = storage?.getItem(VOLUME_KEY);
    if (raw === null || raw === undefined || raw === '') return DEFAULT_VOLUME;
    const v = Number(raw);
    return Number.isFinite(v) && v >= 0 && v <= 1 ? v : DEFAULT_VOLUME;
  } catch {
    return DEFAULT_VOLUME;
  }
}

export function saveVolume(storage, volume) {
  try {
    storage?.setItem(VOLUME_KEY, String(volume));
  } catch {
    // the value holds for this page only
  }
}
