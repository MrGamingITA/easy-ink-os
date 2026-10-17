import Foundation
import XCTest
@testable import CalamaioKit

@MainActor
private final class FakePeripheral: PadPeripheral {
    var onEvent: ((PadPeripheralEvent) -> Void)?
    var advertising = false
    var starts = 0, stops = 0
    var sent: [Data] = []
    var accept = true
    var calls: [String] = []  // ordine delle chiamate alla cucitura
    var foreground = false
    func startAdvertising() { advertising = true; starts += 1; calls.append("start") }
    func stopAdvertising() { advertising = false; stops += 1; calls.append("stop") }
    func setForeground(_ on: Bool) { foreground = on; calls.append("fg:\(on)") }
    func send(_ report: Data) -> Bool {
        calls.append("send")
        guard accept else { return false }
        sent.append(report)
        return true
    }
    func fire(_ e: PadPeripheralEvent) { onEvent?(e) }
    var last: PadReport? { sent.last.flatMap(PadReport.decode) }
}

@MainActor
final class PadControllerTests: XCTestCase {
    private var t: TimeInterval = 100
    private var p: FakePeripheral!
    private var c: PadController!

    override func setUp() async throws {
        t = 100
        p = FakePeripheral()
        c = PadController(peripheral: p, now: { [unowned self] in self.t })
    }

    private func connected() {
        p.fire(.poweredOn)
        c.becameActive()
        p.fire(.subscribed)
    }

    func testAdvertisesOnlyActivePoweredAndUnsubscribed() {
        c.becameActive()
        XCTAssertFalse(p.advertising)  // Bluetooth non ancora acceso
        p.fire(.poweredOn)
        XCTAssertTrue(p.advertising)
        p.fire(.subscribed)
        XCTAssertFalse(p.advertising)  // collegati: niente annuncio
        p.fire(.unsubscribed)
        XCTAssertTrue(p.advertising)
        c.willResignActive()
        XCTAssertFalse(p.advertising)
    }

    func testSubscribeSendsCurrentState() {
        connected()
        XCTAssertEqual(p.sent.count, 1)
        XCTAssertEqual(p.last?.buttons, [])
        XCTAssertEqual(p.last?.foreground, true)
        XCTAssertEqual(c.status, .connected)
    }

    func testChangeSendsReportWithGrowingSeq() {
        connected()
        c.setButtons(.a)
        c.setButtons([.a, .b])  // multi-tocco
        c.setButtons([.a, .b])  // uguale: niente
        XCTAssertEqual(p.sent.count, 3)
        XCTAssertEqual(p.last?.buttons, [.a, .b])
        let seqs = p.sent.compactMap(PadReport.decode).map(\.seq)
        XCTAssertEqual(seqs, [seqs[0], seqs[0] &+ 1, seqs[0] &+ 2])
    }

    func testSeqWraps() {
        connected()
        for i in 0..<300 { c.setButtons(i % 2 == 0 ? .a : []) }
        let seqs = p.sent.compactMap(PadReport.decode).map(\.seq)
        for (a, b) in zip(seqs, seqs.dropFirst()) { XCTAssertEqual(b, a &+ 1) }
    }

    func testKeepaliveEverySecondWhenIdle() {
        connected()
        let n = p.sent.count
        t = 100.9; c.tick()  // assegnati, non sommati: niente arrotondamenti dei Double
        XCTAssertEqual(p.sent.count, n)
        t = 101; c.tick()
        XCTAssertEqual(p.sent.count, n + 1)
        XCTAssertEqual(p.last?.buttons, [])
        t = 102; c.tick()
        XCTAssertEqual(p.sent.count, n + 2)
    }

