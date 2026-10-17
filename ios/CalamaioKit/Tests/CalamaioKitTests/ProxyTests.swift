import XCTest
@testable import CalamaioKit

let ep = Endpoint(host: "192.168.1.20", port: 8767)
let token = String(repeating: "f", count: 64)

final class ProxyTests: XCTestCase {
    func testUpstreamDropsPageHeaders() throws {
        var page = URLRequest(url: URL(string: "calamaio-app://ui/api/displays/calamaio-3f2a/settings?x=1&y=%20z")!)
        page.httpMethod = "POST"
        for (k, v) in ["Content-Type": "application/json", "Accept": "*/*", "Authorization": "Bearer pagina",
                       "Origin": "calamaio-app://ui", "Cookie": "a=b", "Referer": "calamaio-app://ui/", "Sec-Fetch-Site": "same-origin",
                       "Last-Event-ID": "7", "X-Altro": "1"] {
            page.setValue(v, forHTTPHeaderField: k)
        }
        let up = try Proxy.upstream(page, body: Data("{}".utf8), to: ep, token: token)
        XCTAssertEqual(up.url?.absoluteString, "https://192.168.1.20:8767/api/displays/calamaio-3f2a/settings?x=1&y=%20z")
        XCTAssertEqual(up.httpMethod, "POST")
        XCTAssertEqual(up.httpBody, Data("{}".utf8))
        XCTAssertEqual(up.value(forHTTPHeaderField: "Authorization"), "Bearer " + token) // mai quello della pagina
        XCTAssertEqual(up.value(forHTTPHeaderField: "Content-Type"), "application/json")
        XCTAssertEqual(up.value(forHTTPHeaderField: "Last-Event-ID"), "7")
        for h in ["Origin", "Cookie", "Referer", "Sec-Fetch-Site", "X-Altro"] {
            XCTAssertNil(up.value(forHTTPHeaderField: h), h)
        }
    }

    func testRootPathAndForeignURLs() throws {
        let root = try Proxy.upstream(URLRequest(url: URL(string: "calamaio-app://ui")!), body: nil, to: ep, token: token)
        XCTAssertEqual(root.url?.absoluteString, "https://192.168.1.20:8767/")
        for bad in ["calamaio-app://evil/api/status", "https://192.168.1.20:8767/api/status", "calamaio-app:///api/status"] {
            XCTAssertThrowsError(try Proxy.upstream(URLRequest(url: URL(string: bad)!), body: nil, to: ep, token: token), bad)
        }
    }

    func testBodyLimit() {
        let page = URLRequest(url: URL(string: "calamaio-app://ui/api/avatars")!)
        XCTAssertNoThrow(try Proxy.upstream(page, body: Data(count: Proxy.maxBody), to: ep, token: token))
        XCTAssertThrowsError(try Proxy.upstream(page, body: Data(count: Proxy.maxBody + 1), to: ep, token: token)) {
            XCTAssertEqual($0 as? Proxy.ProxyError, .bodyTooLarge)
        }
    }

    func testDownstreamHeadersAndLocation() throws {
        let r = HTTPURLResponse(url: URL(string: "https://192.168.1.20:8767/x")!, statusCode: 301, httpVersion: "HTTP/1.1", headerFields: [
            "Content-Type": "text/html", "Content-Length": "10", "Content-Encoding": "gzip", "Set-Cookie": "a=b",
            "Strict-Transport-Security": "max-age=1", "Content-Security-Policy": "default-src 'self'",
            "Location": "https://192.168.1.20:8767/device.html?a=1"])!
        let page = URL(string: "calamaio-app://ui/x")!
        let d = Proxy.downstream(r, for: page)
        XCTAssertEqual(d.url, page)
        XCTAssertEqual(d.statusCode, 301)
        XCTAssertEqual(d.value(forHTTPHeaderField: "Content-Type"), "text/html")
        XCTAssertEqual(d.value(forHTTPHeaderField: "Content-Security-Policy"), "default-src 'self'")
        XCTAssertEqual(d.value(forHTTPHeaderField: "Location"), "calamaio-app://ui/device.html?a=1")
        for h in ["Content-Length", "Content-Encoding", "Set-Cookie", "Strict-Transport-Security"] {
            XCTAssertNil(d.value(forHTTPHeaderField: h), h)
        }
    }

    func testReadBodyFromStream() {
        var rq = URLRequest(url: URL(string: "calamaio-app://ui/x")!)
        let big = Data((0..<(3 << 20)).map { UInt8($0 & 255) })
        rq.httpBodyStream = InputStream(data: big)
        XCTAssertEqual(Proxy.readBody(rq), big)
        rq.httpBodyStream = InputStream(data: Data(count: Proxy.maxBody + 10))
        XCTAssertEqual(Proxy.readBody(rq)?.count, Proxy.maxBody + 1) // si ferma appena oltre il limite
    }

    // Il servizio risponde 404 a ogni codifica non canonica: percorso e query passano esattamente come li ha chiesti la pagina.
    func testEncodedPathAndQueryUntouched() throws {
        for raw in ["/api/avatars/a%2Fb%20c/anchors?session=x%2By&n=%E2%82%AC+1",
                    "/api/themes/t%7e1/export?a=%41&b=",
                    "/api/displays/calamaio-3f2a/log?after=0&wait=25",
                    "/caf%C3%A8.html?"] {
            let up = try Proxy.upstream(URLRequest(url: URL(string: "calamaio-app://ui" + raw)!), body: nil, to: ep, token: token)
            XCTAssertEqual(up.url?.absoluteString, "https://192.168.1.20:8767" + raw, raw)
        }
    }

