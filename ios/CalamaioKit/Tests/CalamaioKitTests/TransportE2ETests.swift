import XCTest
@testable import CalamaioKit

// Col servizio vero (ios/scripts/e2e.sh …): SSE attraverso URLSessionTransport, elenco chiuso.
@MainActor
final class TransportE2ETests: XCTestCase {
    func testSSEAndForbiddenThroughTransport() async throws {
        let (l, token) = try await E2E.session()
        let up = Upstream(endpoint: Endpoint(host: l.hosts[0], port: l.port), token: token)
        let transport = URLSessionTransport(pin: l.fingerprint)
        let core = ProxyCore(transport: transport, upstream: { up })

        let events = FakeTask(URLRequest(url: URL(string: "calamaio-app://ui/api/events")!))
        core.start(events)
        let deadline = Date().addingTimeInterval(5)
        while !String(decoding: events.data, as: UTF8.self).contains("event: status"), Date() < deadline {
            try await Task.sleep(nanoseconds: 50_000_000)
        }
        XCTAssertEqual(events.response?.statusCode, 200)
        XCTAssertTrue(String(decoding: events.data, as: UTF8.self).contains("event: status"), "primo evento SSE non arrivato")
        XCTAssertFalse(events.calls.contains("finish"), "lo stream non deve finire da solo")
        core.stop(events)

        for (path, want) in [("/api/backups", 404), ("/api/status", 200), ("/", 200), ("/firmware.html", 404)] {
            let t = FakeTask(URLRequest(url: URL(string: "calamaio-app://ui" + path)!))
            core.start(t)
            let end = Date().addingTimeInterval(5)
            while !t.calls.contains("finish") && !t.calls.contains("fail"), Date() < end {
                try await Task.sleep(nanoseconds: 50_000_000)
            }
            XCTAssertEqual(t.response?.statusCode, want, path)
            XCTAssertFalse(String(decoding: t.data, as: UTF8.self).contains(token), "token nella risposta di \(path)")
        }
    }
}
