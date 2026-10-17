import XCTest
@testable import CalamaioKit

@MainActor
final class FetchBridgeTests: XCTestCase {
    func testRequestFromPageMessage() throws {
        let up = Upstream(endpoint: ep, token: token)
        let msg: [String: Any] = ["method": "POST", "url": "calamaio-app://ui/api/avatars",
                                  "headers": ["content-type": "application/json", "authorization": "Bearer pagina", "origin": "x"],
                                  "body": Data("{\"a\":1}".utf8).base64EncodedString()]
        let rq = try FetchBridge.request(from: msg, upstream: up)
        XCTAssertEqual(rq.url?.absoluteString, "https://192.168.1.20:8767/api/avatars")
        XCTAssertEqual(rq.httpBody, Data("{\"a\":1}".utf8))
        XCTAssertEqual(rq.value(forHTTPHeaderField: "Authorization"), "Bearer " + token)
        XCTAssertNil(rq.value(forHTTPHeaderField: "Origin"))
        XCTAssertThrowsError(try FetchBridge.request(from: ["method": "POST", "url": "https://evil.example/"], upstream: up))
        XCTAssertFalse(FetchBridge.shimSource.isEmpty)
        XCTAssertFalse(FetchBridge.shimSource.lowercased().contains("bearer")) // il token non passa mai dal JS
    }

    // Il corpo multipart arriva byte per byte, con il Content-Type (e il boundary) messo dal browser.
    func testMultipartKeptAndPathUntouched() throws {
        let up = Upstream(endpoint: ep, token: token)
        var body = Data("--b0undary\r\nContent-Disposition: form-data; name=\"f\"; filename=\"x.gb\"\r\n\r\n".utf8)
        body.append(contentsOf: [0, 1, 0xff, 0xfe])
        body.append(Data("\r\n--b0undary--\r\n".utf8))
        let rq = try FetchBridge.request(from: ["method": "POST", "url": "calamaio-app://ui/api/gb/games?name=a%2Bb.gb",
                                                "headers": ["content-type": "multipart/form-data; boundary=b0undary", "cookie": "a=b"],
                                                "body": body.base64EncodedString()], upstream: up)
        XCTAssertEqual(rq.url?.absoluteString, "https://192.168.1.20:8767/api/gb/games?name=a%2Bb.gb")
        XCTAssertEqual(rq.httpBody, body)
        XCTAssertEqual(rq.value(forHTTPHeaderField: "Content-Type"), "multipart/form-data; boundary=b0undary")
        XCTAssertNil(rq.value(forHTTPHeaderField: "Cookie"))
    }

    func testBadMessages() {
        let up = Upstream(endpoint: ep, token: token)
        for bad: Any in ["x", ["url": "calamaio-app://ui/a"], ["method": "POST", "url": "calamaio-app://ui/a", "body": "%%%"],
                         ["method": "POST", "url": "calamaio-app://evil/a", "body": ""]] {
            XCTAssertThrowsError(try FetchBridge.request(from: bad, upstream: up), "\(bad)")
        }
        XCTAssertThrowsError(try FetchBridge.request(from: ["method": "POST", "url": "calamaio-app://ui/a", "tooLarge": true], upstream: up)) {
            XCTAssertEqual($0 as? Proxy.ProxyError, .bodyTooLarge)
        }
    }

