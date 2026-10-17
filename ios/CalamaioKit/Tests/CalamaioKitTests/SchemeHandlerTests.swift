import XCTest
import CryptoKit
@testable import CalamaioKit

final class FakeTask: SchemeTask {
    let request: URLRequest
    var response: HTTPURLResponse?
    var data = Data()
    var calls: [String] = []
    var error: Error?
    init(_ r: URLRequest) { request = r }
    func didReceive(_ response: URLResponse) { calls.append("response"); self.response = response as? HTTPURLResponse }
    func didReceive(_ data: Data) { calls.append("data"); self.data.append(data) }
    func didFinish() { calls.append("finish") }
    func didFailWithError(_ error: Error) { calls.append("fail"); self.error = error }
}

final class FakeTransport: Transport {
    var requests: [URLRequest] = []
    var cancelled = 0
    var onResponse: ((HTTPURLResponse) -> Void)?
    var onData: ((Data) -> Void)?
    var onDone: ((Error?) -> Void)?
    func start(_ rq: URLRequest, onResponse: @escaping (HTTPURLResponse) -> Void, onData: @escaping (Data) -> Void,
               onDone: @escaping (Error?) -> Void) -> () -> Void {
        requests.append(rq)
        self.onResponse = onResponse; self.onData = onData; self.onDone = onDone
        return { [weak self] in self?.cancelled += 1 }
    }
    func respond(_ status: Int, _ ctype: String) {
        onResponse?(HTTPURLResponse(url: requests.last!.url!, statusCode: status, httpVersion: "HTTP/1.1", headerFields: ["Content-Type": ctype])!)
    }
}

@MainActor
final class SchemeHandlerTests: XCTestCase {
    var transport = FakeTransport()
    var up: Upstream? = Upstream(endpoint: ep, token: token)
    lazy var core = ProxyCore(transport: transport, upstream: { [unowned self] in self.up })

    func task(_ path: String, method: String = "GET") -> FakeTask {
        var r = URLRequest(url: URL(string: "calamaio-app://ui" + path)!)
        r.httpMethod = method
        return FakeTask(r)
    }

