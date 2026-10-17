import XCTest
@testable import CalamaioKit

// Col servizio vero: ios/scripts/e2e.sh swift test --package-path ios/CalamaioKit --filter E2E
// (senza lo script questi test si saltano).
enum E2E {
    nonisolated(unsafe) static var cached: (link: PairingLink, token: String)?

    // Un abbinamento per esecuzione (il codice è monouso): i test lo condividono.
    static func session() async throws -> (link: PairingLink, token: String) {
        if let c = cached { return c }
        guard let raw = ProcessInfo.processInfo.environment["CALAMAIO_E2E_LINK"] else {
            throw XCTSkip("prova col servizio vero: ios/scripts/e2e.sh swift test --package-path ios/CalamaioKit --filter E2E")
        }
        let l = try PairingLink.parse(raw)
        let t = try await PCClient(pin: l.fingerprint).pair(host: l.hosts[0], port: l.port, code: l.code, name: "Prova e2e")
        cached = (l, t)
        return (l, t)
    }
}

final class E2ETests: XCTestCase {
    func testPairAndPinAgainstRealService() async throws {
        let (l, token) = try await E2E.session()
        let host = l.hosts[0]
        let client = PCClient(pin: l.fingerprint)
        let me = try await client.whoami(host: host, port: l.port, token: token)
        XCTAssertEqual(me.phone.name, "Prova e2e")
        XCTAssertEqual(me.port, l.port)
        XCTAssertEqual(me.hosts, ["127.0.0.1"])
        do { _ = try await client.whoami(host: host, port: l.port, token: String(repeating: "0", count: 64)); XCTFail("token falso accettato") }
        catch PCError.unauthorized {}
        do { _ = try await PCClient(pin: String(repeating: "0", count: 64)).whoami(host: host, port: l.port, token: token); XCTFail("impronta sbagliata accettata") }
        catch PCError.pinMismatch {}
        do { _ = try await client.pair(host: host, port: l.port, code: l.code, name: "doppione"); XCTFail("codice riusato accettato") }
        catch PCError.pairingRejected {}
        do { _ = try await client.whoami(host: "127.0.0.1", port: 1, token: token); XCTFail("porta chiusa") }
        catch PCError.unreachable {}
    }
}
