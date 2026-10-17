import XCTest
import Combine
@testable import CalamaioKit

@MainActor
final class AppControllerTests: XCTestCase {
    var pairCalls: [(host: String, pin: String, code: String, old: String?, oldFp: String?)] = []
    var probe: (Endpoint) throws -> WhoAmI = { _ in who(["127.0.0.1"]) }
    var pairResult: (String) throws -> String = { _ in String(repeating: "d", count: 64) }
    var suites: [String] = []

    override func tearDown() async throws {
        for s in suites { UserDefaults().removePersistentDomain(forName: s) }
        suites = []
    }

    func newDefaults() -> UserDefaults {
        let s = "calamaio-test-\(UUID().uuidString)"
        suites.append(s)
        return UserDefaults(suiteName: s)!
    }

    func makeController(defaults: UserDefaults? = nil, secrets: SecretStore = MemorySecretStore()) -> AppController {
        let d = defaults ?? newDefaults()
        let deps = AppController.Deps(
            pair: { [unowned self] host, _, pin, code, _, old, oldFp in
                self.pairCalls.append((host, pin, code, old, oldFp))
                return try self.pairResult(host)
            },
            whoami: { [unowned self] ep, _, _ in try self.probe(ep) },
            scan: { _ in [] },
            deviceName: { "iPhone di prova" })
        return AppController(store: PairingStore(defaults: d, secrets: secrets), deps: deps)
    }

    func link(hosts: String = "127.0.0.1", fp: String = fpA) -> String {
        "calamaio://pair?v=1&h=\(hosts)&p=8767&fp=\(fp)&c=\(String(repeating: "1", count: 32))&n=MacBook"
    }

    func testPairFromLinkThenReady() async {
        let c = makeController()
        XCTAssertEqual(c.phase, .unpaired)
        let err = await c.pair(with: link())
        XCTAssertNil(err)
        XCTAssertEqual(c.phase, .ready(Endpoint(host: "127.0.0.1", port: 8767)))
        XCTAssertEqual(c.upstream, Upstream(endpoint: Endpoint(host: "127.0.0.1", port: 8767), token: String(repeating: "d", count: 64)))
        XCTAssertEqual(c.pc?.name, "MacBook")
        XCTAssertEqual(pairCalls.first?.pin, fpA)
        XCTAssertNil(pairCalls.first?.old)
        XCTAssertNil(pairCalls.first?.oldFp)
    }

    func testBadLinkAndUnreachable() async {
        let c = makeController()
        let bad = await c.pair(with: "https://example.com")
        XCTAssertEqual(bad, Texts.badLink)
        pairResult = { _ in throw PCError.unreachable }
        let unreachable = await c.pair(with: link(hosts: "192.168.1.20,10.0.0.7"))
        XCTAssertEqual(unreachable, Texts.notFound)
        XCTAssertEqual(pairCalls.map(\.host), ["192.168.1.20", "10.0.0.7"]) // prova ogni indirizzo del QR
        XCTAssertEqual(c.phase, .unpaired)
    }

    func testPairPinMismatchAndRejected() async {
        let c = makeController()
        pairResult = { _ in throw PCError.pinMismatch }
        let mismatch = await c.pair(with: link())
        XCTAssertEqual(mismatch, Texts.pairPinMismatch)
        pairResult = { _ in throw PCError.pairingRejected("codice sbagliato") }
        let rejected = await c.pair(with: link())
        XCTAssertEqual(rejected, Texts.pairRejected + "codice sbagliato")
    }

    func testRepairSamePCSendsOldToken() async {
        let c = makeController()
        _ = await c.pair(with: link())
        pairResult = { _ in String(repeating: "e", count: 64) }
        _ = await c.pair(with: link())
        XCTAssertEqual(pairCalls.last?.old, String(repeating: "d", count: 64))
        XCTAssertEqual(pairCalls.last?.oldFp, fpA) // l'impronta salvata col token: PCClient la ricontrolla
        _ = await c.pair(with: link(fp: fpB)) // un altro PC: niente token vecchio
        XCTAssertNil(pairCalls.last?.old)
    }

