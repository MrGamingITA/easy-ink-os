import XCTest
@testable import CalamaioKit

final class PairingLinkTests: XCTestCase {
    func testParsesTheServiceLink() throws {
        let v = try Vectors.load()
        let l = try PairingLink.parse("  " + v.link.uri + "\n")
        XCTAssertEqual(l.hosts, v.link.hosts)
        XCTAssertEqual(l.port, v.link.port)
        XCTAssertEqual(l.fingerprint, v.cert.sha256)
        XCTAssertEqual(l.code, v.link.code)
        XCTAssertEqual(l.name, v.link.name) // «Mac di Prova & C.»: %20 e %26 decodificati
    }

    func testRejectsBadLinks() throws {
        let v = try Vectors.load()
        let good = v.link.uri
        let fp = v.cert.sha256
        let bad: [(String, PairingLink.ParseError)] = [
            (good.replacingOccurrences(of: "calamaio://", with: "https://"), .notCalamaio),
            ("calamaio://other?v=1", .notCalamaio),
            ("", .notCalamaio),
            ("non un link", .notCalamaio),
            (good.replacingOccurrences(of: "v=1", with: "v=2"), .version),
            (good.replacingOccurrences(of: "h=192.168.1.20,10.0.0.7", with: "h="), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20", with: "h=evil.example/x"), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20", with: "h=[::1]"), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20", with: "h=1.1.1.1,2.2.2.2,3.3.3.3,4.4.4.4,5.5.5.5"), .hosts),
            (good.replacingOccurrences(of: "p=8767", with: "p=0"), .port),
            (good.replacingOccurrences(of: "p=8767", with: "p=70000"), .port),
            (good.replacingOccurrences(of: fp, with: fp.uppercased()), .fingerprint),
            (good.replacingOccurrences(of: fp, with: String(fp.dropLast())), .fingerprint),
            (good.replacingOccurrences(of: "&c=", with: "&x="), .code),
        ]
        for (text, want) in bad {
            XCTAssertThrowsError(try PairingLink.parse(text), text) { err in
                XCTAssertEqual(err as? PairingLink.ParseError, want, text)
            }
        }
    }

    func testEmptyNameBecomesPC() throws {
        let v = try Vectors.load()
        let l = try PairingLink.parse(v.link.uri.components(separatedBy: "&n=")[0])
        XCTAssertEqual(l.name, "PC")
    }

    func testRejectsStrictness() throws {
        let v = try Vectors.load()
        let good = v.link.uri
        let bad: [(String, PairingLink.ParseError)] = [
            (good.replacingOccurrences(of: "calamaio://pair?", with: "calamaio://pair/evil?"), .notCalamaio),
            (good.replacingOccurrences(of: "calamaio://pair?", with: "calamaio://pair/?"), .notCalamaio),
            (good.replacingOccurrences(of: "calamaio://pair?", with: "calamaio://user:pw@pair?"), .notCalamaio),
            (good.replacingOccurrences(of: "calamaio://pair?", with: "calamaio://user@pair?"), .notCalamaio),
            (good.replacingOccurrences(of: "calamaio://pair?", with: "calamaio://pair:99?"), .notCalamaio),
            (good + "#frag", .notCalamaio),
            (good + "&h=10.0.0.9", .hosts),
            (good + "&v=1", .version),
            (good + "&p=8767", .port),
            (good + "&fp=" + v.cert.sha256, .fingerprint),
            (good + "&c=" + v.link.code, .code),
            (good + "&n=x", .name),
            (good.replacingOccurrences(of: "h=192.168.1.20,10.0.0.7", with: "h=192.168.1.20,,10.0.0.7"), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20,10.0.0.7", with: "h=192.168.1.20,"), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20", with: "h=0"), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20", with: "h=localhost"), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20", with: "h=evil.example"), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20", with: "h=999.1.1.1"), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20", with: "h=192.168.01.20"), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20", with: "h=1.2.3"), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20", with: "h=1.2.3.4.5"), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20", with: "h=-..-"), .hosts),
            (good.replacingOccurrences(of: "p=8767", with: "p=%2B8767"), .port),
            (good.replacingOccurrences(of: "p=8767", with: "p=-1"), .port),
            (good.replacingOccurrences(of: "p=8767", with: "p="), .port),
            (good.replacingOccurrences(of: "p=8767", with: "p=87%2067"), .port),
        ]
        for (text, want) in bad {
            XCTAssertThrowsError(try PairingLink.parse(text), text) { err in
                XCTAssertEqual(err as? PairingLink.ParseError, want, text)
            }
        }
    }

    func testNameIsCleaned() throws {
        let v = try Vectors.load()
        let base = v.link.uri.components(separatedBy: "&n=")[0]
        // NUL, a capo, tab, e override bidi (U+202E, U+2066) spariscono
        let l = try PairingLink.parse(base + "&n=Mac%00%0Abook%09%E2%80%AEx%E2%81%A6y")
        XCTAssertEqual(l.name, "Macbookxy")
        XCTAssertEqual(try PairingLink.parse(base + "&n=%0A%E2%80%AE%20").name, "PC")
        let long = try PairingLink.parse(base + "&n=" + String(repeating: "a", count: 300))
        XCTAssertEqual(long.name.count, 64)
    }
}
