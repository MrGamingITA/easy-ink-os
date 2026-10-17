import Foundation

// Link del QR di abbinamento (spec §3.2, phones.Link nel servizio):
// calamaio://pair?v=1&h=<ip1,ip2>&p=8767&fp=<64 cifre>&c=<32 cifre>&n=<nome PC>
public struct PairingLink: Equatable, Sendable {
    public let hosts: [String]
    public let port: Int
    public let fingerprint: String // SHA-256 del certificato del PC, 64 cifre esadecimali minuscole
    public let code: String        // codice monouso, 32 cifre esadecimali minuscole
    public let name: String

    public enum ParseError: Error, Equatable { case notCalamaio, version, hosts, port, fingerprint, code, name }

    public static func parse(_ text: String) throws -> PairingLink {
        let t = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let c = URLComponents(string: t), c.scheme == "calamaio", c.host == "pair",
              c.path.isEmpty, c.user == nil, c.password == nil, c.port == nil, c.fragment == nil else { throw ParseError.notCalamaio }
        // Un parametro ripetuto rende il link ambiguo: rifiutato (i parametri sconosciuti si ignorano).
        let known: [String: ParseError] = ["v": .version, "h": .hosts, "p": .port, "fp": .fingerprint, "c": .code, "n": .name]
        var q: [String: String] = [:]
        for it in c.queryItems ?? [] {
            if q[it.name] != nil, let e = known[it.name] { throw e }
            q[it.name] = it.value ?? ""
        }
        guard q["v"] == "1" else { throw ParseError.version }
        let hosts = (q["h"] ?? "").split(separator: ",", omittingEmptySubsequences: false).map(String.init)
        guard (1...4).contains(hosts.count), hosts.allSatisfy(isIPv4) else { throw ParseError.hosts }
        guard let ps = q["p"], ps.utf8.count <= 5, ps.utf8.allSatisfy({ $0 >= 48 && $0 <= 57 }),
              let p = Int(ps), (1...65535).contains(p) else { throw ParseError.port }
        guard let fp = q["fp"], isLowerHex(fp, 64) else { throw ParseError.fingerprint }
        guard let code = q["c"], isLowerHex(code, 32) else { throw ParseError.code }
        let name = String(cleanName(q["n"] ?? "").prefix(64))
        return PairingLink(hosts: hosts, port: p, fingerprint: fp, code: code, name: name.isEmpty ? "PC" : name)
    }

    // Toglie controlli, a capo e caratteri di formato (override bidi inclusi): niente nomi che ingannano la grafica.
    static func cleanName(_ s: String) -> String {
        let bad: Set<Unicode.GeneralCategory> = [.control, .format, .lineSeparator, .paragraphSeparator, .surrogate, .privateUse, .unassigned]
        var u = String.UnicodeScalarView()
        u.append(contentsOf: s.unicodeScalars.filter { !bad.contains($0.properties.generalCategory) })
        return String(u).trimmingCharacters(in: .whitespacesAndNewlines)
    }

    public static func isLowerHex(_ s: String, _ n: Int) -> Bool {
        s.utf8.count == n && s.utf8.allSatisfy { ($0 >= 48 && $0 <= 57) || ($0 >= 97 && $0 <= 102) }
    }

    // IPv4 con quattro numeri 0-255 senza zeri iniziali (il servizio manda solo IPv4: phones.LocalIPv4s)
    static func isIPv4(_ s: String) -> Bool {
        let parts = s.split(separator: ".", omittingEmptySubsequences: false)
        return parts.count == 4 && parts.allSatisfy { o in
            !o.isEmpty && o.utf8.count <= 3 && o.utf8.allSatisfy({ $0 >= 48 && $0 <= 57 })
                && (o.utf8.count == 1 || o.utf8.first != 48) && Int(o)! <= 255
        }
    }
}
