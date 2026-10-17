/*
 * dc-lite.js · Calamaio OS, versione da browser
 *
 * Interprete minimo dei template delle tavole di design:
 *   {{nome}} e {{oggetto.campo}}  segnaposto nel testo e negli attributi
 *   <sc-if value="{{x}}">          mostra il contenuto se x è vero
 *   <sc-for list="{{l}}" as="r">   ripete il contenuto per ogni elemento di l
 *   onClick="{{fn}}", onPointerDown="{{fn}}" …   eventi
 *   ref="{{fn}}"                   fn(elemento) dopo il disegno
 *
 * Nessuna dipendenza e nessun server: le pagine si aprono anche con doppio clic.
 */
(function (global) {
  'use strict';

  var ONE = /^\s*\{\{\s*([^{}]+?)\s*\}\}\s*$/;
  var ANY = /\{\{\s*([^{}]+?)\s*\}\}/g;
  var HTML_NS = 'http://www.w3.org/1999/xhtml';
  var uid = 0;

  class DCLogic {
    constructor(props) { this.props = props || {}; }
    forceUpdate() { if (this.__host) this.__host.schedule(); }
    renderVals() { return {}; }
  }

  /* ---------- lettura dei valori ---------- */
  function get(path, scopes) {
    if (path === 'true') return true;
    if (path === 'false') return false;
    if (path === 'null') return null;
    if (/^-?\d+(?:\.\d+)?$/.test(path)) return parseFloat(path);
    var parts = path.split('.'), v, found = false;
    for (var i = scopes.length - 1; i >= 0; i--) {
      var s = scopes[i];
      if (s != null && typeof s === 'object' && parts[0] in s) { v = s[parts[0]]; found = true; break; }
    }
    if (!found) return undefined;
    for (var k = 1; k < parts.length; k++) { if (v == null) return undefined; v = v[parts[k]]; }
    return v;
  }
  function txt(v) { return v == null || v === false || v === true ? '' : String(v); }
  function attrVal(name, v) {
    if (v == null) return null;
    var aria = name.indexOf('aria-') === 0;
    if (v === false) return aria ? 'false' : null;
    if (v === true) return aria ? 'true' : '';
    return String(v);
  }
  function holePath(val) { var m = ONE.exec(val || ''); return m ? m[1].trim() : null; }
  function splitHoles(s) {
    if (s.indexOf('{{') < 0) return null;
    var parts = [], last = 0, m;
    ANY.lastIndex = 0;
    while ((m = ANY.exec(s))) {
      if (m.index > last) parts.push(s.slice(last, m.index));
      parts.push({ p: m[1].trim() });
      last = m.index + m[0].length;
    }
    if (last < s.length) parts.push(s.slice(last));
    return parts;
  }
  function join(parts, scopes) {
    var out = '';
    for (var i = 0; i < parts.length; i++) out += typeof parts[i] === 'string' ? parts[i] : txt(get(parts[i].p, scopes));
    return out;
  }

  /* ---------- compilazione del template ---------- */
  function compileList(node) {
    var out = [];
    node.childNodes.forEach(function (ch) { var c = compileNode(ch); if (c) out.push(c); });
    return out;
  }
  function compileNode(n) {
    if (n.nodeType === 3) {
      var s = n.data;
      if (!s.trim() && s.indexOf('\n') >= 0) return null;
      var parts = splitHoles(s);
      return parts ? { k: 'text', parts: parts } : { k: 'text', s: s };
    }
    if (n.nodeType !== 1) return null;
    var tag = n.localName;
    if (tag === 'helmet' || tag === 'script') return null;
    if (tag === 'sc-if') return { k: 'if', p: holePath(n.getAttribute('value')), kids: compileList(n) };
    if (tag === 'sc-for') return { k: 'for', p: holePath(n.getAttribute('list')), as: n.getAttribute('as') || 'item', kids: compileList(n) };
    var el = { k: 'el', id: ++uid, tag: tag, ns: n.namespaceURI || HTML_NS, attrs: [], ev: [], ref: null, kids: null };
    var dynamic = false;
    Array.prototype.forEach.call(n.attributes, function (a) {
      var name = a.name, val = a.value;
      if (name.indexOf('hint-') === 0) return;
      if (name === 'ref') { el.ref = holePath(val); dynamic = true; return; }
      if (/^on[a-z]+$/.test(name) && val.indexOf('{{') >= 0) { el.ev.push({ type: name.slice(2), p: holePath(val) }); dynamic = true; return; }
      var one = holePath(val);
      if (one) { el.attrs.push({ name: name, p: one }); dynamic = true; return; }
      var parts = splitHoles(val);
      if (parts) { el.attrs.push({ name: name, parts: parts }); dynamic = true; return; }
      el.attrs.push({ name: name, s: val });
    });
    el.kids = compileList(n.localName === 'template' ? n.content : n);
    var plain = el.kids.every(function (c) { return c.k === 'static' || (c.k === 'text' && !c.parts); });
    if (!dynamic && plain) return { k: 'static', id: el.id, node: n };
    return el;
  }

  /* ---------- disegno ---------- */
  function render(list, scopes, out) {
    for (var i = 0; i < list.length; i++) {
      var c = list[i];
      if (c.k === 'text') out.push({ k: 'text', s: c.parts ? join(c.parts, scopes) : c.s });
      else if (c.k === 'static') out.push(c);
      else if (c.k === 'if') { if (get(c.p, scopes)) render(c.kids, scopes, out); }
      else if (c.k === 'for') {
        var arr = get(c.p, scopes) || [];
        for (var j = 0; j < arr.length; j++) { var sc = {}; sc[c.as] = arr[j]; render(c.kids, scopes.concat([sc]), out); }
      } else {
        var v = { k: 'el', c: c, attrs: {}, ev: {}, ref: null, kids: [] };
        for (var a = 0; a < c.attrs.length; a++) {
          var at = c.attrs[a];
          v.attrs[at.name] = 's' in at ? at.s : at.p ? attrVal(at.name, get(at.p, scopes)) : join(at.parts, scopes);
        }
        for (var e = 0; e < c.ev.length; e++) v.ev[c.ev[e].type] = c.ev[e].p ? get(c.ev[e].p, scopes) : null;
        if (c.ref) v.ref = get(c.ref, scopes);
        render(c.kids, scopes, v.kids);
        out.push(v);
      }
    }
    return out;
  }
  function same(n, v) {
    if (v.k === 'text') return n.nodeType === 3;
    if (v.k === 'static') return n.__dcS === v.id;
    return n.nodeType === 1 && n.__dcE === v.c.id;
  }
  function create(v, refs) {
    if (v.k === 'text') return document.createTextNode(v.s);
    if (v.k === 'static') { var s = document.importNode(v.node, true); s.__dcS = v.id; return s; }
    var el = document.createElementNS(v.c.ns, v.c.tag);
    el.__dcE = v.c.id;
    update(el, v, refs);
    return el;
  }
  function update(el, v, refs) {
    if (v.k === 'text') { if (el.data !== v.s) el.data = v.s; return; }
    if (v.k === 'static') return;
    var prev = el.__dcA || {}, name;
    for (name in v.attrs) {
      var val = v.attrs[name];
      if (val == null) { if (el.hasAttribute(name)) el.removeAttribute(name); }
      else if (el.getAttribute(name) !== val) el.setAttribute(name, val);
    }
    for (name in prev) if (!(name in v.attrs) && el.hasAttribute(name)) el.removeAttribute(name);
    el.__dcA = v.attrs;
    el.__dcV = v.ev;
    if (!el.__dcL) el.__dcL = {};
    Object.keys(v.ev).forEach(function (type) {
      if (el.__dcL[type]) return;
      el.__dcL[type] = true;
      el.addEventListener(type, function (ev) {
        var f = el.__dcV && el.__dcV[type];
        if (typeof f === 'function') f(ev);
      });
    });
    patch(el, v.kids, refs);
    if (typeof v.ref === 'function' && el.__dcR !== v.ref) refs.push([v.ref, el]);
  }
  function patch(parent, vk, refs) {
    var cur = parent.firstChild;
    for (var i = 0; i < vk.length; i++) {
      var v = vk[i];
      if (cur && same(cur, v)) { update(cur, v, refs); cur = cur.nextSibling; }
      else parent.insertBefore(create(v, refs), cur);
    }
    while (cur) { var nx = cur.nextSibling; parent.removeChild(cur); cur = nx; }
  }

  /* ---------- montaggio ---------- */
  function toTemplate(t) {
    if (typeof t !== 'string') return t;
    var el;
    if (t.trim().charAt(0) !== '<') return document.querySelector(t);
    el = document.createElement('template');
    el.innerHTML = t;
    return el;
  }
  function mount(target, template, Cls, props) {
    var box = typeof target === 'string' ? document.querySelector(target) : target;
    var tpl = toTemplate(template);
    var compiled = compileList(tpl.content || tpl);
    var inst = new Cls(Object.assign({}, props || {}));
    inst.props = Object.assign({}, props || {});
    var host = {
      queued: false,
      render: function () {
        host.queued = false;
        var vals;
        try { vals = inst.renderVals() || {}; } catch (e) { console.error(e); return; }
        var refs = [];
        patch(box, render(compiled, [vals], []), refs);
        refs.forEach(function (r) { r[1].__dcR = r[0]; try { r[0](r[1]); } catch (e) { console.error(e); } });
      },
      schedule: function () {
        if (host.queued) return;
        host.queued = true;
        (global.queueMicrotask || function (f) { Promise.resolve().then(f); })(host.render);
      }
    };
    inst.__host = host;
    host.render();
    if (typeof inst.componentDidMount === 'function') inst.componentDidMount();
    DC.instances.push(inst);
    return inst;
  }

  var DC = { mount: mount, instances: [] };
  global.DCLogic = DCLogic;
  global.DC = DC;
})(window);