    // Il ponte senza WKWebView: risposta completa in base64, 413, 503, errori senza dettagli della rete.
    func testHandleReplies() throws {
        let transport = FakeTransport()
        var up: Upstream? = Upstream(endpoint: ep, token: token)
        let bridge = FetchBridge(transport: transport, upstream: { up })
        var replies: [(Any?, String?)] = []
        var msg: [String: Any] = ["id": "r1", "method": "POST", "url": "calamaio-app://ui/api/backups", "headers": [:], "body": "AAEC"]

        bridge.handle(msg) { replies.append(($0, $1)) }
        XCTAssertEqual(transport.requests.first?.httpBody, Data([0, 1, 2]))
        transport.onResponse?(HTTPURLResponse(url: transport.requests[0].url!, statusCode: 201, httpVersion: "HTTP/1.1",
                                              headerFields: ["Content-Type": "application/json", "Location": "https://192.168.1.20:8767/api/backups/b1",
                                                             "Set-Cookie": "a=b"])!)
        transport.onData?(Data("{\"a\"".utf8))
        transport.onData?(Data(":1}".utf8))
        XCTAssertTrue(replies.isEmpty)
        transport.onDone?(nil)
        let r = try XCTUnwrap(replies.first?.0 as? [String: Any])
        XCTAssertEqual(r["status"] as? Int, 201)
        XCTAssertEqual(r["body"] as? String, Data("{\"a\":1}".utf8).base64EncodedString())
        let h = try XCTUnwrap(r["headers"] as? [String: String])
        XCTAssertEqual(h["Content-Type"], "application/json")
        XCTAssertEqual(h["Location"], "calamaio-app://ui/api/backups/b1")
        XCTAssertNil(h["Set-Cookie"])

        replies = []
        bridge.handle(["id": "r2", "method": "POST", "url": "calamaio-app://ui/api/avatars", "tooLarge": true]) { replies.append(($0, $1)) }
        XCTAssertEqual((replies.first?.0 as? [String: Any])?["status"] as? Int, 413)
        let b413 = Data(base64Encoded: (replies.first?.0 as? [String: Any])?["body"] as? String ?? "") ?? Data()
        XCTAssertTrue(String(decoding: b413, as: UTF8.self).contains("16 MiB"))

        replies = []
        msg["id"] = "r3"
        bridge.handle(msg) { replies.append(($0, $1)) }
        transport.onDone?(URLError(.secureConnectionFailed, userInfo: [NSLocalizedDescriptionKey: "https://192.168.1.20:8767"]))
        XCTAssertNil(replies.first?.0 ?? nil)
        XCTAssertEqual(replies.first?.1, Texts.pcUnreachable)

        replies = []
        bridge.handle(["id": "r4", "method": "POST", "url": "https://evil.example/"]) { replies.append(($0, $1)) }
        XCTAssertEqual(replies.first?.1, "richiesta non valida")

        replies = []
        up = nil
        msg["id"] = "r5"
        let n = transport.requests.count
        bridge.handle(msg) { replies.append(($0, $1)) }
        XCTAssertEqual(transport.requests.count, n)
        XCTAssertEqual((replies.first?.0 as? [String: Any])?["status"] as? Int, 503)
        XCTAssertEqual(replies.count, 1)
    }

    // I1: un 401 dal PC avvisa l'app (ricontrollo del telefono revocato); la pagina riceve comunque il 401.
    func testUnauthorizedCallsBack() throws {
        let transport = FakeTransport()
        let bridge = FetchBridge(transport: transport, upstream: { Upstream(endpoint: ep, token: token) })
        var n = 0
        bridge.onUnauthorized = { n += 1 }
        var replies: [(Any?, String?)] = []
        let msg: [String: Any] = ["id": "u1", "method": "POST", "url": "calamaio-app://ui/api/avatars", "headers": [:], "body": ""]
        bridge.handle(msg) { replies.append(($0, $1)) }
        transport.respond(200, "application/json")
        transport.onDone?(nil)
        XCTAssertEqual(n, 0)
        var m2 = msg
        m2["id"] = "u2"
        bridge.handle(m2) { replies.append(($0, $1)) }
        transport.respond(401, "application/json")
        transport.onDone?(nil)
        XCTAssertEqual(n, 1)
        XCTAssertEqual((replies.last?.0 as? [String: Any])?["status"] as? Int, 401)
    }

    func testShimShape() {
        let s = FetchBridge.shimSource
        XCTAssertTrue(s.contains("calamaioFetch"))
        XCTAssertTrue(s.contains("arrayBuffer")) // Blob, File e FormData diventano byte nel JS (ramo B1)
        XCTAssertFalse(s.lowercased().contains("authorization"))
    }

