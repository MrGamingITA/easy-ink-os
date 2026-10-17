import XCTest
@testable import CalamaioKit

final class PCClientTests: XCTestCase {
    private let token = String(repeating: "ab", count: 32)

    func testRefusesRedirectOnWhoami() async throws {
        let s = try LocalPinnedServer()
        for code in [302, 307] {
            do { _ = try await PCClient(pin: s.pin).whoami(host: "127.0.0.1", port: s.tlsPort, token: token, path: "/redir/\(code)"); XCTFail("redirect seguito") }
            catch PCError.badResponse(let st) { XCTAssertEqual(st, code) }
        }
        XCTAssertFalse(s.events().contains { $0.hasPrefix("plain") }, "il redirect in chiaro è stato seguito")
    }

    func testRefusesRedirectOnPair() async throws {
        let s = try LocalPinnedServer()
        for code in [302, 307] {
            do { _ = try await PCClient(pin: s.pin).pair(host: "127.0.0.1", port: s.tlsPort, code: String(repeating: "0", count: 32), name: "x", oldToken: nil, oldTokenFingerprint: nil, path: "/redir/\(code)"); XCTFail("redirect seguito") }
            catch PCError.pairingRejected(let m) { XCTAssertEqual(m, "HTTP \(code)") }
        }
        XCTAssertFalse(s.events().contains { $0.hasPrefix("plain") }, "il redirect in chiaro è stato seguito")
    }

    func testOldTokenOnlyWithSameFingerprint() async throws {
        let s = try LocalPinnedServer()
        let code = String(repeating: "0", count: 32)
        let old = String(repeating: "cd", count: 32)
        let c = PCClient(pin: s.pin)
        _ = try await c.pair(host: "127.0.0.1", port: s.tlsPort, code: code, name: "x", oldToken: old, oldTokenFingerprint: s.pin.uppercased())
        _ = try await c.pair(host: "127.0.0.1", port: s.tlsPort, code: code, name: "x", oldToken: old, oldTokenFingerprint: String(repeating: "1", count: 64))
        _ = try await c.pair(host: "127.0.0.1", port: s.tlsPort, code: code, name: "x", oldToken: old, oldTokenFingerprint: nil)
        let auth = s.events().filter { $0.hasPrefix("tls POST") }
        XCTAssertEqual(auth, ["tls POST /app/pair auth=Bearer \(old)", "tls POST /app/pair auth=-", "tls POST /app/pair auth=-"])
    }

    func testMalformedTokenNotSent() async throws {
        let s = try LocalPinnedServer()
        for bad in ["", "abc", token.uppercased(), token + "\r\nX: y"] {
            do { _ = try await PCClient(pin: s.pin).whoami(host: "127.0.0.1", port: s.tlsPort, token: bad); XCTFail("token malformato inviato") }
            catch PCError.unauthorized {}
        }
        XCTAssertTrue(s.events().isEmpty)
    }

    func testConcurrentMismatchStaysPerTask() async throws {
        let s = try LocalPinnedServer()
        let c = PCClient(pin: String(repeating: "0", count: 64))
        let errs = await withTaskGroup(of: PCError?.self) { g -> [PCError?] in
            for _ in 0..<8 { g.addTask { do { _ = try await c.whoami(host: "127.0.0.1", port: s.tlsPort, token: self.token); return nil } catch { return error as? PCError } } }
            var out: [PCError?] = []
            for await e in g { out.append(e) }
            return out
        }
        XCTAssertEqual(errs.count, 8)
        XCTAssertTrue(errs.allSatisfy { $0 == .pinMismatch }, "\(errs)")
    }

    func testCancellationIsItsOwnError() async throws {
        let s = try LocalPinnedServer()
        let c = PCClient(pin: s.pin)
        let t = Task { try await c.whoami(host: "127.0.0.1", port: s.tlsPort, token: self.token, path: "/redir/200") }
        t.cancel()
        do { _ = try await t.value; XCTFail("non annullato") }
        catch PCError.cancelled {}
        catch { XCTFail("errore inatteso \(error)") }
    }

    func testSessionConfigurationHardening() {
        let cfg = PCClient.sessionConfiguration(timeout: 4)
        XCTAssertNil(cfg.urlCredentialStorage)
        XCTAssertNil(cfg.httpCookieStorage)
        XCTAssertFalse(cfg.httpShouldSetCookies)
        XCTAssertEqual(cfg.tlsMinimumSupportedProtocolVersion, .TLSv13)
    }
}