    // Ruling coordinatore: con un tasto tenuto un report ogni 250 ms (l'orologio di tenuta della scheda non va oltre
    // l'ultimo report); a tasti rilasciati si torna a 1 s.
    func testKeepaliveEvery250msWhileHeldThenBackToOneSecond() {
        connected()
        c.setButtons(.a)
        var n = p.sent.count
        for k in 1...12 {  // 100.25 … 103.0 (3 s di tenuta)
            t = 100 + 0.25 * Double(k) - 0.06; c.tick()
            XCTAssertEqual(p.sent.count, n, "troppo presto a \(k)")
            t = 100 + 0.25 * Double(k); c.tick()
            n += 1
            XCTAssertEqual(p.sent.count, n, "manca il mantenimento \(k)")
            XCTAssertEqual(p.last?.buttons, .a)
        }
        t = 103.1; c.tick()  // dopo l'ultimo ancora 0,1 s: niente
        XCTAssertEqual(p.sent.count, n)
        t = 103.25; c.tick()
        n += 1
        XCTAssertEqual(p.sent.count, n)
        c.setButtons([])  // rilascio: un report
        n += 1
        XCTAssertEqual(p.sent.count, n)
        XCTAssertEqual(p.last?.buttons, [])
        t = 104.0; c.tick()  // 0,75 s dopo il rilascio: ancora niente
        XCTAssertEqual(p.sent.count, n)
        t = 104.25; c.tick()  // 1 s
        XCTAssertEqual(p.sent.count, n + 1)
        t = 104.5; c.tick()  // non più a 250 ms
        XCTAssertEqual(p.sent.count, n + 1)
    }

    // Review I1: il passo del timer e' uguale alla soglia; con jitter reale il mantenimento non deve saltare un tick.
    func testHeldKeepaliveSurvivesTimerJitter() {
        connected()
        c.setButtons(.a)
        var times: [TimeInterval] = [t]
        let gaps: [TimeInterval] = [0.24, 0.26, 0.245, 0.255, 0.25, 0.24, 0.26, 0.245, 0.25, 0.255, 0.24, 0.26]
        for g in gaps {
            t += g; c.tick()
            times.append(t)
        }
        let sentCount = p.sent.count
        XCTAssertEqual(sentCount, 2 + gaps.count)  // iscrizione, pressione, un report per tick
        for (a, b) in zip(times, times.dropFirst()) { XCTAssertLessThanOrEqual(b - a, 0.3) }
    }

    func testBecameActiveSendsStateImmediately() {
        connected()
        c.willResignActive()
        let n = p.sent.count
        c.becameActive()
        XCTAssertEqual(p.sent.count, n + 1)
        XCTAssertEqual(p.last?.foreground, true)
        XCTAssertEqual(p.last?.buttons, [])
    }

    func testUnsubscribeClearsPending() {
        connected()
        p.accept = false
        c.setButtons(.a)
        p.fire(.unsubscribed)
        p.accept = true
        let n = p.sent.count
        p.fire(.readyToSend)
        XCTAssertEqual(p.sent.count, n)
    }

    // Ruling coordinatore (F7): in secondo piano «rilasciato» e poi niente annuncio, altrimenti la scheda si
    // ricollega al telefono e lo scollega ogni 5-10 s.
    func testBackgroundReleasesThenNeverAdvertises() {
        connected()
        c.setButtons(.a)
        c.willResignActive()
        XCTAssertEqual(p.last?.buttons, [])
        XCTAssertEqual(p.last?.foreground, false)
        XCTAssertFalse(p.advertising)
        p.fire(.unsubscribed)  // la scheda lascia il collegamento mentre siamo in secondo piano
        p.fire(.poweredOn)
        XCTAssertFalse(p.advertising)
        // non collegati ma in annuncio: lo stop arriva al secondo piano
        c.becameActive()
        XCTAssertTrue(p.advertising)
        let stops = p.stops
        c.willResignActive()
        XCTAssertFalse(p.advertising)
        XCTAssertEqual(p.stops, stops + 1)
        c.becameActive()
        XCTAssertTrue(p.advertising)  // al ritorno si riparte
    }

    // Review Focus «tasti bloccati»: prima del secondo piano un «tutto rilasciato» (flags bit0 = 0), poi silenzio.
    func testResignSendsReleasedThenStops() {
        connected()
        c.setButtons([.a, .left])
        c.willResignActive()
        XCTAssertEqual(p.last?.buttons, [])
        XCTAssertEqual(p.last?.foreground, false)
        XCTAssertEqual(c.status, .paused)
        let n = p.sent.count
        c.setButtons(.b)  // tocchi a app non attiva: ignorati
        t += 5; c.tick()  // niente mantenimento in secondo piano
        XCTAssertEqual(p.sent.count, n)
        XCTAssertEqual(c.buttons, [])
    }

