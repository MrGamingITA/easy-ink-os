import XCTest

// Scheda Controller nel simulatore (niente Bluetooth): i tasti ci sono e lo stato dice perché non si collega.
final class ControllerTabUITests: XCTestCase {
    func testControllerTabShowsPadAndStatus() {
        let app = XCUIApplication()
        app.launchArguments = ["--reset-for-tests"]
        app.launch()
        app.tabBars.buttons["Controller"].tap()
        for id in ["pad-a", "pad-b", "pad-select", "pad-start", "pad-guide", "pad-dpad"] {
            XCTAssertTrue(app.descendants(matching: .any)[id].waitForExistence(timeout: 5), id)
        }
        // M6: sullo schermo vero croce e B non si toccano
        let dpad = app.descendants(matching: .any)["pad-dpad"].frame, b = app.descendants(matching: .any)["pad-b"].frame
        XCTAssertFalse(dpad.intersects(b), "croce \(dpad) e B \(b) si toccano")
        let status = app.staticTexts["pad-status"]
        XCTAssertTrue(status.waitForExistence(timeout: 5))
        let known = ["Bluetooth non disponibile su questo dispositivo.", "Bluetooth spento: accendilo dal Centro di Controllo.",
                     "Bluetooth non permesso: consentilo in Impostazioni › Calamaio.", "Cerco la scheda…"]
        XCTAssertTrue(known.contains(status.label), "stato inatteso: \(status.label)")
        app.tabBars.buttons["Impostazioni"].tap()
        XCTAssertTrue(app.switches["Vibrazione al tocco"].waitForExistence(timeout: 5))
    }
}
