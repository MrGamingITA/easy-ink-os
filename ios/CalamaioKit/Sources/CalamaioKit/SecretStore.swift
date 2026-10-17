import Foundation
import Security

// Segreti dell'app (il token del PC). Nell'app il Portachiavi; nei test un dizionario.
public protocol SecretStore: Sendable {
    func read(_ key: String) throws -> String?
    func write(_ key: String, _ value: String) throws
    func delete(_ key: String) throws
}

public final class MemorySecretStore: SecretStore, @unchecked Sendable {
    private let lock = NSLock()
    private var items: [String: String] = [:]
    public init() {}
    public func read(_ key: String) throws -> String? { lock.lock(); defer { lock.unlock() }; return items[key] }
    public func write(_ key: String, _ value: String) throws { lock.lock(); items[key] = value; lock.unlock() }
    public func delete(_ key: String) throws { lock.lock(); items[key] = nil; lock.unlock() }
}

public struct KeychainError: Error, Equatable { public let status: OSStatus }

// Portachiavi: solo questo dispositivo, leggibile dopo il primo sblocco, mai sincronizzato con iCloud (spec §3.2 punto 4).
public struct KeychainStore: SecretStore {
    public let service: String
    public init(service: String) { self.service = service }

    private func base(_ key: String) -> [CFString: Any] {
        [kSecClass: kSecClassGenericPassword, kSecAttrService: service, kSecAttrAccount: key,
         kSecUseDataProtectionKeychain: true, kSecAttrSynchronizable: false]
    }

    public func read(_ key: String) throws -> String? {
        var q = base(key)
        q[kSecReturnData] = true
        q[kSecMatchLimit] = kSecMatchLimitOne
        var out: CFTypeRef?
        let st = SecItemCopyMatching(q as CFDictionary, &out)
        if st == errSecItemNotFound { return nil }
        guard st == errSecSuccess, let d = out as? Data else { throw KeychainError(status: st) }
        return String(data: d, encoding: .utf8)
    }

    // Aggiorna se c'è, altrimenti aggiunge: un errore non cancella mai il token di prima.
    public func write(_ key: String, _ value: String) throws {
        let attrs: [CFString: Any] = [kSecValueData: Data(value.utf8),
                                      kSecAttrAccessible: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly]
        let up = SecItemUpdate(base(key) as CFDictionary, attrs as CFDictionary)
        if up == errSecSuccess { return }
        guard up == errSecItemNotFound else { throw KeychainError(status: up) }
        let st = SecItemAdd(base(key).merging(attrs) { $1 } as CFDictionary, nil)
        guard st == errSecSuccess else { throw KeychainError(status: st) }
    }

    public func delete(_ key: String) throws {
        let st = SecItemDelete(base(key) as CFDictionary)
        guard st == errSecSuccess || st == errSecItemNotFound else { throw KeychainError(status: st) }
    }
}