    // Coda piena: al «pronto» parte lo stato di adesso, mai quello vecchio rimasto indietro.
    func testFullQueueResendsLatest() {
        connected()
        p.accept = false
        c.setButtons(.a)
        c.setButtons([])  // rilasciato mentre la coda era piena
        p.accept = true
        p.fire(.readyToSend)
        XCTAssertEqual(p.last?.buttons, [])
        p.fire(.readyToSend)  // niente in sospeso: niente
        XCTAssertEqual(p.sent.count, 2)  // quello dell'iscrizione e il «rilasciato»
    }

    func testHintAfterTenSeconds() {
        p.fire(.poweredOn)
        c.becameActive()
        XCTAssertEqual(c.status, .searching)
        t = 109.9; c.tick()
        XCTAssertEqual(c.status, .searching)
        t = 110; c.tick()
        XCTAssertEqual(c.status, .otherController)
        p.fire(.subscribed)
        XCTAssertEqual(c.status, .connected)
        p.fire(.unsubscribed)
        XCTAssertEqual(c.status, .searching)  // il conto riparte da 110
        t = 120; c.tick()
        XCTAssertEqual(c.status, .otherController)
    }

    func testPowerStates() {
        c.becameActive()
        p.fire(.unauthorized)
        XCTAssertEqual(c.status, .unauthorized)
        p.fire(.unsupported)
        XCTAssertEqual(c.status, .unsupported)
        p.fire(.poweredOn)
        p.fire(.subscribed)
        c.setButtons(.a)
        p.fire(.poweredOff)
        XCTAssertEqual(c.status, .bluetoothOff)
        XCTAssertEqual(c.buttons, [])
    }

    func testHapticOnlyOnNewPressWhenEnabled() {
        connected()
        var n = 0
        c.onHaptic = { n += 1 }
        c.setButtons(.a)
        c.setButtons([.a, .b])
        c.setButtons(.b)  // rilascio di A: niente
        XCTAssertEqual(n, 2)
        c.haptics = false
        c.setButtons([.b, .up])
        XCTAssertEqual(n, 2)
    }

    func testLastReportForReads() {
        connected()
        c.setButtons(.menu)
        XCTAssertEqual(PadReport.decode(c.lastReport)?.buttons, .menu)
    }

    // M2: in secondo piano niente servizio esposto. Prima il «tutto rilasciato», poi il ritiro; l'iscrizione da sola
    // ferma solo l'annuncio, mai il servizio.
    func testResignWithdrawsServiceAfterRelease() {
        connected()
        XCTAssertFalse(p.calls.contains("fg:false"))  // collegati: servizio sempre esposto
        c.setButtons(.a)
        c.willResignActive()
        XCTAssertEqual(Array(p.calls.suffix(2)), ["send", "fg:false"])
        XCTAssertEqual(p.last?.foreground, false)
        XCTAssertFalse(p.foreground)
    }

    func testBecameActiveExposesServiceBeforeAdvertising() {
        p.fire(.poweredOn)
        c.becameActive()
        XCTAssertEqual(p.calls, ["fg:true", "start"])
        c.willResignActive()
        XCTAssertEqual(Array(p.calls.suffix(2)), ["stop", "fg:false"])
        p.calls = []
        c.becameActive()
        XCTAssertEqual(p.calls, ["fg:true", "start"])
    }

    // M1: ogni rilascio forzato (secondo piano, collegamento perso, Bluetooth spento) avvisa la vista, che
    // dimentica le dita (TouchTracker.cancelAll): un dito rimasto non rimanda il vecchio A.
    func testForcedReleasesAreCounted() {
        connected()
        var n = c.releases
        c.willResignActive()
        XCTAssertEqual(c.releases, n + 1); n += 1
        c.becameActive()
        XCTAssertEqual(c.releases, n)
        p.fire(.unsubscribed)
        XCTAssertEqual(c.releases, n + 1); n += 1
        p.fire(.subscribed)
        p.fire(.poweredOff)
        XCTAssertEqual(c.releases, n + 1)
    }
}
