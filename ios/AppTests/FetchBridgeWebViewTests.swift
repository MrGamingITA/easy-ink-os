import XCTest
import WebKit
import CryptoKit
import CalamaioKit

// Ramo B1 dal vivo: in una WKWebView vera con SchemeHandler e FetchBridge, i corpi JSON, ArrayBuffer, Blob, File e
// FormData arrivano interi al trasporto (multipart col boundary del Content-Type), le GET passano dallo schema e
// l'Authorization della pagina non arriva mai.
@MainActor
final class FetchBridgeWebViewTests: XCTestCase {
    struct Seen { let via: String; let rq: URLRequest }

    // Trasporto finto: serve la pagina di prova e risponde alle API con la lunghezza e lo SHA-256 del corpo.
    final class Recorder: Transport {
        let via: String
        let log: (Seen) -> Void
        var cancelled: [String] = []
        init(_ via: String, _ log: @escaping (Seen) -> Void) { self.via = via; self.log = log }

        func start(_ rq: URLRequest, onResponse: @escaping (HTTPURLResponse) -> Void, onData: @escaping (Data) -> Void,
                   onDone: @escaping (Error?) -> Void) -> () -> Void {
            log(Seen(via: via, rq: rq))
            let path = rq.url?.path ?? ""
            if path == "/api/slow" { // non risponde mai: solo l'annullamento la chiude
                return { [weak self] in self?.cancelled.append(path) }
            }
            let (ctype, body): (String, Data) = switch path {
            case "/": ("text/html; charset=utf-8", Data(Page.html.utf8))
            case "/t.js": ("text/javascript; charset=utf-8", Data(Page.js.utf8))
            default: ("application/json", try! JSONSerialization.data(withJSONObject: [
                "n": rq.httpBody?.count ?? -1, "sha": FetchBridgeWebViewTests.hex(rq.httpBody ?? Data())]))
            }
            DispatchQueue.main.async {
                // /api/odd: uno stato fuori da 200–599 (M4), che new Response non accetta
                let status = path == "/api/none" ? 204 : path == "/api/odd" ? 103 : 200
                onResponse(HTTPURLResponse(url: rq.url!, statusCode: status, httpVersion: "HTTP/1.1",
                                           headerFields: ["Content-Type": ctype, "Content-Security-Policy": Page.csp])!)
                if path != "/api/none" { onData(body) }
                onDone(nil)
            }
            return {}
        }
    }

    final class Sink: NSObject, WKScriptMessageHandler {
        var result: String?
        var done: () -> Void = {}
        func userContentController(_ ucc: WKUserContentController, didReceive message: WKScriptMessage) {
            result = message.body as? String
            done()
        }
    }

    nonisolated static func hex(_ d: Data) -> String { SHA256.hash(data: d).map { String(format: "%02x", $0) }.joined() }