    // Trasporto finto con più richieste in corso.
    final class Multi: Transport {
        struct Call { let rq: URLRequest; let onResponse: (HTTPURLResponse) -> Void; let onData: (Data) -> Void; let onDone: (Error?) -> Void }
        var calls: [Call] = []
        var cancelled: [Int] = []
        func start(_ rq: URLRequest, onResponse: @escaping (HTTPURLResponse) -> Void, onData: @escaping (Data) -> Void,
                   onDone: @escaping (Error?) -> Void) -> () -> Void {
            calls.append(Call(rq: rq, onResponse: onResponse, onData: onData, onDone: onDone))
            let i = calls.count - 1
            return { [weak self] in self?.cancelled.append(i) }
        }
        func ok(_ i: Int, _ d: Data = Data()) {
            calls[i].onResponse(HTTPURLResponse(url: calls[i].rq.url!, statusCode: 200, httpVersion: "HTTP/1.1", headerFields: [:])!)
            if !d.isEmpty { calls[i].onData(d) }
            calls[i].onDone(nil)
        }
    }

    func message(_ id: String, method: String = "POST", headers: [String: String] = [:], body: String = "") -> [String: Any] {
        ["id": id, "method": method, "url": "calamaio-app://ui/api/avatars", "headers": headers, "body": body]
    }

    final class Replies {
        var all: [(Any?, String?)] = []
        var status: [Int?] { all.map { ($0.0 as? [String: Any])?["status"] as? Int } }
        func sink() -> (Any?, String?) -> Void { { self.all.append(($0, $1)) } }
    }

    // Chiamate dirette al gestore (come un postMessage scritto a mano dalla pagina, senza fetch).
    func testMethodAllowlistAndHeaderInjection() {
        let t = Multi()
        let bridge = FetchBridge(transport: t, upstream: { Upstream(endpoint: ep, token: token) })
        let r = Replies()
        for (i, m) in ["GET", "HEAD", "post", "CONNECT", "TRACE", "OPTIONS", "PO\r\nST", "POST ", ""].enumerated() {
            bridge.handle(message("m\(i)", method: m), reply: r.sink())
        }
        bridge.handle(message("h1", headers: ["Content-Type": "text/plain\r\nAuthorization: Bearer x"]), reply: r.sink())
        bridge.handle(message("h2", headers: ["X-A\nB": "1"]), reply: r.sink()) // anche fuori dall'elenco
        bridge.handle(message("h3", headers: ["Accept": "a\u{0}b"]), reply: r.sink())
        bridge.handle(["method": "POST", "url": "calamaio-app://ui/a", "body": ""], reply: r.sink()) // senza id
        bridge.handle(message(String(repeating: "x", count: 65)), reply: r.sink())
        XCTAssertTrue(t.calls.isEmpty, "\(t.calls.map(\.rq))")
        XCTAssertEqual(r.all.count, 14)
        XCTAssertTrue(r.all.allSatisfy { $0.0 == nil && $0.1 == "richiesta non valida" })
        for (i, m) in ["POST", "PUT", "DELETE", "PATCH"].enumerated() {
            bridge.handle(message("ok\(i)", method: m), reply: r.sink())
            XCTAssertEqual(t.calls.last?.rq.httpMethod, m)
            t.ok(i)
        }
        XCTAssertEqual(r.status.suffix(4), [200, 200, 200, 200])
    }

    func testBase64LengthCheckedBeforeDecoding() {
        let t = Multi()
        let bridge = FetchBridge(transport: t, upstream: { Upstream(endpoint: ep, token: token) })
        let r = Replies()
        // non è base64 valido: se fosse decodificato prima darebbe «richiesta non valida», non 413
        bridge.handle(message("big", body: String(repeating: "!", count: FetchBridge.maxBase64 + 1)), reply: r.sink())
        XCTAssertEqual(r.status, [413])
        XCTAssertTrue(t.calls.isEmpty)
        XCTAssertGreaterThanOrEqual(FetchBridge.maxBase64, (Proxy.maxBody + 2) / 3 * 4)
    }