    // I1: un 401 dal PC (telefono revocato) avvisa l'app, che ricontrolla; la pagina riceve comunque la risposta.
    func testUnauthorizedCallsBack() {
        var n = 0
        core.onUnauthorized = { n += 1 }
        let ok = task("/api/status")
        core.start(ok)
        transport.respond(200, "application/json")
        XCTAssertEqual(n, 0)
        let t = task("/api/status")
        core.start(t)
        transport.respond(401, "application/json")
        transport.onData?(Data(#"{"error":"non autorizzato"}"#.utf8))
        transport.onDone?(nil)
        XCTAssertEqual(n, 1)
        XCTAssertEqual(t.response?.statusCode, 401)
        XCTAssertEqual(t.calls, ["response", "data", "finish"])
        // anche un documento (Accept text/html)
        var doc = URLRequest(url: URL(string: "calamaio-app://ui/")!)
        doc.setValue("text/html", forHTTPHeaderField: "Accept")
        core.start(FakeTask(doc))
        transport.respond(401, "application/json")
        XCTAssertEqual(n, 2)
    }

    func testStreamsChunksBeforeFinish() {
        let t = task("/api/events")
        core.start(t)
        XCTAssertEqual(transport.requests.first?.value(forHTTPHeaderField: "Authorization"), "Bearer " + token)
        transport.respond(200, "text/event-stream")
        transport.onData?(Data("event: status\n".utf8))
        XCTAssertEqual(t.calls, ["response", "data"]) // il primo pezzo è già nella pagina
        XCTAssertEqual(t.response?.url?.absoluteString, "calamaio-app://ui/api/events")
        transport.onData?(Data("data: {}\n\n".utf8))
        transport.onDone?(nil)
        XCTAssertEqual(t.calls, ["response", "data", "data", "finish"])
        XCTAssertEqual(String(data: t.data, encoding: .utf8), "event: status\ndata: {}\n\n")
    }

    // M4: WebKit non segue un 3xx dal gestore dello schema. Per i documenti (Accept text/html) l'app risponde con una
    // pagina che porta al Location riportato sullo schema; fuori dallo schema, 502 senza indirizzi. I fetch restano 3xx.
    func testDocumentRedirectBecomesPage() throws {
        var rq = URLRequest(url: URL(string: "calamaio-app://ui/sub/index.html")!)
        rq.setValue("text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8", forHTTPHeaderField: "Accept")
        let t = FakeTask(rq)
        core.start(t)
        transport.onResponse?(HTTPURLResponse(url: transport.requests[0].url!, statusCode: 301, httpVersion: "HTTP/1.1",
                                              headerFields: ["Location": "./", "Content-Type": "text/html"])!)
        XCTAssertEqual(transport.cancelled, 1)
        transport.onData?(Data("<a>Moved</a>".utf8))
        transport.onDone?(nil)
        XCTAssertEqual(t.calls, ["response", "data", "finish"])
        XCTAssertEqual(t.response?.statusCode, 200)
        XCTAssertEqual(t.response?.value(forHTTPHeaderField: "Content-Type"), "text/html; charset=utf-8")
        let html = String(decoding: t.data, as: UTF8.self)
        XCTAssertTrue(html.contains(#"<meta http-equiv="refresh" content="0;url=calamaio-app://ui/sub/">"#), html)

        for loc in ["http://192.168.1.20:8765/x", "javascript:alert(1)", "calamaio-app://evil/", "//evil.example/"] {
            let t2 = FakeTask(rq)
            core.start(t2)
            transport.onResponse?(HTTPURLResponse(url: transport.requests.last!.url!, statusCode: 302, httpVersion: "HTTP/1.1",
                                                  headerFields: ["Location": loc])!)
            XCTAssertEqual(t2.response?.statusCode, 502, loc)
            XCTAssertFalse(String(decoding: t2.data, as: UTF8.self).contains("192.168"), loc)
            XCTAssertFalse(String(decoding: t2.data, as: UTF8.self).contains("refresh"), loc)
        }
        // https del PC: riportato sullo schema
        let t3 = FakeTask(rq)
        core.start(t3)
        transport.onResponse?(HTTPURLResponse(url: transport.requests.last!.url!, statusCode: 308, httpVersion: "HTTP/1.1",
                                              headerFields: ["Location": "https://192.168.1.20:8767/a?b=1&c=\"2\""])!)
        let h3 = String(decoding: t3.data, as: UTF8.self)
        XCTAssertTrue(h3.contains("url=calamaio-app://ui/a?b=1&amp;c=%222%22") || h3.contains("url=calamaio-app://ui/a?b=1&amp;c=&quot;2&quot;"), h3)
        XCTAssertFalse(h3.contains("192.168"))

        // un fetch (Accept */*) riceve il 3xx così com'è, col Location sullo schema
        let f = task("/api/r")
        core.start(f)
        transport.onResponse?(HTTPURLResponse(url: transport.requests.last!.url!, statusCode: 302, httpVersion: "HTTP/1.1",
                                              headerFields: ["Location": "/api/target"])!)
        XCTAssertEqual(f.response?.statusCode, 302)
        XCTAssertEqual(f.response?.value(forHTTPHeaderField: "Location"), "/api/target")
    }

    func testStopCancelsAndSilences() {
        let t = task("/api/events")
        core.start(t)
        transport.respond(200, "text/event-stream")
        core.stop(t)
        XCTAssertEqual(transport.cancelled, 1)
        transport.onData?(Data("tardi".utf8))
        transport.onDone?(URLError(.cancelled))
        XCTAssertEqual(t.calls, ["response"]) // dopo lo stop niente più chiamate al compito (WebKit si fermerebbe)
        core.stop(t) // un secondo stop non fa niente
        XCTAssertEqual(transport.cancelled, 1)
    }

    func testFailureReachesPageOnce() {
        let t = task("/api/status")
        core.start(t)
        transport.onDone?(URLError(.networkConnectionLost))
        transport.onDone?(URLError(.networkConnectionLost))
        XCTAssertEqual(t.calls, ["fail"])
    }

    func testNotReachableGives503() {
        up = nil
        let t = task("/api/status")
        core.start(t)
        XCTAssertTrue(transport.requests.isEmpty)
        XCTAssertEqual(t.response?.statusCode, 503)
        XCTAssertEqual(t.calls.last, "finish")
        XCTAssertTrue(String(data: t.data, encoding: .utf8)!.contains(Texts.pcUnreachable))
    }

    func testLargeStreamBodyForwarded() {
        let big = Data((0..<(2 << 20)).map { UInt8(($0 * 7) & 255) }) // una ROM da 2 MiB
        let t = task("/api/gb/games?name=x.gb", method: "POST")
        var r = t.request
        r.httpBodyStream = InputStream(data: big)
        let t2 = FakeTask(r)
        core.start(t2)
        XCTAssertEqual(transport.requests.count, 1)
        XCTAssertEqual(transport.requests[0].httpBody.map { SHA256.hash(data: $0).description }, SHA256.hash(data: big).description)
    }

    func testBodyOverLimitIs413() {
        var r = task("/api/avatars", method: "POST").request
        r.httpBodyStream = InputStream(data: Data(count: Proxy.maxBody + 1))
        let t = FakeTask(r)
        core.start(t)
        XCTAssertTrue(transport.requests.isEmpty) // niente parte verso il PC
        XCTAssertEqual(t.response?.statusCode, 413)
        XCTAssertTrue(String(data: t.data, encoding: .utf8)!.contains("16 MiB"))
    }

    func testForeignHostFails() {
        let t = FakeTask(URLRequest(url: URL(string: "calamaio-app://evil/api/status")!))
        core.start(t)
        XCTAssertTrue(transport.requests.isEmpty)
        XCTAssertEqual(t.calls, ["fail"])
    }

    // L'errore del PC arriva alla pagina senza l'indirizzo https del PC (né altro che venga dalla rete).
    func testUpstreamErrorIsSanitized() {
        let t = task("/api/status")
        core.start(t)
        let failing = URL(string: "https://192.168.1.20:8767/api/status")!
        transport.onDone?(URLError(.secureConnectionFailed, userInfo: [NSURLErrorFailingURLErrorKey: failing,
                                                                     NSLocalizedDescriptionKey: "https://192.168.1.20:8767"]))
        let e = t.error as NSError?
        XCTAssertEqual(e?.domain, NSURLErrorDomain)
        XCTAssertEqual(e?.code, URLError.secureConnectionFailed.rawValue)
        XCTAssertFalse("\(e?.userInfo ?? [:])".contains("https://"), "\(e?.userInfo ?? [:])")
    }

    func testEncodedPathForwardedExactly() {
        core.start(task("/api/avatars/a%2Fb%7e?session=x%2By"))
        XCTAssertEqual(transport.requests.first?.url?.absoluteString, "https://192.168.1.20:8767/api/avatars/a%2Fb%7e?session=x%2By")
    }

    // Un trasporto che finisce dentro start (errore immediato) chiude il compito una volta sola.
    func testSynchronousCompletion() {
        final class Instant: Transport {
            func start(_ rq: URLRequest, onResponse: @escaping (HTTPURLResponse) -> Void, onData: @escaping (Data) -> Void,
                       onDone: @escaping (Error?) -> Void) -> () -> Void {
                onDone(URLError(.cannotConnectToHost))
                return {}
            }
        }
        let c = ProxyCore(transport: Instant(), upstream: { Upstream(endpoint: ep, token: token) })
        let t = task("/api/status")
        var ended = 0
        c.start(t, onEnd: { ended += 1 })
        c.stop(t)
        XCTAssertEqual(t.calls, ["fail"])
        XCTAssertEqual(ended, 1)
    }
}
