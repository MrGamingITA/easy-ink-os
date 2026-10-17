import Foundation
import Network

// Cerca «_calamaio-app._tcp» per un attimo e risolve ogni servizio in IPv4 e porta (una connessione TCP che si
// chiude appena pronta). Nel simulatore usa la rete del Mac; sul telefono la prima volta iOS chiede il permesso
// della rete locale. Si ferma subito se il compito viene annullato.
public final class BonjourScanner: @unchecked Sendable {
    public init() {}

    // matching: le prime 16 cifre della nostra impronta; nil = tutti gli annunci con un fp valido.
    public func scan(timeout: TimeInterval = 1.5, matching fpPrefix: String? = nil) async -> [BonjourHit] {
        let found = await Self.browse(timeout: timeout)
        let picked = Self.pick(found, fpPrefix: fpPrefix, cap: 8) // il filtro prima del tetto
        if Task.isCancelled { return [] }
        // risolti in parallelo: al massimo ~2 s in tutto, non 2 s per servizio
        let hits = await withTaskGroup(of: BonjourHit?.self) { g in
            for (ep, fp) in picked { g.addTask { await Self.resolve(ep, fp: fp) } }
            var out: [BonjourHit] = []
            for await h in g { if let h, !out.contains(h) { out.append(h) } }
            return out
        }
        return Task.isCancelled ? [] : hits
    }

    // Tiene gli annunci col fp giusto (16 cifre esadecimali, uguale al prefisso se dato), poi al massimo cap.
    static func pick<E>(_ items: [(E, String)], fpPrefix: String?, cap: Int) -> [(E, String)] {
        let want = fpPrefix?.lowercased()
        let ok = items.compactMap { item -> (E, String)? in
            let f = item.1.lowercased()
            guard PairingLink.isLowerHex(f, 16), want == nil || f == want else { return nil }
            return (item.0, f)
        }
        return Array(ok.prefix(cap))
    }

    // Solo IPv4 senza zona («%en0»), porta valida e fp di 16 cifre esadecimali: il resto si scarta.
    static func hit(host: String, port: Int, fp: String) -> BonjourHit? {
        var h = host
        if let i = h.firstIndex(of: "%") { h = String(h[..<i]) }
        let f = fp.lowercased()
        guard PairingLink.isIPv4(h), (1...65535).contains(port), PairingLink.isLowerHex(f, 16) else { return nil }
        return BonjourHit(host: h, port: port, fpPrefix: f)
    }

    static func browse(timeout: TimeInterval) async -> [(NWEndpoint, String)] {
        let browser = NWBrowser(for: .bonjourWithTXTRecord(type: "_calamaio-app._tcp", domain: nil), using: .tcp)
        let q = DispatchQueue(label: "calamaio.bonjour")
        let items = OnQueue<[(NWEndpoint, String)]>([])
        let cont = OnQueue<CheckedContinuation<[(NWEndpoint, String)], Never>?>(nil)
        // sempre sulla coda q: una sola ripresa, che sia il tempo scaduto o l'annullamento
        @Sendable func finish() {
            guard let c = cont.value else { return }
            cont.value = nil
            browser.cancel()
            c.resume(returning: items.value)
        }
        return await withTaskCancellationHandler {
            await withCheckedContinuation { c in
                q.async {
                    cont.value = c
                    if Task.isCancelled { finish(); return }
                    browser.browseResultsChangedHandler = { results, _ in
                        items.value = results.compactMap { r in
                            guard case let .bonjour(txt) = r.metadata else { return nil }
                            return (r.endpoint, txt["fp"] ?? "")
                        }
                    }
                    browser.start(queue: q)
                    q.asyncAfter(deadline: .now() + timeout) { finish() }
                }
            }
        } onCancel: {
            q.async { finish() }
        }
    }

    static func resolve(_ ep: NWEndpoint, fp: String) async -> BonjourHit? {
        let params = NWParameters.tcp
        if let ip = params.defaultProtocolStack.internetProtocol as? NWProtocolIP.Options { ip.version = .v4 }
        let c = NWConnection(to: ep, using: params)
        let q = DispatchQueue(label: "calamaio.resolve")
        let cont = OnQueue<CheckedContinuation<BonjourHit?, Never>?>(nil)
        let result = OnQueue<BonjourHit?>(nil)
        // sempre sulla coda q: niente doppie riprese
        @Sendable func finish() {
            guard let k = cont.value else { return }
            cont.value = nil
            c.cancel()
            k.resume(returning: result.value)
        }
        return await withTaskCancellationHandler {
            await withCheckedContinuation { k in
                q.async {
                    cont.value = k
                    if Task.isCancelled { finish(); return }
                    c.stateUpdateHandler = { st in
                        switch st {
                        case .ready:
                            if case let .hostPort(host, port)? = c.currentPath?.remoteEndpoint {
                                result.value = hit(host: "\(host)", port: Int(port.rawValue), fp: fp)
                            }
                            finish()
                        case .failed, .cancelled:
                            finish()
                        default:
                            break
                        }
                    }
                    c.start(queue: q)
                    q.asyncAfter(deadline: .now() + 2) { finish() }
                }
            }
        } onCancel: {
            q.async { finish() }
        }
    }
}

// Stato toccato solo dalla coda di Network che lo possiede (browser o connessione).
final class OnQueue<T>: @unchecked Sendable {
    var value: T
    init(_ v: T) { value = v }
}