    func testConcurrencyCap() {
        let t = Multi()
        let bridge = FetchBridge(transport: t, upstream: { Upstream(endpoint: ep, token: token) })
        let r = Replies()
        for i in 0..<FetchBridge.maxInFlight { bridge.handle(message("c\(i)"), reply: r.sink()) }
        bridge.handle(message("over"), reply: r.sink())
        XCTAssertEqual(t.calls.count, FetchBridge.maxInFlight)
        XCTAssertEqual(r.status, [503])
        let b = Data(base64Encoded: (r.all[0].0 as? [String: Any])?["body"] as? String ?? "") ?? Data()
        XCTAssertTrue(String(decoding: b, as: UTF8.self).contains(Texts.busy))
        bridge.handle(message("c0"), reply: r.sink()) // id già in corso
        XCTAssertEqual(r.all.last?.1, "richiesta non valida")
        t.ok(0)
        bridge.handle(message("again"), reply: r.sink()) // finita una, ne parte un'altra
        XCTAssertEqual(t.calls.count, FetchBridge.maxInFlight + 1)
    }

    // Il tetto delle richieste in corso si controlla prima di leggere e decodificare il corpo.
    func testConcurrencyCapBeforeDecoding() {
        let t = Multi()
        let bridge = FetchBridge(transport: t, upstream: { Upstream(endpoint: ep, token: token) })
        let r = Replies()
        for i in 0..<FetchBridge.maxInFlight { bridge.handle(message("c\(i)"), reply: r.sink()) }
        bridge.handle(message("bad", body: "%%%"), reply: r.sink()) // decodificato prima darebbe «non valida»
        XCTAssertEqual(r.status, [503])
        XCTAssertEqual(t.calls.count, FetchBridge.maxInFlight)
    }

    func testCancelByID() {
        let t = Multi()
        let bridge = FetchBridge(transport: t, upstream: { Upstream(endpoint: ep, token: token) })
        let r = Replies()
        bridge.handle(message("u1", body: Data(count: 1 << 20).base64EncodedString()), reply: r.sink())
        bridge.handle(message("u2"), reply: r.sink())
        var acks = 0
        bridge.handle(["cancel": "u1"]) { v, e in XCTAssertNil(v); XCTAssertNil(e); acks += 1 }
        XCTAssertEqual(t.cancelled, [0]) // l'invio al PC si ferma davvero
        XCTAssertEqual(r.all.count, 1)
        XCTAssertNil(r.all[0].0)
        XCTAssertEqual(r.all[0].1, "AbortError")
        t.ok(0) // un trasporto che arriva lo stesso in fondo non risponde una seconda volta
        XCTAssertEqual(r.all.count, 1)
        bridge.handle(["cancel": "u1"]) { _, _ in acks += 1 } // già tolto: niente
        bridge.handle(["cancel": "nessuno"]) { _, _ in acks += 1 }
        XCTAssertEqual(t.cancelled, [0])
        t.ok(1)
        bridge.handle(["cancel": "u2"]) { _, _ in acks += 1 } // finita: la voce non c'è più
        XCTAssertEqual(t.cancelled, [0])
        XCTAssertEqual(r.status, [nil, 200])
        XCTAssertEqual(acks, 4)
        XCTAssertEqual(bridge.inFlight, 0)
    }

    func testResponseCap() {
        let t = Multi()
        let bridge = FetchBridge(transport: t, upstream: { Upstream(endpoint: ep, token: token) })
        let r = Replies()
        bridge.handle(message("big"), reply: r.sink())
        t.calls[0].onResponse(HTTPURLResponse(url: t.calls[0].rq.url!, statusCode: 200, httpVersion: "HTTP/1.1", headerFields: [:])!)
        t.calls[0].onData(Data(count: FetchBridge.maxResponse))
        XCTAssertTrue(r.all.isEmpty)
        t.calls[0].onData(Data(count: 1))
        XCTAssertEqual(t.cancelled, [0])
        XCTAssertEqual(r.all.count, 1)
        XCTAssertEqual(r.all[0].1, Texts.responseTooLarge)
        t.calls[0].onDone(nil)
        XCTAssertEqual(r.all.count, 1)
        XCTAssertEqual(bridge.inFlight, 0)
    }

    func testShimSendsIDsAndCancel() {
        let s = FetchBridge.shimSource
        XCTAssertTrue(s.contains("cancel:"))
        XCTAssertTrue(s.contains("id"))
    }
}
