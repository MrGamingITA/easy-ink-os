import XCTest
@testable import CalamaioKit

let fpA = String(repeating: "a", count: 64)
let fpB = String(repeating: "b", count: 64)

func pcA(lastHost: String? = "192.168.1.20") -> PairedPC {
    PairedPC(name: "MacBook", fingerprint: fpA, hosts: ["192.168.1.20", "10.0.0.7"], port: 8767, lastHost: lastHost)
}

func who(_ hosts: [String]) -> WhoAmI {
    WhoAmI(phone: .init(id: "p1", name: "iPhone"), pc: .init(name: "MacBook", version: "1.0"), hosts: hosts, port: 8767)
}

final class LocatorTests: XCTestCase {
    func testCandidatesOrderAndFilter() {
        let hits = [
            BonjourHit(host: "192.168.1.99", port: 8767, fpPrefix: String(fpB.prefix(16))), // un altro PC Calamaio
            BonjourHit(host: "192.168.1.50", port: 9767, fpPrefix: String(fpA.prefix(16))), // il nostro, IP nuovo
            BonjourHit(host: "192.168.1.20", port: 8767, fpPrefix: String(fpA.prefix(16))), // doppione
        ]
        let c = Locator.candidates(pc: pcA(), bonjour: hits)
        XCTAssertEqual(c, [
            Endpoint(host: "192.168.1.20", port: 8767), // l'ultimo buono
            Endpoint(host: "192.168.1.50", port: 9767), // Bonjour con la nostra impronta
            Endpoint(host: "10.0.0.7", port: 8767),     // gli indirizzi conosciuti
        ])
        XCTAssertEqual(Locator.candidates(pc: pcA(lastHost: nil), bonjour: []).map(\.host), ["192.168.1.20", "10.0.0.7"])
    }

    func testConnectFallsBackAndRemembersHosts() async {
        var tried: [String] = []
        let (reach, updated) = await Connector.connect(pc: pcA(), token: "t", hits: []) { ep in
            tried.append(ep.host)
            if ep.host == "10.0.0.7" { return who(["10.0.0.7", "172.20.0.3"]) }
            throw PCError.unreachable
        }
        XCTAssertEqual(tried, ["192.168.1.20", "10.0.0.7"])
        XCTAssertEqual(reach, .ready(Endpoint(host: "10.0.0.7", port: 8767)))
        XCTAssertEqual(updated.lastHost, "10.0.0.7")
        XCTAssertEqual(updated.hosts, ["10.0.0.7", "172.20.0.3"]) // gli indirizzi di adesso, per il prossimo cambio di rete
    }

    func testConnectReportsCertChanged() async {
        let (reach, updated) = await Connector.connect(pc: pcA(), token: "t", hits: []) { ep in
            if ep.host == "192.168.1.20" { throw PCError.pinMismatch }
            throw PCError.unreachable
        }
        XCTAssertEqual(reach, .certChanged)
        XCTAssertEqual(updated, pcA())
    }

    func testConnectStopsOnRevoked() async {
        var tried = 0
        let (reach, _) = await Connector.connect(pc: pcA(), token: "t", hits: []) { _ in tried += 1; throw PCError.unauthorized }
        XCTAssertEqual(reach, .revoked)
        XCTAssertEqual(tried, 1)
    }

    func testConnectNotFound() async {
        let (reach, _) = await Connector.connect(pc: pcA(), token: "t", hits: []) { _ in throw PCError.unreachable }
        XCTAssertEqual(reach, .notFound)
    }

    // Una richiesta annullata ferma la ricerca: niente altri indirizzi provati, e non è «non trovato».
    func testConnectStopsOnCancel() async {
        var tried = 0
        let (reach, updated) = await Connector.connect(pc: pcA(), token: "t", hits: []) { _ in tried += 1; throw PCError.cancelled }
        XCTAssertEqual(reach, .cancelled)
        XCTAssertEqual(updated, pcA())
        XCTAssertEqual(tried, 1)
    }

    // Un annuncio Bonjour col nostro prefisso ma un certificato diverso è un impostore (un certificato nuovo
    // cambierebbe anche il TXT fp): si salta, e col PC vero spento il risultato è «non trovato».
    func testHostileBonjourHitIsNotCertChanged() async {
        let hostile = BonjourHit(host: "192.168.1.66", port: 8767, fpPrefix: String(fpA.prefix(16)))
        var tried: [String] = []
        let (reach, updated) = await Connector.connect(pc: pcA(), token: "t", hits: [hostile]) { ep in
            tried.append(ep.host)
            if ep.host == "192.168.1.66" { throw PCError.pinMismatch }
            throw PCError.unreachable
        }
        XCTAssertEqual(tried, ["192.168.1.20", "192.168.1.66", "10.0.0.7"])
        XCTAssertEqual(reach, .notFound)
        XCTAssertEqual(updated, pcA())
        // l'impostore prima, poi il PC vero risponde a un indirizzo conosciuto
        let (r2, u2) = await Connector.connect(pc: pcA(), token: "t", hits: [hostile]) { ep in
            if ep.host == "192.168.1.66" { throw PCError.pinMismatch }
            if ep.host == "10.0.0.7" { return who(["10.0.0.7"]) }
            throw PCError.unreachable
        }
        XCTAssertEqual(r2, .ready(Endpoint(host: "10.0.0.7", port: 8767)))
        XCTAssertEqual(u2.lastHost, "10.0.0.7")
        XCTAssertEqual(u2.port, 8767)
    }