    func testStartupStatesAndForget() async throws {
        let d = newDefaults()
        let secrets = MemorySecretStore()
        _ = await makeController(defaults: d, secrets: secrets).pair(with: link())
        let c = makeController(defaults: d, secrets: secrets) // riavvio dell'app
        XCTAssertEqual(c.phase, .connecting)
        probe = { _ in throw PCError.unauthorized }
        await c.connect()
        XCTAssertEqual(c.phase, .failed(Texts.revoked))
        XCTAssertNil(c.upstream)
        probe = { _ in throw PCError.pinMismatch }
        await c.connect()
        XCTAssertEqual(c.phase, .failed(Texts.certChanged))
        c.forget()
        XCTAssertEqual(c.phase, .unpaired)
        XCTAssertNil(makeController(defaults: d, secrets: secrets).pc)
    }

    // «Dimentica» mentre la ricerca è in corso: il risultato vecchio non rimette il PC né lo stato pronto.
    func testForgetDuringConnectWins() async {
        let d = newDefaults()
        let secrets = MemorySecretStore()
        _ = await makeController(defaults: d, secrets: secrets).pair(with: link())
        let c = makeController(defaults: d, secrets: secrets)
        probe = { [unowned c] _ in c.forget(); return who(["127.0.0.1", "10.0.0.9"]) }
        await c.connect()
        XCTAssertEqual(c.phase, .unpaired)
        XCTAssertNil(c.pc)
        XCTAssertNil(c.upstream)
        XCTAssertNil(makeController(defaults: d, secrets: secrets).pc)
    }

    // Una ricerca annullata non mostra «PC non trovato»: resta lo stato di prima.
    func testCancelledConnectKeepsPreviousState() async {
        let d = newDefaults()
        let secrets = MemorySecretStore()
        _ = await makeController(defaults: d, secrets: secrets).pair(with: link())
        let c = makeController(defaults: d, secrets: secrets)
        probe = { _ in throw PCError.unauthorized }
        await c.connect()
        XCTAssertEqual(c.phase, .failed(Texts.revoked))
        probe = { _ in throw PCError.cancelled }
        await c.connect()
        XCTAssertEqual(c.phase, .failed(Texts.revoked))
        // compito annullato prima di partire
        probe = { _ in who(["127.0.0.1"]) }
        let t = Task { await c.connect() }
        t.cancel()
        await t.value
        XCTAssertEqual(c.phase, .failed(Texts.revoked))
        // all'avvio («connecting») un annullamento non lascia mai «connecting» (nessun pulsante) né «non trovato»
        let c2 = makeController(defaults: d, secrets: secrets)
        probe = { _ in throw PCError.cancelled }
        await c2.connect()
        XCTAssertEqual(c2.phase, .failed(Texts.searchStopped))
    }

    func testCancelledPairIsNotNotFound() async {
        let c = makeController()
        pairResult = { _ in throw PCError.cancelled }
        let err = await c.pair(with: link(hosts: "192.168.1.20,10.0.0.7"))
        XCTAssertEqual(err, Texts.cancelled)
        XCTAssertEqual(pairCalls.map(\.host), ["192.168.1.20"])
        XCTAssertEqual(c.phase, .unpaired)
    }

    // «Annulla» nel foglio (M1): il PC ha già risposto col token, ma la richiesta era annullata → niente salvato.
    func testCancelledPairIsNotSaved() async {
        let d = newDefaults()
        let secrets = MemorySecretStore()
        let deps = AppController.Deps(
            pair: { _, _, _, _, _, _, _ in
                // un PC che risponde comunque, anche dopo l'annullamento
                try? await Task.sleep(for: .milliseconds(150))
                return String(repeating: "d", count: 64)
            },
            whoami: { _, _, _ in who(["127.0.0.1"]) },
            scan: { _ in [] },
            deviceName: { "iPhone di prova" })
        let c = AppController(store: PairingStore(defaults: d, secrets: secrets), deps: deps)
        let t = Task { await c.pair(with: link()) }
        try? await Task.sleep(for: .milliseconds(30))
        t.cancel()
        let err = await t.value
        XCTAssertEqual(err, Texts.cancelled)
        XCTAssertNil(c.pc)
        XCTAssertEqual(c.phase, .unpaired)
        XCTAssertNil(PairingStore(defaults: d, secrets: secrets).load())
    }

