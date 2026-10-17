import Foundation

// Il PC abbinato: dati non segreti nelle preferenze, il token solo nel SecretStore.
public struct PairedPC: Codable, Equatable, Sendable {
    public var name: String
    public var fingerprint: String
    public var hosts: [String]   // indirizzi conosciuti (QR, poi /app/whoami)
    public var port: Int
    public var lastHost: String? // l'ultimo che ha risposto

    public init(name: String, fingerprint: String, hosts: [String], port: Int, lastHost: String?) {
        self.name = name; self.fingerprint = fingerprint; self.hosts = hosts; self.port = port; self.lastHost = lastHost
    }
}

public final class PairingStore: @unchecked Sendable {
    static let pcKey = "calamaio.pairedPC"
    static let tokenKey = "pc-token"
    let defaults: UserDefaults
    let secrets: SecretStore

    public init(defaults: UserDefaults, secrets: SecretStore) {
        self.defaults = defaults
        self.secrets = secrets
    }

    // nil se manca il PC o il token (per esempio Portachiavi non ripristinato): l'app torna «non abbinata».
    public func load() -> (PairedPC, String)? {
        guard let d = defaults.data(forKey: Self.pcKey), let pc = try? JSONDecoder().decode(PairedPC.self, from: d),
              PairingLink.isLowerHex(pc.fingerprint, 64),
              let t = try? secrets.read(Self.tokenKey), PairingLink.isLowerHex(t, 64) else { return nil }
        return (pc, t)
    }

    // Prima il token: se il Portachiavi rifiuta, niente PC a metà nelle preferenze.
    public func save(_ pc: PairedPC, token: String) throws {
        try secrets.write(Self.tokenKey, token)
        update(pc)
    }

    public func update(_ pc: PairedPC) {
        if let d = try? JSONEncoder().encode(pc) { defaults.set(d, forKey: Self.pcKey) }
    }

    public func forget() {
        defaults.removeObject(forKey: Self.pcKey)
        try? secrets.delete(Self.tokenKey)
    }
}
