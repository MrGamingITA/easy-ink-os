// Avatar delle sessioni (docs/AVATAR.md): disegno di un avatar JSON su canvas e cache dei file
// letti da /api/avatars/{id}. Simboli: '.' trasparente, '#' inchiostro, '+' carta.

export const POSES = ['working', 'permission', 'input', 'done', 'error', 'idle', 'compact', 'tired', 'stuffy', 'run'];
export const FALLBACK = { compact: 'working', tired: 'idle', stuffy: 'idle', run: 'working' };

// Fotogrammi effettivi di una posa (con il ripiego delle facoltative), null se mancano.
export function poseFrames(av, pose) {
  if (!av || !av.poses) return null;
  if (av.poses[pose]) return av.poses[pose];
  const fb = FALLBACK[pose];
  if (fb && av.poses[fb]) return av.poses[fb];
  return av.poses.idle || null;
}

// Disegna il fotogramma f (ridotto a f mod frames, come il display) a scala s in (ox, oy).
export function drawAvatar(c, av, ox, oy, s, pose, f, ink, paper) {
  const frs = poseFrames(av, pose);
  if (!frs || !frs.length) return false;
  const n = frs.length;
  const fr = frs[((f % n) + n) % n];
  for (let y = 0; y < fr.length; y++) {
    const row = fr[y];
    for (let x = 0; x < row.length; x++) {
      const ch = row[x];
      if (ch === '#') c.fillStyle = ink;
      else if (ch === '+') c.fillStyle = paper;
      else continue;
      c.fillRect(ox + x * s, oy + y * s, s, s);
    }
  }
  return true;
}

// Cache dei file avatar: chiave id + crc (il CRC cambia quando l'avatar viene sostituito).
const cache = new Map();

export function loadAvatar(id, crc) {
  const key = id + ':' + (crc === undefined || crc === null ? '' : crc);
  if (!cache.has(key)) {
    const p = fetch('/api/avatars/' + encodeURIComponent(id), { cache: 'no-cache' })
      .then((r) => { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
      .catch((e) => { cache.delete(key); throw e; });
    cache.set(key, p);
  }
  return cache.get(key);
}

// Avatar già in cache (senza aspettare), o null.
const ready = new Map();
export function cachedAvatar(id, crc) {
  return ready.get(id + ':' + crc) || null;
}
export async function ensureAvatar(id, crc) {
  const av = await loadAvatar(id, crc);
  ready.set(id + ':' + crc, av);
  return av;
}
