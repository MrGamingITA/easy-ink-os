import WebKit
import CryptoKit

// Gestore dello schema «spike» per la prova: serve la pagina, rimanda indietro cosa ha ricevuto
// (corpo in linea, corpo a stream, SHA-256) e manda un SSE con un evento al secondo.
final class SpikeHandler: NSObject, WKURLSchemeHandler {
    // la stessa CSP della webapp (service/internal/api/web.go)
    static let csp = "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; " +
        "font-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'"
    private var stopped = Set<ObjectIdentifier>()

    static func hex(_ d: Data) -> String { SHA256.hash(data: d).map { String(format: "%02x", $0) }.joined() }

    func webView(_ webView: WKWebView, start task: WKURLSchemeTask) {
        let path = task.request.url?.path ?? ""
        switch path {
        case "/harness.html": reply(task, "text/html; charset=utf-8", Data(SpikePage.html.utf8))
        case "/harness.js": reply(task, "text/javascript; charset=utf-8", Data(SpikePage.js.utf8))
        case "/sse": sse(task)
        default:
            if path.hasPrefix("/echo/") { echo(task) } else { reply(task, "text/plain", Data(), status: 404) }
        }
    }

    func webView(_ webView: WKWebView, stop task: WKURLSchemeTask) { stopped.insert(ObjectIdentifier(task)) }

    private func reply(_ t: WKURLSchemeTask, _ ctype: String, _ body: Data, status: Int = 200) {
        let r = HTTPURLResponse(url: t.request.url!, statusCode: status, httpVersion: "HTTP/1.1",
                                headerFields: ["Content-Type": ctype, "Content-Security-Policy": Self.csp, "Cache-Control": "no-store"])!
        t.didReceive(r)
        t.didReceive(body)
        t.didFinish()
    }

    private func echo(_ t: WKURLSchemeTask) {
        let rq = t.request
        let inline = rq.httpBody
        var streamed: Data?
        if let s = rq.httpBodyStream {
            s.open()
            var d = Data()
            var buf = [UInt8](repeating: 0, count: 64 << 10)
            while true {
                let n = s.read(&buf, maxLength: buf.count)
                if n <= 0 { break }
                d.append(buf, count: n)
            }
            s.close()
            streamed = d
        }
        let got = inline ?? streamed ?? Data()
        let info: [String: Any] = [
            "path": rq.url?.path ?? "", "method": rq.httpMethod ?? "",
            "ctype": rq.value(forHTTPHeaderField: "Content-Type") ?? "",
            "origin": rq.value(forHTTPHeaderField: "Origin") ?? "",
            "inline": inline?.count ?? -1, "stream": streamed?.count ?? -1,
            "sha": Self.hex(got),
            "multipart": got.range(of: Data("filename=\"x.gb\"".utf8)) != nil,
        ]
        reply(t, "application/json", try! JSONSerialization.data(withJSONObject: info))
    }

    // tre eventi a 0, 1 e 2 s, fine del flusso a 3 s
    private func sse(_ t: WKURLSchemeTask) {
        let id = ObjectIdentifier(t)
        t.didReceive(HTTPURLResponse(url: t.request.url!, statusCode: 200, httpVersion: "HTTP/1.1",
                                     headerFields: ["Content-Type": "text/event-stream; charset=utf-8", "Cache-Control": "no-store"])!)
        for i in 0..<4 {
            DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(1000 * i)) { [weak self] in
                guard let self, !self.stopped.contains(id) else { return }
                if i < 3 { t.didReceive(Data("event: tick\ndata: \(i)\n\n".utf8)) } else { t.didFinish() }
            }
        }
    }
}

enum SpikePage {
    static let html = """
    <!doctype html><meta charset="utf-8"><title>prova</title><script type="module" src="/harness.js"></script>
    """

    static let js = """
    const big = new Uint8Array(2 * 1024 * 1024);
    for (let i = 0; i < big.length; i++) big[i] = i & 255;
    const out = { csp: [] };
    document.addEventListener('securitypolicyviolation', (e) => out.csp.push(e.violatedDirective + ' ' + e.blockedURI));
    const post = async (path, init) => {
      try { const r = await fetch(path, Object.assign({ method: 'POST' }, init)); return await r.json(); }
      catch (e) { return { error: String(e) }; }
    };
    async function run() {
      out.json = await post('/echo/json', { headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ a: 1, s: 'è' }) });
      out.buffer = await post('/echo/buffer', { headers: { 'Content-Type': 'application/octet-stream' }, body: big });
      out.blob = await post('/echo/blob', { headers: { 'Content-Type': 'application/octet-stream' }, body: new Blob([big]) });
      const fd = new FormData();
      fd.append('name', 'x');
      fd.append('rom', new File([big.subarray(0, 32768)], 'x.gb', { type: 'application/octet-stream' }));
      out.form = await post('/echo/form', { body: fd });
      out.sse = await new Promise((res) => {
        if (!('EventSource' in window)) return res({ error: 'EventSource assente' });
        const t0 = performance.now(), got = [];
        const es = new EventSource('/sse');
        const end = (x) => { es.close(); res(x); };
        es.addEventListener('tick', (e) => { got.push([Number(e.data), Math.round(performance.now() - t0)]); if (got.length === 3) end({ got }); });
        es.addEventListener('error', () => end({ error: 'error', got }));
        setTimeout(() => end({ timeout: true, got }), 8000);
      });
      webkit.messageHandlers.spike.postMessage(JSON.stringify(out));
    }
    run().catch((e) => webkit.messageHandlers.spike.postMessage(JSON.stringify({ fatal: String(e) })));
    """
}

// Riceve il risultato della pagina.
final class SpikeSink: NSObject, WKScriptMessageHandler {
    var result: String?
    var done: (() -> Void)?
    func userContentController(_ ucc: WKUserContentController, didReceive message: WKScriptMessage) {
        result = message.body as? String
        done?()
        done = nil
    }
}
