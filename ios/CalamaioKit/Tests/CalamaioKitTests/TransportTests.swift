import XCTest
@testable import CalamaioKit

// URLSessionTransport contro il server TLS locale: impronta, redirect mai seguiti (anche con corpo), annullamento.
@MainActor
final class TransportTests: XCTestCase {
    final class Rec {
        var status: Int?
        var data = Data()
        var done = false
        var error: Error?
        var calls = 0
    }

    func run(_ t: URLSessionTransport, _ rq: URLRequest, timeout: TimeInterval = 5) async throws -> Rec {
        let r = Rec()
        _ = t.start(rq, onResponse: { XCTAssertTrue(Thread.isMainThread); r.status = $0.statusCode; r.calls += 1 },
                    onData: { r.data.append($0); r.calls += 1 },
                    onDone: { r.error = $0; r.done = true; r.calls += 1 })
        let end = Date().addingTimeInterval(timeout)
        while !r.done, Date() < end { try await Task.sleep(nanoseconds: 20_000_000) }
        return r
    }

    func rq(_ s: LocalPinnedServer, _ path: String, method: String = "GET", body: Data? = nil) -> URLRequest {
        var r = URLRequest(url: URL(string: "https://127.0.0.1:\(s.tlsPort)\(path)")!)
        r.httpMethod = method
        r.httpBody = body
        r.setValue("Bearer " + token, forHTTPHeaderField: "Authorization")
        return r
    }

    func testFetchWithPin() async throws {
        let s = try LocalPinnedServer()
        let r = try await run(URLSessionTransport(pin: s.pin), rq(s, "/x"))
        XCTAssertTrue(r.done)
        XCTAssertNil(r.error)
        XCTAssertEqual(r.status, 200)
        XCTAssertTrue(String(decoding: r.data, as: UTF8.self).contains("token"))
        XCTAssertEqual(s.events(), ["tls GET /x auth=Bearer \(token)"])
    }

    func testRedirectsNeverFollowed() async throws {
        let s = try LocalPinnedServer()
        let t = URLSessionTransport(pin: s.pin)
        for (code, method, body) in [(302, "GET", nil), (307, "POST", Data(count: 1 << 20)), (308, "POST", Data("{}".utf8))] as [(Int, String, Data?)] {
            let r = try await run(t, rq(s, "/redir/\(code)", method: method, body: body))
            XCTAssertNil(r.error, "\(code)")
            XCTAssertEqual(r.status, code) // la 3xx arriva a chi chiama
        }
        XCTAssertFalse(s.events().contains { $0.hasPrefix("plain") }, "redirect seguito: \(s.events())")
    }

    func testWrongPinFails() async throws {
        let s = try LocalPinnedServer()
        let r = try await run(URLSessionTransport(pin: String(repeating: "0", count: 64)), rq(s, "/x"))
        XCTAssertTrue(r.done)
        XCTAssertNotNil(r.error)
        XCTAssertNil(r.status)
        XCTAssertTrue(s.events().isEmpty)
    }

    func testCancelSilences() async throws {
        let s = try LocalPinnedServer()
        let r = Rec()
        let cancel = URLSessionTransport(pin: s.pin).start(rq(s, "/x"), onResponse: { _ in r.calls += 1 },
                                                           onData: { _ in r.calls += 1 }, onDone: { _ in r.calls += 1 })
        cancel()
        cancel() // due volte non fa danni
        try await Task.sleep(nanoseconds: 500_000_000)
        XCTAssertEqual(r.calls, 0)
    }

    // Il tempo massimo della richiesta intera non deve chiudere l'SSE (resta aperto per ore).
    func testLongLivedStreamsAllowed() {
        let cfg = URLSessionTransport(pin: "").session.configuration
        XCTAssertGreaterThanOrEqual(cfg.timeoutIntervalForResource, 24 * 3600)
        XCTAssertEqual(cfg.timeoutIntervalForRequest, URLSessionTransport.idleTimeout)
        XCTAssertNil(cfg.urlCache)
        XCTAssertNil(cfg.httpCookieStorage)
        XCTAssertEqual(cfg.tlsMinimumSupportedProtocolVersion, .TLSv13)
    }
}
