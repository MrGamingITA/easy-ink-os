import Foundation
import Combine

// Dove mandare le richieste della webapp: PC raggiunto e token.
public struct Upstream: Equatable, Sendable {
    public let endpoint: Endpoint
    public let token: String
    public init(endpoint: Endpoint, token: String) { self.endpoint = endpoint; self.token = token }
}

// Stato dell'app: abbinamento, ricerca del PC, errori da mostrare. Le dipendenze di rete si iniettano (prove).
@MainActor
public final class AppController: ObservableObject {
    public enum Phase: Equatable, Sendable {
        case unpaired
        case connecting
        case ready(Endpoint)
        case failed(String)
    }

    public struct Deps {
        // old e oldFingerprint: token di prima e l'impronta salvata con lui (PCClient li ricontrolla prima di inviarlo)
        public var pair: (_ host: String, _ port: Int, _ pin: String, _ code: String, _ name: String,
                          _ old: String?, _ oldFingerprint: String?) async throws -> String
        public var whoami: (_ ep: Endpoint, _ pin: String, _ token: String) async throws -> WhoAmI
        public var scan: @Sendable (_ fpPrefix: String) async -> [BonjourHit] // solo gli annunci col nostro prefisso
        public var deviceName: () -> String

        public init(pair: @escaping (String, Int, String, String, String, String?, String?) async throws -> String,
                    whoami: @escaping (Endpoint, String, String) async throws -> WhoAmI,
                    scan: @escaping @Sendable (String) async -> [BonjourHit],
                    deviceName: @escaping () -> String) {
            self.pair = pair; self.whoami = whoami; self.scan = scan; self.deviceName = deviceName
        }

        // Rete vera: PCClient con l'impronta fissata, Bonjour.
        public static func live(deviceName: @escaping () -> String) -> Deps {
            Deps(pair: { host, port, pin, code, name, old, oldFp in
                     try await PCClient(pin: pin).pair(host: host, port: port, code: code, name: name,
                                                       oldToken: old, oldTokenFingerprint: oldFp)
                 },
                 whoami: { ep, pin, token in
                     try await PCClient(pin: pin, timeout: 2.5).whoami(host: ep.host, port: ep.port, token: token)
                 },
                 scan: { await BonjourScanner().scan(matching: $0) },
                 deviceName: deviceName)
        }
    }

    @Published public private(set) var phase: Phase = .unpaired
    @Published public private(set) var pc: PairedPC?
    public private(set) var token: String?
    let store: PairingStore
    let deps: Deps
    // Cresce a ogni ricerca, abbinamento o «dimentica»: un risultato arrivato dopo non tocca più lo stato.
    private var generation = 0

    public init(store: PairingStore, deps: Deps) {
        self.store = store
        self.deps = deps
        if let (pc, t) = store.load() {
            self.pc = pc
            self.token = t
            phase = .connecting
        }
    }

    public var upstream: Upstream? {
        guard case let .ready(ep) = phase, let t = token else { return nil }
        return Upstream(endpoint: ep, token: t)
    }

    public func connect() async { await search(keepPage: false) }

    // Ricontrollo (I1): da «ready» la ricerca non passa da «Cerco il PC…», quindi la vista web resta quella di prima
    // finché l'esito è ancora «ready»; da ogni altro stato è connect().
    public func recheck() async { await search(keepPage: true) }

    // Ritorno in primo piano: si ricontrolla il PC da «ready» (telefono revocato, PC sparito) e da «failed». Il
    // compito torna per le prove; nil = niente da fare.
    @discardableResult
    public func becameActive() -> Task<Void, Never>? {
        guard pc != nil else { return nil }
        switch phase {
        case .ready, .failed: return Task { await self.recheck() }
        case .unpaired, .connecting: return nil
        }
    }

    // Pausa dopo un ricontrollo avviato da un 401: una raffica di 401 dalla pagina ne avvia uno solo.
    public static let unauthorizedPause: TimeInterval = 5
    private var unauthorizedAt: Date?
    private var unauthorizedCheck = false