    // Un hit Bonjour sullo stesso indirizzo di uno conosciuto resta conosciuto: lì il mismatch è «certificato cambiato».
    func testKnownHostMismatchStillCertChangedWithBonjour() async {
        let same = BonjourHit(host: "10.0.0.7", port: 8767, fpPrefix: String(fpA.prefix(16)))
        let (reach, _) = await Connector.connect(pc: pcA(lastHost: nil), token: "t", hits: [same]) { ep in
            if ep.host == "10.0.0.7" { throw PCError.pinMismatch }
            throw PCError.unreachable
        }
        XCTAssertEqual(reach, .certChanged)
    }

    // L'ultimo indirizzo buono si prova subito, senza aspettare Bonjour; la ricerca si annulla quando non serve.
    func testLastHostDoesNotWaitForScan() async {
        var tried: [String] = []
        let t0 = Date()
        let (reach, _) = await Connector.connect(pc: pcA(), token: "t", scan: {
            try? await Task.sleep(for: .seconds(10)) // finisce subito solo se annullata
            return []
        }) { ep in
            tried.append(ep.host)
            return who([ep.host])
        }
        XCTAssertEqual(reach, .ready(Endpoint(host: "192.168.1.20", port: 8767)))
        XCTAssertEqual(tried, ["192.168.1.20"])
        XCTAssertLessThan(Date().timeIntervalSince(t0), 2)
    }

    // Se l'ultimo indirizzo non risponde si usano i risultati di Bonjour, senza riprovarlo.
    func testScanUsedAfterLastHostFails() async {
        var tried: [String] = []
        let hit = BonjourHit(host: "192.168.1.50", port: 9767, fpPrefix: String(fpA.prefix(16)))
        let (reach, _) = await Connector.connect(pc: pcA(), token: "t", scan: { [hit] }) { ep in
            tried.append(ep.host)
            if ep.host == "192.168.1.50" { return who([]) }
            throw PCError.unreachable
        }
        XCTAssertEqual(reach, .ready(Endpoint(host: "192.168.1.50", port: 9767)))
        XCTAssertEqual(tried, ["192.168.1.20", "192.168.1.50"])
    }

    // Il filtro sull'impronta viene prima del tetto: tanti annunci estranei non spingono fuori il nostro.
    func testPickFiltersBeforeCap() {
        let ours = String(fpA.prefix(16))
        var items = (0..<20).map { ("altro\($0)", String(fpB.prefix(16))) }
        items.append(("nostro", ours.uppercased()))
        let picked = BonjourScanner.pick(items, fpPrefix: ours, cap: 8)
        XCTAssertEqual(picked.map { $0.0 }, ["nostro"])
        XCTAssertEqual(picked.map { $0.1 }, [ours]) // fp portato in minuscolo
        XCTAssertEqual(BonjourScanner.pick((0..<20).map { ("n\($0)", ours) }, fpPrefix: ours, cap: 8).count, 8)
    }

    // La ricerca vera (solo ascolto, nessun annuncio) si ferma appena il compito è annullato.
    func testScanRespectsCancellation() async {
        let t0 = Date()
        let t = Task { await BonjourScanner().scan(timeout: 10, matching: String(fpA.prefix(16))) }
        try? await Task.sleep(for: .milliseconds(100))
        t.cancel()
        let hits = await t.value
        XCTAssertEqual(hits, [])
        XCTAssertLessThan(Date().timeIntervalSince(t0), 2)
    }

    // Risultati di Bonjour (iniettati: nelle prove non si annuncia niente sulla rete): solo IPv4 e fp di 16 cifre.
    func testBonjourHitNormalization() {
        let p = String(fpA.prefix(16))
        XCTAssertEqual(BonjourScanner.hit(host: "192.168.1.50%en0", port: 8767, fp: p),
                       BonjourHit(host: "192.168.1.50", port: 8767, fpPrefix: p))
        XCTAssertEqual(BonjourScanner.hit(host: "192.168.1.50", port: 8767, fp: p.uppercased())?.fpPrefix, p)
        XCTAssertNil(BonjourScanner.hit(host: "fe80::1%en0", port: 8767, fp: p))
        XCTAssertNil(BonjourScanner.hit(host: "calamaio.local", port: 8767, fp: p))
        XCTAssertNil(BonjourScanner.hit(host: "192.168.1.50", port: 0, fp: p))
        XCTAssertNil(BonjourScanner.hit(host: "192.168.1.50", port: 8767, fp: ""))
        XCTAssertNil(BonjourScanner.hit(host: "192.168.1.50", port: 8767, fp: String(fpA.prefix(15))))
        XCTAssertNil(BonjourScanner.hit(host: "192.168.1.50", port: 8767, fp: String(repeating: "z", count: 16)))
    }
}
