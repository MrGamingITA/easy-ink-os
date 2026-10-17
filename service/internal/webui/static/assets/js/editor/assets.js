// Avatar (con le ancore) e accessori per il renderer dell'editor. Il renderer li chiede in modo sincrono:
// ensure(model) li carica prima; un errore lascia null e il renderer ripiega sulla Goccia.
// Avatar = JSON di /api/avatars/{id} con le ancore di /anchors, come loadWithAnchors (tools/avatar/lib/anchors.mjs).
import { loadAvatar } from '../avatar.js';
import { loadAnchors, loadAccessory } from '../api.js';

const DEFAULT_LOAD = { avatar: (id) => loadAvatar(id), anchors: (id) => loadAnchors(id, ''), accessory: (id) => loadAccessory(id, '') };
const RETRY_MS = 10000;
const MISSING = Symbol('missing');
// 404 = non esiste (definitivo: null, ripiego sulla Goccia); ogni altro errore si riprova più tardi
const gone = (e) => /\bHTTP 404\b/.test(String(e?.message || e));

export function createAssets(load = DEFAULT_LOAD, { now = Date.now } = {}) {
  const avs = new Map(), accs = new Map();   // id → valore | null (non esiste)
  const failed = new Map();                  // chiave → quando riprovare
  const pending = new Map();

  async function fetchAvatar(id) {
    let av;
    try { av = await load.avatar(id); } catch (e) { return gone(e) ? null : MISSING; }
    const anc = await Promise.resolve().then(() => load.anchors(id)).catch(() => null);
    return anc && anc.anchors && !av.anchors ? { ...av, anchors: anc.anchors } : av;
  }
  const fetchAccessory = (id) => Promise.resolve().then(() => load.accessory(id)).catch((e) => (gone(e) ? null : MISSING));

  function want(map, id, fetcher) {
    const key = (map === avs ? 'a:' : 'x:') + id;
    if (typeof id !== 'string' || !id || map.has(id) || (failed.get(key) ?? 0) > now()) return null;
    if (!pending.has(key)) {
      pending.set(key, fetcher(id).then((v) => {
        pending.delete(key);
        if (v === MISSING) { failed.set(key, now() + RETRY_MS); return false; }
        failed.delete(key);
        map.set(id, v ?? null);
        return true;
      }));
    }
    return pending.get(key);
  }

  return {
    // carica quello che il modello può chiedere; true se è arrivato qualcosa di nuovo (da ridisegnare)
    async ensure(model) {
      const ps = [];
      const ids = new Set(['goccia', model?.set?.defAvatar, ...(Array.isArray(model?.sessions) ? model.sessions.map((s) => s?.avatar) : [])]);
      for (const id of ids) { const p = want(avs, id, fetchAvatar); if (p) ps.push(p); }
      const p = want(accs, model?.pet?.wear, fetchAccessory); if (p) ps.push(p);
      return (await Promise.all(ps)).some(Boolean);
    },
    avatar: (id) => avs.get(id) ?? null,
    accessory: (id) => accs.get(id) ?? null,
  };
}