    // Un 401 dal PC a una richiesta della pagina (ProxyCore, FetchBridge): il telefono forse è revocato. Si ricontrolla
    // una volta sola; connect() vede il 401 di whoami e mostra «revocato». nil = scartato (pausa o ricontrollo in corso).
    @discardableResult
    public func upstreamUnauthorized(now: Date = Date()) -> Task<Void, Never>? {
        guard case .ready = phase, !unauthorizedCheck else { return nil }
        if let at = unauthorizedAt, now.timeIntervalSince(at) < Self.unauthorizedPause { return nil }
        unauthorizedAt = now
        unauthorizedCheck = true
        return Task {
            await self.recheck()
            self.unauthorizedCheck = false
        }
    }

    private func search(keepPage: Bool) async {
        guard let pc, let token else { phase = .unpaired; return }
        generation += 1
        let gen = generation
        // annullata: si torna allo stato di prima (mai «non trovato»), ma mai a «connecting», che non ha pulsanti:
        // da non abbinato o da una ricerca in corso si va a «failed» con «Riprova» (R1)
        let before: Phase = (phase == .unpaired || phase == .connecting) ? .failed(Texts.searchStopped) : phase
        let quiet: Bool
        if keepPage, case .ready = phase { quiet = true } else { quiet = false }
        if !quiet { phase = .connecting }
        let whoami = deps.whoami
        let scan = deps.scan
        let prefix = String(pc.fingerprint.prefix(16))
        let (reach, updated) = await Connector.connect(pc: pc, token: token, scan: { await scan(prefix) }) { ep in
            try await whoami(ep, pc.fingerprint, token)
        }
        guard gen == generation else { return }
        if reach == .cancelled || Task.isCancelled { phase = before; return }
        if updated != pc {
            self.pc = updated
            store.update(updated)
        }
        switch reach {
        case let .ready(ep): if phase != .ready(ep) { phase = .ready(ep) } // stesso stato: nessun aggiornamento
        case .notFound: phase = .failed(Texts.notFound)
        // Il token revocato resta salvato (scelta del coordinatore): sul PC non vale più, e se phones.json torna
        // da un backup l'app riparte da sola.
        case .revoked: phase = .failed(Texts.revoked)
        case .certChanged: phase = .failed(Texts.certChanged)
        case .cancelled: phase = before
        }
    }

    // Abbinamento da QR o da link incollato: prova gli indirizzi del QR uno alla volta. nil = fatto.
    public func pair(with text: String) async -> String? {
        let link: PairingLink
        do { link = try PairingLink.parse(text) } catch { return Texts.badLink }
        // stesso PC: il servizio revoca il telefono vecchio. Mai il token a un'impronta diversa.
        let samePC = pc?.fingerprint == link.fingerprint
        let old = samePC ? token : nil
        let oldFp = samePC ? pc?.fingerprint : nil
        var mismatch = false
        for host in link.hosts {
            if Task.isCancelled { return Texts.cancelled }
            do {
                let t = try await deps.pair(host, link.port, link.fingerprint, link.code, deps.deviceName(), old, oldFp)
                // «Annulla» mentre il PC rispondeva: niente salvato (sul PC il telefono resta, si revoca dalla webapp)
                if Task.isCancelled { return Texts.cancelled }
                let p = PairedPC(name: link.name, fingerprint: link.fingerprint, hosts: link.hosts, port: link.port, lastHost: host)
                do { try store.save(p, token: t) } catch { return Texts.keychain }
                generation += 1 // una ricerca vecchia in corso non sovrascrive il PC nuovo
                pc = p
                token = t
                // salvato = abbinato: la ricerca parte in un compito suo, che «Annulla» del foglio non raggiunge (R1)
                await Task { await self.connect() }.value
                return nil
            } catch PCError.pinMismatch {
                mismatch = true
            } catch let PCError.pairingRejected(msg) {
                return Texts.pairRejected + msg
            } catch PCError.cancelled {
                return Texts.cancelled // mai «non trovato» per un abbinamento annullato
            } catch {
                continue
            }
        }
        return mismatch ? Texts.pairPinMismatch : Texts.notFound
    }

    // Dimentica il PC sul telefono (il token resta valido sul PC finché non si revoca dalla webapp).
    public func forget() {
        generation += 1
        store.forget()
        pc = nil
        token = nil
        phase = .unpaired
    }
}
