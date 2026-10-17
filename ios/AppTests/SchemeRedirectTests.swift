import XCTest
import WebKit
import CalamaioKit

// M4 del Task 10: WebKit non segue un 3xx restituito dal gestore dello schema (né navigazione né fetch: il fetch vede
// il 302 con redirected=false). Per i documenti ci pensa Proxy.redirectPage; qui si prova dal vivo che si arriva alla
// pagina giusta e che niente si carica fuori dallo schema. Osservazioni stampate con «REDIRECT-OBS».
@MainActor
final class SchemeRedirectTests: XCTestCase {
    final class Fake: Transport {
        var paths: [String] = []
        func start(_ rq: URLRequest, onResponse: @escaping (HTTPURLResponse) -> Void, onData: @escaping (Data) -> Void,
                   onDone: @escaping (Error?) -> Void) -> () -> Void {
            let path = rq.url?.path ?? ""
            paths.append(path)
            let (status, headers, body): (Int, [String: String], String) = switch path {
            case "/r-top": (302, ["Location": "/dest.html"], "")
            case "/r-https-top": (302, ["Location": "https://192.168.1.20:8767/dest.html"], "")
            case "/r-off-top": (302, ["Location": "http://127.0.0.1:9/evil"], "")
            case "/r-js-top": (302, ["Location": "javascript:alert(1)"], "")
            case "/api/r": (302, ["Location": "/api/target"], "")
            case "/api/r-off": (302, ["Location": "http://127.0.0.1:9/x"], "")
            case "/api/target": (200, ["Content-Type": "application/json"], #"{"t":1}"#)
            case "/dest.html": (200, ["Content-Type": "text/html; charset=utf-8"],
                                #"<!doctype html><meta charset="utf-8"><title>dest</title><script type="module" src="/d.js"></script>"#)
            case "/d.js": (200, ["Content-Type": "text/javascript"], Self.js)
            default: (404, ["Content-Type": "text/plain"], "no")
            }
            var h = headers
            h["Content-Security-Policy"] = Page.csp
            DispatchQueue.main.async {
                onResponse(HTTPURLResponse(url: rq.url!, statusCode: status, httpVersion: "HTTP/1.1", headerFields: h)!)
                if !body.isEmpty { onData(Data(body.utf8)) }
                onDone(nil)
            }
            return {}
        }

        static let js = #"""
        const done = (o) => window.webkit.messageHandlers.done.postMessage(JSON.stringify(o));
        const probe = async (p) => {
          try { const r = await fetch(p); return { status: r.status, url: r.url, redirected: r.redirected, body: await r.text() }; }
          catch (e) { return { error: String(e && e.message || e) }; }
        };
        (async () => { done({ page: location.href, rel: await probe('/api/r'), off: await probe('/api/r-off') }); })();
        """#
    }

    final class Nav: NSObject, WKNavigationDelegate, WKScriptMessageHandler {
        var actions: [(String, NavDecision)] = []
        var redirects: [String] = []
        var ended: (String) -> Void = { _ in }
        var message: String?
        var onMessage: () -> Void = {}

        func webView(_ webView: WKWebView, decidePolicyFor a: WKNavigationAction) async -> WKNavigationActionPolicy {
            let d = NavigationPolicy.decide(url: a.request.url, shouldDownload: a.shouldPerformDownload)
            actions.append((a.request.url?.absoluteString ?? "nil", d))
            return d == .allow ? .allow : .cancel
        }
        var responses: [String] = []
        func webView(_ webView: WKWebView, decidePolicyFor r: WKNavigationResponse) async -> WKNavigationResponsePolicy {
            let st = (r.response as? HTTPURLResponse)?.statusCode ?? -1
            responses.append("\(type(of: r.response)) \(st) canShow=\(r.canShowMIMEType) mime=\(r.response.mimeType ?? "nil")")
            return .allow
        }
        func webView(_ webView: WKWebView, didReceiveServerRedirectForProvisionalNavigation navigation: WKNavigation!) {
            redirects.append(webView.url?.absoluteString ?? "nil")
        }
        func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) { ended("finish " + (webView.url?.absoluteString ?? "nil")) }
        func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) { ended("fail \(error)") }
        func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
            let e = error as NSError
            ended("failProvisional \(e.domain) \(e.code)")
        }
        func userContentController(_ ucc: WKUserContentController, didReceive m: WKScriptMessage) {
            message = m.body as? String
            onMessage()
        }
    }

    func testRedirectsFromSchemeHandler() async throws {
        let up = Upstream(endpoint: Endpoint(host: "127.0.0.1", port: 1), token: String(repeating: "e", count: 64))
        let fake = Fake()
        let handler = SchemeHandler(core: ProxyCore(transport: fake, upstream: { up }))
        let nav = Nav()
        let cfg = WKWebViewConfiguration()
        cfg.setURLSchemeHandler(handler, forURLScheme: Proxy.scheme)
        cfg.userContentController.add(nav, name: "done")
        cfg.websiteDataStore = .nonPersistent()
        let wv = WKWebView(frame: CGRect(x: 0, y: 0, width: 393, height: 852), configuration: cfg)
        wv.navigationDelegate = nav

        func go(_ path: String, waitMessage: Bool = false) async -> String {
            var result = "timeout"
            let exp = expectation(description: path)
            var fired = false
            let fire = { if !fired { fired = true; exp.fulfill() } }
            nav.ended = { r in result = r; if !waitMessage { fire() } }
            nav.message = nil
            nav.onMessage = { fire() }
            nav.actions = []
            nav.redirects = []
            nav.responses = []
            wv.load(URLRequest(url: URL(string: "calamaio-app://ui" + path)!))
            await fulfillment(of: [exp], timeout: 15)
            try? await Task.sleep(for: .milliseconds(300))
            let acts = nav.actions.map { "\($0.0)=\($0.1)" }.joined(separator: ", ")
            print("REDIRECT-OBS \(path): end=\(result) url=\(wv.url?.absoluteString ?? "nil") actions=[\(acts)] redirects=\(nav.redirects) responses=\(nav.responses) msg=\(nav.message ?? "-")")
            return result
        }

        // fetch dalla pagina: 302 relativo e 302 verso http://
        _ = await go("/dest.html", waitMessage: true)
        let msg = try XCTUnwrap(nav.message)
        let r = try XCTUnwrap(try JSONSerialization.jsonObject(with: Data(msg.utf8)) as? [String: Any])
        print("REDIRECT-OBS fetch: \(r)")
        let off = r["off"] as? [String: Any]
        XCTAssertNotEqual(off?["status"] as? Int, 200) // mai una risposta da fuori dello schema

        // navigazione principale: il 302 (relativo o https del PC, riportato sullo schema) arriva alla pagina giusta
        // attraverso la pagina dell'app (Proxy.redirectPage); http:// e javascript: diventano 502, mai caricati
        for p in ["/r-top", "/r-https-top"] {
            _ = await go(p, waitMessage: true)
            XCTAssertEqual(wv.url?.absoluteString, "calamaio-app://ui/dest.html", p)
            XCTAssertNotNil(nav.message, p)
        }
        for p in ["/r-off-top", "/r-js-top"] {
            _ = await go(p)
            XCTAssertEqual(wv.url?.absoluteString, "calamaio-app://ui" + p, p)
            for (u, d) in nav.actions where !u.hasPrefix("calamaio-app://ui") && u != "about:blank" {
                XCTAssertEqual(d, .cancel, u)
            }
        }
        XCTAssertFalse(fake.paths.contains("/evil"))
        print("REDIRECT-OBS transport paths: \(fake.paths)")
    }
}
