import XCTest
@testable import CalamaioKit

final class NavigationPolicyTests: XCTestCase {
    func testNavigationPolicy() {
        let u = { (s: String) in URL(string: s) }
        XCTAssertEqual(NavigationPolicy.decide(url: u("calamaio-app://ui/device.html"), shouldDownload: false), .allow)
        XCTAssertEqual(NavigationPolicy.decide(url: u("about:blank"), shouldDownload: false), .allow)
        XCTAssertEqual(NavigationPolicy.decide(url: u("blob:calamaio-app://ui/1234"), shouldDownload: true), .download)
        XCTAssertEqual(NavigationPolicy.decide(url: u("calamaio-app://ui/api/themes/t1/export"), shouldDownload: true), .download)
        // mai il PC direttamente (né col suo certificato né senza token), mai siti esterni
        for s in ["https://192.168.1.20:8767/", "http://127.0.0.1:8765/", "https://example.com/", "calamaio-app://evil/",
                  "data:text/html,ciao", "javascript:alert(1)", "file:///etc/hosts"] {
            XCTAssertEqual(NavigationPolicy.decide(url: u(s), shouldDownload: false), .cancel, s)
        }
        XCTAssertEqual(NavigationPolicy.decide(url: nil, shouldDownload: false), .cancel)
        // M2: esattamente calamaio-app://ui, senza utente, password né porta (come Proxy.upstream)
        for s in ["calamaio-app://user@ui/", "calamaio-app://u:p@ui/", "calamaio-app://ui:99/", "calamaio-app://UI/",
                  "calamaio-app:ui/x", "calamaio-app:///x"] {
            XCTAssertEqual(NavigationPolicy.decide(url: u(s), shouldDownload: false), .cancel, s)
            XCTAssertEqual(NavigationPolicy.decide(url: u(s), shouldDownload: true), .cancel, s)
        }
    }

    // Link esterni (decisione del coordinatore, M5): solo https:// toccato dall'utente va in Safari; mai il PC.
    func testOpensInSafari() {
        let u = { (s: String) in URL(string: s) }
        let hosts: Set<String> = ["192.168.1.20", "pc.local"]
        let ext = { (s: String, tap: Bool) in NavigationPolicy.opensInSafari(url: u(s), linkActivated: tap, pcHosts: hosts, pcPort: 8767) }
        XCTAssertTrue(ext("https://example.com/guida", true))
        XCTAssertTrue(ext("HTTPS://Example.com/", true))
        XCTAssertFalse(ext("https://example.com/", false)) // redirect o script, non un tocco
        for s in ["http://example.com/", "https://192.168.1.20/", "https://PC.local:443/", "https://example.com:8767/",
                  "calamaio-app://ui/", "mailto:a@b.c", "tel:123", "javascript:alert(1)", "data:text/html,x", "blob:x",
                  "file:///etc/hosts", "about:srcdoc", "https:///nohost", "https://user:pw@example.com/"] {
            XCTAssertFalse(ext(s, true), s)
        }
        XCTAssertFalse(NavigationPolicy.opensInSafari(url: nil, linkActivated: true, pcHosts: [], pcPort: nil))
    }

    func testResponsePolicy() {
        XCTAssertEqual(NavigationPolicy.decideResponse(disposition: #"attachment; filename="t1.theme.json""#, canShow: true), .download)
        XCTAssertEqual(NavigationPolicy.decideResponse(disposition: nil, canShow: true), .allow)
        XCTAssertEqual(NavigationPolicy.decideResponse(disposition: nil, canShow: false), .download)
    }

    func testDownloadName() {
        XCTAssertEqual(DownloadName.safe("t1.theme.json"), "t1.theme.json")
        XCTAssertEqual(DownloadName.safe("../../etc/passwd"), "passwd")
        XCTAssertEqual(DownloadName.safe(""), "download")
        XCTAssertEqual(DownloadName.safe(".."), "download")
        XCTAssertEqual(DownloadName.safe("..."), "download")
        XCTAssertEqual(DownloadName.safe(".hidden"), "hidden")
        XCTAssertEqual(DownloadName.safe("..\\..\\x.gb"), "x.gb")
        XCTAssertEqual(DownloadName.safe("a\u{0}b\nc\u{7f}d\u{1b}.json"), "abcd.json")
        XCTAssertEqual(DownloadName.safe("\n\t"), "download")
        XCTAssertEqual(DownloadName.safe(String(repeating: "è", count: 300)).count, 120)
    }
}
