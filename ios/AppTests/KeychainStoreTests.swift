import XCTest
import CalamaioKit
import Security

// Portachiavi vero, solo nel simulatore: un servizio unico per prova, ripulito alla fine.
final class KeychainStoreTests: XCTestCase {
    func testWriteReadOverwriteDelete() throws {
        let ks = KeychainStore(service: "com.mrgamingita.calamaio.tests.\(UUID().uuidString)")
        defer { try? ks.delete("token") }
        XCTAssertNil(try ks.read("token"))
        try ks.write("token", "uno")
        XCTAssertEqual(try ks.read("token"), "uno")
        try ks.write("token", "due") // sovrascrive, niente doppioni
        XCTAssertEqual(try ks.read("token"), "due")
        try ks.delete("token")
        XCTAssertNil(try ks.read("token"))
        try ks.delete("token") // cancellare ciò che non c'è non è un errore
    }

    // Servizi diversi non si vedono: le prove non toccano il token vero dell'app.
    func testServicesAreSeparate() throws {
        let a = KeychainStore(service: "com.mrgamingita.calamaio.tests.\(UUID().uuidString)")
        let b = KeychainStore(service: "com.mrgamingita.calamaio.tests.\(UUID().uuidString)")
        defer { try? a.delete("token"); try? b.delete("token") }
        try a.write("token", "solo-a")
        XCTAssertNil(try b.read("token"))
        XCTAssertEqual(try a.read("token"), "solo-a")
    }

    // L'elemento salvato è solo di questo dispositivo, dopo il primo sblocco, e non va su iCloud.
    func testItemAttributes() throws {
        let service = "com.mrgamingita.calamaio.tests.\(UUID().uuidString)"
        let ks = KeychainStore(service: service)
        defer { try? ks.delete("token") }
        try ks.write("token", "uno")
        try ks.write("token", "due") // anche dopo una sovrascrittura: un solo elemento, stessi attributi
        let q: [CFString: Any] = [kSecClass: kSecClassGenericPassword, kSecAttrService: service, kSecAttrAccount: "token",
                                  kSecUseDataProtectionKeychain: true, kSecAttrSynchronizable: kSecAttrSynchronizableAny,
                                  kSecReturnAttributes: true, kSecMatchLimit: kSecMatchLimitAll]
        var out: CFTypeRef?
        XCTAssertEqual(SecItemCopyMatching(q as CFDictionary, &out), errSecSuccess)
        let items = try XCTUnwrap(out as? [[String: Any]])
        XCTAssertEqual(items.count, 1)
        XCTAssertEqual(items[0][kSecAttrAccessible as String] as? String, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly as String)
        XCTAssertNotEqual(items[0][kSecAttrSynchronizable as String] as? Bool, true)
    }
}
