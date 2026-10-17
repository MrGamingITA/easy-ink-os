import XCTest
import CalamaioKit
@testable import Calamaio

// Adattatore vero nel simulatore (senza Bluetooth): M3, il primo stato arriva sul thread principale; le chiamate
// della cucitura senza Bluetooth non fanno niente e non si bloccano.
@MainActor
final class CBPadPeripheralTests: XCTestCase {
    func testFirstStateOnMainThread() async {
        let p = CBPadPeripheral(appVersion: "0.1.0")
        let got = expectation(description: "primo stato")
        var events: [PadPeripheralEvent] = []
        p.onEvent = { e in
            XCTAssertTrue(Thread.isMainThread)
            events.append(e)
            if events.count == 1 { got.fulfill() }
        }
        await fulfillment(of: [got], timeout: 5)
        XCTAssertTrue([.unsupported, .poweredOff, .unauthorized, .poweredOn].contains(events[0]))
        p.setForeground(true)
        p.startAdvertising()
        XCTAssertTrue(p.send(PadReport(seq: 1, buttons: .a).encoded()))  // nessun servizio: niente da mandare
        p.stopAdvertising()
        p.setForeground(false)
    }
}
