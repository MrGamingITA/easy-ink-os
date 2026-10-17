// App Calamaio (ramo B1 della prova): lo schema proprio di iOS non riceve i corpi Blob, File e FormData, quindi i
// fetch con metodo diverso da GET/HEAD passano dall'app (messageHandlers.calamaioFetch). Il corpo diventa byte qui:
// new Request(...).arrayBuffer() serializza anche FormData (multipart) e il Content-Type col boundary resta nelle
// intestazioni. Qui non c'è nessun token: lo aggiunge l'app.
(() => {
  const h = window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.calamaioFetch;
  if (!h || window.__calamaioFetchShim) return;
  window.__calamaioFetchShim = true;
  const MAX = 16 << 20; // come Proxy.maxBody: oltre, l'app risponde 413 senza leggere il corpo
  const toB64 = (buf) => {
    const b = new Uint8Array(buf); let s = '';
    for (let i = 0; i < b.length; i += 0x8000) s += String.fromCharCode.apply(null, b.subarray(i, i + 0x8000));
    return btoa(s);
  };
  const fromB64 = (s) => { const bin = atob(s || ''); const b = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) b[i] = bin.charCodeAt(i); return b; };
  const aborted = () => new DOMException('The operation was aborted.', 'AbortError');
  const orig = window.fetch.bind(window);
  // id di ogni richiesta del ponte: serve a {cancel: id} quando la pagina annulla (AbortSignal)
  const prefix = Math.random().toString(36).slice(2, 10) + '-';
  let seq = 0;
  window.fetch = async (input, init) => {
    const rq = new Request(input, init);
    if (rq.method === 'GET' || rq.method === 'HEAD') return orig(input, init);
    const signal = rq.signal;
    if (signal && signal.aborted) throw aborted();
    const body = await rq.arrayBuffer();
    if (signal && signal.aborted) throw aborted(); // annullata mentre si leggeva il corpo
    const headers = {};
    rq.headers.forEach((v, k) => { headers[k] = v; });
    const id = prefix + (++seq);
    const msg = { id, method: rq.method, url: rq.url, headers };
    if (body.byteLength > MAX) msg.tooLarge = true; else msg.body = toB64(body);
    const sent = h.postMessage(msg).catch((e) => { throw new TypeError(String((e && e.message) || e)); });
    let onAbort = null;
    const stop = signal ? new Promise((_, rej) => {
      onAbort = () => { h.postMessage({ cancel: id }).catch(() => {}); rej(aborted()); };
      signal.addEventListener('abort', onAbort, { once: true });
    }) : null;
    let r;
    try { r = await (stop ? Promise.race([sent, stop]) : sent); }
    finally {
      if (onAbort) signal.removeEventListener('abort', onAbort);
      sent.catch(() => {}); // se ha vinto l'annullamento, il rifiuto tardivo di sent non resta senza gestore
    }
    // M4: new Response accetta solo 200–599; fuori, un errore di rete come quello di fetch (mai un RangeError)
    if (!r || typeof r.status !== 'number' || r.status < 200 || r.status > 599) throw new TypeError('Load failed');
    const empty = r.status === 204 || r.status === 205 || r.status === 304;
    return new Response(empty ? null : fromB64(r.body), { status: r.status, headers: r.headers });
  };
})();
