import XCTest

// Abbinamento col link incollato e webapp caricata, col servizio vero:
//   ios/scripts/e2e.sh xcodebuild test -project ios/Calamaio.xcodeproj -scheme Calamaio -destination '…' -only-testing:CalamaioUITests
final class PairAndLoadUITests: XCTestCase {
    func testPairWithLinkAndSeeTheWebapp() throws {
        guard let link = ProcessInfo.processInfo.environment["CALAMAIO_E2E_LINK"] else {
            throw XCTSkip("prova col servizio vero: ios/scripts/e2e.sh xcodebuild test …")
        }
        let app = XCUIApplication()
        app.launchArguments = ["--reset-for-tests"]
        app.launch()
        app.buttons["open-pair"].tap()
        let field = app.textFields["pair-link"]
        XCTAssertTrue(field.waitForExistence(timeout: 5))
        field.tap()
        field.typeText(link)
        app.buttons["pair-go"].tap()

        let web = app.webViews["webapp"]
        XCTAssertTrue(web.waitForExistence(timeout: 20), "webapp assente: " + app.staticTexts["pair-error"].label)
        XCTAssertTrue(web.links["Panoramica"].waitForExistence(timeout: 20), "la Panoramica della webapp non si vede")
        web.links["Dispositivo"].tap()
        // un testo che c'è solo in device.html, non il link «Dispositivo» della barra (decisione P9)
        let lead = web.staticTexts.matching(NSPredicate(format: "label BEGINSWITH %@", "Le stesse voci dei menu Display")).firstMatch
        XCTAssertTrue(lead.waitForExistence(timeout: 10), "pagina Dispositivo non caricata")
        sleep(2) // GET /api/phones ha avuto il tempo di rispondere (404 dalla porta dei telefoni)
        XCTAssertFalse(web.staticTexts["Telefoni"].exists, "dall'app la sezione Telefoni non deve esserci")

        app.tabBars.buttons["Impostazioni"].tap()
        // la riga «Stato» è un elemento solo: se ne controlla il valore (decisione P8)
        let state = app.descendants(matching: .any)["settings-state"]
        XCTAssertTrue(state.waitForExistence(timeout: 5), "riga Stato assente")
        let connected = expectation(for: NSPredicate(format: "value == %@", "Collegato"), evaluatedWith: state)
        wait(for: [connected], timeout: 5)
    }
}