    func testBodiesThroughBridge() async throws {
        var seen: [Seen] = []
        let token = String(repeating: "e", count: 64)
        let up = Upstream(endpoint: Endpoint(host: "127.0.0.1", port: 1), token: token)
        let handler = SchemeHandler(core: ProxyCore(transport: Recorder("scheme") { seen.append($0) }, upstream: { up }))
        let bridgeTransport = Recorder("bridge") { seen.append($0) }
        let bridge = FetchBridge(transport: bridgeTransport, upstream: { up })
        let sink = Sink()
        let cfg = WKWebViewConfiguration()
        cfg.setURLSchemeHandler(handler, forURLScheme: Proxy.scheme)
        bridge.install(in: cfg.userContentController)
        cfg.userContentController.add(sink, name: "done")
        cfg.websiteDataStore = .nonPersistent()
        let scene = try XCTUnwrap(UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.first)
        let window = UIWindow(windowScene: scene)
        window.frame = CGRect(x: 0, y: 0, width: 393, height: 852)
        let vc = UIViewController()
        let wv = WKWebView(frame: window.bounds, configuration: cfg)
        vc.view.addSubview(wv)
        window.rootViewController = vc
        window.makeKeyAndVisible()
        defer { window.isHidden = true }

        let exp = expectation(description: "pagina di prova")
        sink.done = { exp.fulfill() }
        wv.load(URLRequest(url: Proxy.startURL))
        await fulfillment(of: [exp], timeout: 60)
        let raw = try XCTUnwrap(sink.result)
        print("BRIDGE-RESULT " + raw)
        let r = try XCTUnwrap(try JSONSerialization.jsonObject(with: Data(raw.utf8)) as? [String: Any])
        XCTAssertNil(r["fatal"], "\(r)")

        let big = Data((0..<(2 << 20)).map { UInt8($0 & 255) })
        let rom = Data((0..<300_000).map { UInt8(($0 * 7) & 255) })
        func one(_ path: String) -> Seen? { seen.first { $0.rq.url?.path == path } }

        // GET dallo schema, corpi dal ponte
        XCTAssertEqual(one("/")?.via, "scheme")
        XCTAssertEqual(one("/api/get")?.via, "scheme")
        for p in ["/api/json", "/api/buffer", "/api/blob", "/api/form", "/api/delete", "/api/auth"] {
            XCTAssertEqual(one(p)?.via, "bridge", p)
        }
        XCTAssertEqual(one("/api/json")?.rq.httpBody, Data(#"{"a":1,"s":"è"}"#.utf8))
        XCTAssertEqual(one("/api/json")?.rq.value(forHTTPHeaderField: "Content-Type"), "application/json")
        XCTAssertEqual(one("/api/buffer")?.rq.httpBody, big)
        XCTAssertEqual(one("/api/blob")?.rq.httpBody, big)
        XCTAssertEqual(one("/api/blob")?.rq.value(forHTTPHeaderField: "Content-Type"), "application/octet-stream")
        XCTAssertEqual(one("/api/delete")?.rq.httpMethod, "DELETE")

        // FormData: il boundary del Content-Type è quello del corpo, il file c'è byte per byte
        let form = try XCTUnwrap(one("/api/form")?.rq)
        let ctype = try XCTUnwrap(form.value(forHTTPHeaderField: "Content-Type"))
        XCTAssertTrue(ctype.hasPrefix("multipart/form-data; boundary="), ctype)
        let boundary = String(ctype.dropFirst("multipart/form-data; boundary=".count))
        let fb = try XCTUnwrap(form.httpBody)
        XCTAssertTrue(fb.starts(with: Data(("--" + boundary + "\r\n").utf8)))
        XCTAssertNotNil(fb.range(of: Data(("--" + boundary + "--").utf8)))
        XCTAssertNotNil(fb.range(of: Data(#"filename="x.gb""#.utf8)))
        XCTAssertNotNil(fb.range(of: rom), "il file nel multipart non è intero")
        XCTAssertNotNil(fb.range(of: Data("Ciao è".utf8)))

        // l'Authorization della pagina non passa: c'è solo quello dell'app
        XCTAssertEqual(one("/api/auth")?.rq.value(forHTTPHeaderField: "Authorization"), "Bearer " + token)
        XCTAssertNil(one("/api/auth")?.rq.value(forHTTPHeaderField: "Cookie"))
        // percorso già codificato: lo stesso, sia dallo schema che dal ponte
        XCTAssertEqual(one("/api/a/b c")?.rq.url?.absoluteString, "https://127.0.0.1:1/api/a%2Fb%20c?q=x%2By")
        XCTAssertEqual(seen.filter { $0.rq.url?.absoluteString == "https://127.0.0.1:1/api/a%2Fb%20c?q=x%2By" }.map(\.via).sorted(),
                       ["bridge", "scheme"])

        // quello che la pagina ha visto: status, corpi delle risposte, 204 vuoto, nessun token
        let got = try XCTUnwrap(r["got"] as? [String: Any])
        XCTAssertEqual((got["blob"] as? [String: Any])?["sha"] as? String, Self.hex(big))
        XCTAssertEqual((got["form"] as? [String: Any])?["n"] as? Int, fb.count)
        XCTAssertEqual(got["none"] as? Int, 204)
        XCTAssertEqual(r["sawToken"] as? Bool, false)
        // M4: stato fuori da 200–599 → errore di rete come fetch (TypeError), non un RangeError da new Response
        XCTAssertEqual(r["odd"] as? String, "TypeError")
        XCTAssertEqual(r["abort"] as? String, "AbortError")
        // annullare un fetch in corso ferma davvero la richiesta verso il PC
        XCTAssertEqual(r["slow"] as? String, "AbortError")
        XCTAssertEqual(bridgeTransport.cancelled, ["/api/slow"])
        // postMessage scritto a mano (senza fetch): metodo o intestazioni con CR/LF rifiutati, niente parte
        XCTAssertEqual(r["direct"] as? [String], ["richiesta non valida", "richiesta non valida", "richiesta non valida"])
        XCTAssertFalse(seen.contains { $0.rq.url?.path == "/api/direct" })
    }
}

enum Page {
    // la stessa CSP della webapp (service/internal/api/web.go)
    static let csp = "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; " +
        "font-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'"
    static let html = """
    <!doctype html><meta charset="utf-8"><title>ponte</title><script type="module" src="/t.js"></script>
    """
    static let js = #"""
    const done = (o) => window.webkit.messageHandlers.done.postMessage(JSON.stringify(o));
    const big = new Uint8Array(2 << 20); for (let i = 0; i < big.length; i++) big[i] = i & 255;
    const rom = new Uint8Array(300000); for (let i = 0; i < rom.length; i++) rom[i] = (i * 7) & 255;
    const post = (p, init) => fetch(p, Object.assign({ method: 'POST' }, init)).then((r) => r.json());
    (async () => {
      const got = {};
      let sawToken = false;
      const check = (r) => { r.headers.forEach((v) => { if (v.includes('eeeeeeee')) sawToken = true; }); return r; };
      got.get = await fetch('/api/get').then(check).then((r) => r.json());
      got.json = await post('/api/json', { headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ a: 1, s: 'è' }) });
      got.buffer = await post('/api/buffer', { body: big.buffer });
      got.blob = await post('/api/blob', { body: new Blob([big], { type: 'application/octet-stream' }) });
      const fd = new FormData();
      fd.append('nome', 'Ciao è');
      fd.append('rom', new File([rom], 'x.gb', { type: 'application/octet-stream' }));
      got.form = await post('/api/form', { body: fd });
      got.del = await fetch('/api/delete', { method: 'DELETE' }).then((r) => r.json());
      got.auth = await post('/api/auth', { headers: { Authorization: 'Bearer pagina', 'Content-Type': 'text/plain' }, body: 'x' });
      got.none = (await fetch('/api/none', { method: 'POST', body: 'x' })).status;
      await fetch('/api/a%2Fb%20c?q=x%2By');
      await fetch('/api/a%2Fb%20c?q=x%2By', { method: 'POST', body: 'x' });
      let odd = '';
      try { await fetch('/api/odd', { method: 'POST', body: 'x' }); odd = 'ok'; } catch (e) { odd = e.name; }
      let abort = '';
      try { const c = new AbortController(); c.abort(); await fetch('/api/aborted', { method: 'POST', body: 'x', signal: c.signal }); }
      catch (e) { abort = e.name; }
      let slow = '';
      try {
        const c = new AbortController();
        setTimeout(() => c.abort(), 200);
        await fetch('/api/slow', { method: 'POST', body: big, signal: c.signal });
      } catch (e) { slow = e.name; }
      await new Promise((r) => setTimeout(r, 300)); // il messaggio di annullamento arriva all'app
      const h = window.webkit.messageHandlers.calamaioFetch;
      const direct = [];
      for (const m of [
        { id: 'd1', method: 'GET\r\nX-A: b', url: 'calamaio-app://ui/api/direct', headers: {}, body: '' },
        { id: 'd2', method: 'POST', url: 'calamaio-app://ui/api/direct', headers: { 'content-type': 'a\r\nAuthorization: Bearer x' }, body: '' },
        { id: 'd3', method: 'TRACE', url: 'calamaio-app://ui/api/direct', headers: {}, body: '' },
      ]) {
        try { await h.postMessage(m); direct.push('ok'); } catch (e) { direct.push(String(e.message || e)); }
      }
      done({ got, sawToken, odd, abort, slow, direct });
    })().catch((e) => done({ fatal: String(e && e.stack || e) }));
    """#
}
