import XCTest
@testable import CalamaioKit

final class WebScriptsTests: XCTestCase {
    func testUserAgentAndScripts() {
        XCTAssertEqual(WebScripts.userAgentName(version: "0.1.0"), "CalamaioApp/0.1.0") // spec §5: la webapp riconosce l'app
        XCTAssertEqual(WebScripts.sources(sseStreams: true), [])
        XCTAssertEqual(WebScripts.sources(sseStreams: false), ["delete window.EventSource;"])
        for s in WebScripts.sources(sseStreams: false) { XCTAssertFalse(s.lowercased().contains("bearer")) }
    }
}
