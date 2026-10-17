import XCTest
@testable import CalamaioKit

final class PairingStoreTests: XCTestCase {
    // Ogni valore delle preferenze come testo: le stringhe così come sono, i Data decodificati in UTF-8
    // (la description di Data è esadecimale troncata e non conterrebbe mai il token).
    func storedTexts(_ d: UserDefaults) -> [String] {
        func texts(_ v: Any) -> [String] {
            switch v {
            case let s as String: return [s]
            case let b as Data: return [String(decoding: b, as: UTF8.self)]
            case let a as [Any]: return a.flatMap(texts)
            case let m as [String: Any]: return m.keys.map { $0 } + m.values.flatMap(texts)
            default: return ["\(v)"]
            }
        }
        return d.dictionaryRepresentation().flatMap { [$0.key] + texts($0.value) }
    }

    func testSaveLoadUpdateForget() throws {
        let suite = "calamaio-test-\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        let secrets = MemorySecretStore()
        let st = PairingStore(defaults: defaults, secrets: secrets)
        XCTAssertNil(st.load())
        let tok = String(repeating: "c", count: 64)
        try st.save(pcA(), token: tok)
        let (pc, t) = try XCTUnwrap(st.load())
        XCTAssertEqual(pc, pcA())
        XCTAssertEqual(t, tok)
        // il token non sta nelle preferenze, solo nel segreto; il PC sì (la prova sa leggere i Data)
        let texts = storedTexts(defaults)
        XCTAssertTrue(texts.contains { $0.contains(fpA) })
        XCTAssertFalse(texts.contains { $0.contains(tok) })
        var moved = pcA()
        moved.lastHost = "10.0.0.7"
        st.update(moved)
        XCTAssertEqual(st.load()?.0.lastHost, "10.0.0.7")
        st.forget()
        XCTAssertNil(st.load())
        XCTAssertNil(try secrets.read(PairingStore.tokenKey))
        XCTAssertNil(defaults.object(forKey: PairingStore.pcKey))
    }

    func testMissingTokenMeansUnpaired() throws {
        let suite = "calamaio-test-\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        let secrets = MemorySecretStore()
        let st = PairingStore(defaults: defaults, secrets: secrets)
        try st.save(pcA(), token: String(repeating: "c", count: 64))
        try secrets.delete(PairingStore.tokenKey) // per esempio un ripristino del telefono senza Portachiavi
        XCTAssertNil(st.load())
    }
}