    // R1: «Annulla» dopo il salvataggio (whoami lento) non ferma la ricerca: l'abbinamento è fatto e si arriva a «ready».
    func testCancelAfterSaveStillConnects() async {
        let d = newDefaults()
        let secrets = MemorySecretStore()
        var saved = false
        let deps = AppController.Deps(
            pair: { _, _, _, _, _, _, _ in String(repeating: "d", count: 64) },
            whoami: { _, _, _ in
                saved = true
                try? await Task.sleep(for: .milliseconds(200))
                return who(["127.0.0.1"])
            },
            scan: { _ in [] },
            deviceName: { "iPhone di prova" })
        let c = AppController(store: PairingStore(defaults: d, secrets: secrets), deps: deps)
        let t = Task { await c.pair(with: link()) }
        while !saved { await Task.yield() }
        t.cancel()
        let err = await t.value
        XCTAssertNil(err)
        XCTAssertNotNil(PairingStore(defaults: d, secrets: secrets).load())
        XCTAssertEqual(c.phase, .ready(Endpoint(host: "127.0.0.1", port: 8767)))
    }

    // I1: al ritorno in primo piano da «ready» si ricontrolla il PC senza passare da «Cerco il PC…» (la vista web
    // resta quella di prima se l'esito è ancora «ready»).
    func testForegroundFromReadyKeepsPage() async throws {
        let c = makeController()
        _ = await c.pair(with: link())
        let ready = Phase.ready(Endpoint(host: "127.0.0.1", port: 8767))
        XCTAssertEqual(c.phase, ready)
        var seen: [Phase] = []
        let sub = c.$phase.dropFirst().sink { seen.append($0) }
        defer { sub.cancel() }
        var probes = 0
        probe = { _ in probes += 1; return who(["127.0.0.1"]) }
        let t = try XCTUnwrap(c.becameActive())
        await t.value
        XCTAssertEqual(probes, 1)
        XCTAssertEqual(c.phase, ready)
        XCTAssertFalse(seen.contains(.connecting))
        XCTAssertTrue(seen.allSatisfy { $0 == ready }, "\(seen)")
    }

    // I1: telefono revocato con l'app aperta: al ritorno in primo piano «revocato».
    func testForegroundFromReadyShowsRevoked() async throws {
        let c = makeController()
        _ = await c.pair(with: link())
        probe = { _ in throw PCError.unauthorized }
        let t = try XCTUnwrap(c.becameActive())
        await t.value
        XCTAssertEqual(c.phase, .failed(Texts.revoked))
        XCTAssertNil(c.upstream)
        // da «failed» si riprova come prima
        probe = { _ in who(["127.0.0.1"]) }
        await c.becameActive()?.value
        XCTAssertEqual(c.phase, .ready(Endpoint(host: "127.0.0.1", port: 8767)))
    }

    func testForegroundWithoutPCDoesNothing() {
        let c = makeController()
        XCTAssertNil(c.becameActive())
        XCTAssertEqual(c.phase, .unpaired)
    }

    // I1: un 401 dalla pagina avvia un ricontrollo solo; una raffica di 401 (o un altro entro la pausa) non ne
    // avvia altri.
    func testUnauthorizedIsDebounced() async throws {
        let c = makeController()
        _ = await c.pair(with: link())
        var probes = 0
        probe = { _ in probes += 1; return who(["127.0.0.1"]) }
        let t0 = Date()
        let first = try XCTUnwrap(c.upstreamUnauthorized(now: t0))
        XCTAssertNil(c.upstreamUnauthorized(now: t0)) // ricontrollo in corso
        XCTAssertNil(c.upstreamUnauthorized(now: t0.addingTimeInterval(0.1)))
        await first.value
        XCTAssertNil(c.upstreamUnauthorized(now: t0.addingTimeInterval(1))) // dentro la pausa
        XCTAssertEqual(probes, 1)
        XCTAssertEqual(c.phase, .ready(Endpoint(host: "127.0.0.1", port: 8767)))
        // dopo la pausa: un altro 401, e stavolta il PC dice revocato
        probe = { _ in probes += 1; throw PCError.unauthorized }
        let second = try XCTUnwrap(c.upstreamUnauthorized(now: t0.addingTimeInterval(AppController.unauthorizedPause + 1)))
        await second.value
        XCTAssertEqual(probes, 2)
        XCTAssertEqual(c.phase, .failed(Texts.revoked))
        // fuori da «ready» un 401 non avvia niente (la pagina non dovrebbe più fare richieste)
        XCTAssertNil(c.upstreamUnauthorized(now: t0.addingTimeInterval(100)))
    }

    typealias Phase = AppController.Phase
}