    func testRejectsUserinfoAndPort() {
        for bad in ["calamaio-app://u:p@ui/api/status", "calamaio-app://ui:8767/api/status"] {
            XCTAssertThrowsError(try Proxy.upstream(URLRequest(url: URL(string: bad)!), body: nil, to: ep, token: token), bad)
        }
    }

    func testRelativeLocationKept() {
        let r = HTTPURLResponse(url: URL(string: "https://192.168.1.20:8767/api/backups")!, statusCode: 201, httpVersion: "HTTP/1.1",
                                headerFields: ["Location": "/api/backups/b%201"])!
        XCTAssertEqual(Proxy.downstream(r, for: URL(string: "calamaio-app://ui/api/backups")!).value(forHTTPHeaderField: "Location"),
                       "/api/backups/b%201")
    }

    func testLocalReplyHasNoToken() throws {
        let (r, d) = Proxy.local(status: 503, url: URL(string: "calamaio-app://ui/x")!, message: Texts.pcUnreachable)
        XCTAssertEqual(r.statusCode, 503)
        XCTAssertEqual(try JSONSerialization.jsonObject(with: d) as? [String: String], ["error": Texts.pcUnreachable])
    }

    // Il metodo deve essere un token HTTP (RFC 7230): niente spazi, CR/LF o caratteri di controllo.
    func testMethodMustBeToken() {
        for bad in ["", "GE T", "GET\r\nX-A: b", "PO\u{0}ST", "PÖST", "(GET)"] {
            var r = URLRequest(url: URL(string: "calamaio-app://ui/api/status")!)
            r.httpMethod = bad
            if r.httpMethod != bad { continue } // URLRequest normalizza "" a GET
            XCTAssertThrowsError(try Proxy.upstream(r, body: nil, to: ep, token: token), bad) {
                XCTAssertEqual($0 as? Proxy.ProxyError, .badRequest)
            }
        }
        for good in ["GET", "POST", "PATCH", "M-SEARCH"] {
            var r = URLRequest(url: URL(string: "calamaio-app://ui/api/status")!)
            r.httpMethod = good
            XCTAssertNoThrow(try Proxy.upstream(r, body: nil, to: ep, token: token), good)
        }
    }

    func testHeaderValueWithLineBreakRejected() {
        XCTAssertFalse(Proxy.isSafeHeader("Content-Type", "text/plain\r\nX-A: b"))
        XCTAssertFalse(Proxy.isSafeHeader("Content-Type", "a\nb"))
        XCTAssertFalse(Proxy.isSafeHeader("Content-Type", "a\u{0}b"))
        XCTAssertFalse(Proxy.isSafeHeader("Content\r\nType", "a"))
        XCTAssertFalse(Proxy.isSafeHeader("", "a"))
        XCTAssertTrue(Proxy.isSafeHeader("Content-Type", "multipart/form-data; boundary=----WebKitFormBoundaryAb12"))
    }

    // M3: le risposte fatte dall'app portano la CSP della webapp, la stessa stringa di service/internal/api/web.go.
    func testLocalRepliesCarryServiceCSP() throws {
        var url = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { url.deleteLastPathComponent() }
        let go = try String(contentsOf: url.appendingPathComponent("service/internal/api/web.go"), encoding: .utf8)
        let start = try XCTUnwrap(go.range(of: "const csp = "))
        // la costante Go è su tre righe di stringhe concatenate
        let decl = go[start.upperBound...].split(separator: "\n", omittingEmptySubsequences: false).prefix(3).joined(separator: "\n")
        let parts = try NSRegularExpression(pattern: #""([^"]*)""#)
            .matches(in: decl, range: NSRange(decl.startIndex..., in: decl))
            .map { String(decl[Range($0.range(at: 1), in: decl)!]) }
        let goCSP = parts.joined()
        XCTAssertTrue(goCSP.hasPrefix("default-src 'self';"), goCSP)
        XCTAssertTrue(goCSP.hasSuffix("frame-ancestors 'none'"), goCSP)
        XCTAssertEqual(Proxy.csp, goCSP)

        let page = URL(string: "calamaio-app://ui/x")!
        let (r, _) = Proxy.local(status: 503, url: page, message: Texts.pcUnreachable)
        XCTAssertEqual(r.value(forHTTPHeaderField: "Content-Security-Policy"), goCSP)
        let moved = HTTPURLResponse(url: URL(string: "https://192.168.1.20:8767/x")!, statusCode: 301, httpVersion: "HTTP/1.1",
                                    headerFields: ["Location": "/y"])!
        let (rr, _) = try XCTUnwrap(Proxy.redirectPage(moved, for: page))
        XCTAssertEqual(rr.statusCode, 200)
        XCTAssertEqual(rr.value(forHTTPHeaderField: "Content-Security-Policy"), goCSP)
        let away = HTTPURLResponse(url: URL(string: "https://192.168.1.20:8767/x")!, statusCode: 302, httpVersion: "HTTP/1.1",
                                   headerFields: ["Location": "http://evil.example/"])!
        let (r502, _) = try XCTUnwrap(Proxy.redirectPage(away, for: page))
        XCTAssertEqual(r502.value(forHTTPHeaderField: "Content-Security-Policy"), goCSP)
    }
}
