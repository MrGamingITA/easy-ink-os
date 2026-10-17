import XCTest
@testable import CalamaioKit

// Conferma per i link esterni (I1), attesa tra i ricollegamenti (M3), file scaricati (M4).
@MainActor
final class AppHelpersTests: XCTestCase {
    // linkActivated è necessario ma non basta (un a.click() da script è «linkActivated»): ogni apertura passa da
    // una conferma nativa, una alla volta, mai entro 2 s dalla chiusura della precedente, solo con l'app attiva.
    func testExternalOpenGate() {
        let g = ExternalOpenGate()
        let t0 = Date(timeIntervalSince1970: 1000)
        XCTAssertFalse(g.mayAsk(now: t0, appActive: false)) // app non attiva: niente
        XCTAssertTrue(g.mayAsk(now: t0, appActive: true))
        XCTAssertFalse(g.mayAsk(now: t0, appActive: true)) // una già aperta: la seconda si scarta
        XCTAssertFalse(g.mayAsk(now: t0 + 10, appActive: true))
        g.dismissed(now: t0 + 20)
        XCTAssertFalse(g.mayAsk(now: t0 + 21, appActive: true)) // meno di 2 s dalla chiusura
        XCTAssertFalse(g.mayAsk(now: t0 + 21.99, appActive: true))
        XCTAssertTrue(g.mayAsk(now: t0 + 22, appActive: true))
        g.dismissed(now: t0 + 30)
        XCTAssertFalse(g.mayAsk(now: t0 + 40, appActive: false))
        XCTAssertTrue(g.mayAsk(now: t0 + 40, appActive: true))
    }

    func testReconnectBackoff() {
        let b = ReconnectBackoff()
        XCTAssertEqual((0..<6).map { _ in b.next() }, [1, 2, 5, 10, 10, 10])
        b.reset()
        XCTAssertEqual(b.next(), 1)
    }

    func testDownloadFolder() throws {
        let tmp = FileManager.default.temporaryDirectory.appendingPathComponent("calamaio-dl-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: tmp) }
        let f = DownloadFolder(base: tmp)
        let a = try f.file(named: "t1.theme.json")
        let b = try f.file(named: "../x.gb")
        XCTAssertEqual(a.lastPathComponent, "t1.theme.json")
        XCTAssertEqual(b.lastPathComponent, "x.gb")
        XCTAssertNotEqual(a.deletingLastPathComponent(), b.deletingLastPathComponent()) // una cartella per scaricamento
        try Data("a".utf8).write(to: a)
        try Data("b".utf8).write(to: b)
        f.remove(a) // dopo il foglio di condivisione: via file e cartella
        XCTAssertFalse(FileManager.default.fileExists(atPath: a.deletingLastPathComponent().path))
        XCTAssertTrue(FileManager.default.fileExists(atPath: b.path))
        f.remove(URL(fileURLWithPath: "/etc/hosts")) // fuori dalla cartella: niente
        XCTAssertTrue(FileManager.default.fileExists(atPath: "/etc/hosts"))
        f.purge() // all'avvio: i resti vecchi
        XCTAssertFalse(FileManager.default.fileExists(atPath: b.path))
    }
}
