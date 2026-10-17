import Foundation

public struct Endpoint: Hashable, Sendable {
    public let host: String
    public let port: Int
    public init(host: String, port: Int) { self.host = host; self.port = port }
}

// Un servizio «_calamaio-app._tcp» trovato con Bonjour: indirizzo IPv4, porta, TXT fp (16 cifre).
public struct BonjourHit: Equatable, Sendable {
    public let host: String
    public let port: Int
    public let fpPrefix: String
    public init(host: String, port: Int, fpPrefix: String) { self.host = host; self.port = port; self.fpPrefix = fpPrefix }
}

public enum Locator {
    // Ordine dei tentativi: l'ultimo indirizzo buono, Bonjour con la nostra impronta (mai un altro PC), poi gli
    // indirizzi conosciuti. Senza doppioni.
    public static func candidates(pc: PairedPC, bonjour: [BonjourHit]) -> [Endpoint] {
        var out: [Endpoint] = []
        func add(_ e: Endpoint) { if !out.contains(e) { out.append(e) } }
        if let h = pc.lastHost { add(Endpoint(host: h, port: pc.port)) }
        let prefix = String(pc.fingerprint.prefix(16))
        for b in bonjour where b.fpPrefix == prefix { add(Endpoint(host: b.host, port: b.port)) }
        for h in pc.hosts { add(Endpoint(host: h, port: pc.port)) }
        return out
    }
}

public enum Reach: Equatable, Sendable {
    case ready(Endpoint)
    case notFound
    case revoked
    case certChanged
    case cancelled // chi ha chiesto la ricerca l'ha annullata: niente esito da mostrare
}

public enum Connector {
    // Prova i candidati con /app/whoami. 401 ferma subito (revocato); un annullamento ferma la ricerca. Un'impronta
    // diversa conta come «certificato cambiato» solo su un indirizzo conosciuto (lastHost o hosts): un annuncio
    // Bonjour col nostro prefisso e un certificato diverso è un impostore (un certificato nuovo cambierebbe anche il
    // TXT fp) e si salta. Il PC aggiornato tiene l'indirizzo buono e quelli che il PC dice di avere adesso.
    public static func connect(pc: PairedPC, token: String, hits: [BonjourHit],
                               probe: (Endpoint) async throws -> WhoAmI) async -> (Reach, PairedPC) {
        var state = Attempt()
        return await state.run(Locator.candidates(pc: pc, bonjour: hits), pc: pc, probe: probe) ?? state.end(pc)
    }

    // Come sopra, ma l'ultimo indirizzo buono si prova subito, mentre Bonjour cerca in parallelo; se risponde (o
    // revoca) la ricerca si annulla.
    public static func connect(pc: PairedPC, token: String, scan: @escaping @Sendable () async -> [BonjourHit],
                               probe: (Endpoint) async throws -> WhoAmI) async -> (Reach, PairedPC) {
        let scanning = Task { await scan() }
        defer { scanning.cancel() }
        var state = Attempt()
        let first = Locator.candidates(pc: pc, bonjour: []).prefix(pc.lastHost == nil ? 0 : 1)
        if let r = await state.run(Array(first), pc: pc, probe: probe) { return r }
        let hits = await withTaskCancellationHandler { await scanning.value } onCancel: { scanning.cancel() }
        let rest = Locator.candidates(pc: pc, bonjour: hits).filter { !first.contains($0) }
        return await state.run(rest, pc: pc, probe: probe) ?? state.end(pc)
    }

    private struct Attempt {
        var mismatch = false

        // nil = nessun esito definitivo su questi candidati: si continua.
        mutating func run(_ eps: [Endpoint], pc: PairedPC, probe: (Endpoint) async throws -> WhoAmI) async -> (Reach, PairedPC)? {
            let known = Set(Locator.candidates(pc: pc, bonjour: []))
            for ep in eps {
                if Task.isCancelled { return (.cancelled, pc) }
                do {
                    let me = try await probe(ep)
                    var up = pc
                    up.lastHost = ep.host
                    up.port = ep.port
                    let fresh = me.hosts.filter(PairingLink.isIPv4)
                    if !fresh.isEmpty { up.hosts = Array(fresh.prefix(4)) }
                    return (.ready(ep), up)
                } catch PCError.unauthorized {
                    return (.revoked, pc)
                } catch PCError.pinMismatch {
                    if known.contains(ep) { mismatch = true }
                } catch PCError.cancelled {
                    return (.cancelled, pc)
                } catch {
                    continue
                }
            }
            return Task.isCancelled ? (.cancelled, pc) : nil
        }

        func end(_ pc: PairedPC) -> (Reach, PairedPC) { (mismatch ? .certChanged : .notFound, pc) }
    }
}
