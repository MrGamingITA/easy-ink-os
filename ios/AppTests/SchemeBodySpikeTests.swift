import XCTest
import WebKit
import CalamaioKit

// Prova del Task 7: misura, non giudica. Le ultime due asserzioni confrontano la misura con SpikeOutcome.
@MainActor
final class SchemeBodySpikeTests: XCTestCase {
    func testWhatTheSchemeHandlerReceives() async throws {
        let handler = SpikeHandler()
        let sink = SpikeSink()
        let cfg = WKWebViewConfiguration()
        cfg.setURLSchemeHandler(handler, forURLScheme: "spike")
        cfg.userContentController.add(sink, name: "spike")
        cfg.websiteDataStore = .nonPersistent()
        // la WKWebView sta in una finestra visibile: fuori schermo i timer della pagina rallentano
        let scene = try XCTUnwrap(UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.first)
        let window = UIWindow(windowScene: scene)
        window.frame = CGRect(x: 0, y: 0, width: 393, height: 852)
        let vc = UIViewController()
        let wv = WKWebView(frame: window.bounds, configuration: cfg)
        vc.view.addSubview(wv)
        window.rootViewController = vc
        window.makeKeyAndVisible()
        defer { window.isHidden = true }

        let exp = expectation(description: "pagina di prova")
        sink.done = { exp.fulfill() }
        wv.load(URLRequest(url: URL(string: "spike://t/harness.html")!))
        await fulfillment(of: [exp], timeout: 60)

        let raw = try XCTUnwrap(sink.result)
        print("SPIKE-RESULT " + raw)
        let att = XCTAttachment(string: raw)
        att.name = "spike.json"
        att.lifetime = .keepAlways
        add(att)
        let r = try XCTUnwrap(try JSONSerialization.jsonObject(with: Data(raw.utf8)) as? [String: Any])
        XCTAssertNil(r["fatal"], "la pagina di prova si è fermata")

        let big = Data((0..<(2 << 20)).map { UInt8($0 & 255) })
        let json = Data(#"{"a":1,"s":"è"}"#.utf8)
        func full(_ k: String, _ want: Data) -> Bool {
            guard let e = r[k] as? [String: Any] else { return false }
            let n = max(e["inline"] as? Int ?? -1, e["stream"] as? Int ?? -1)
            return n == want.count && (e["sha"] as? String) == SpikeHandler.hex(want)
        }
        let form = (r["form"] as? [String: Any]) ?? [:]
        let formOK = max(form["inline"] as? Int ?? -1, form["stream"] as? Int ?? -1) > 32768 && (form["multipart"] as? Bool ?? false)
        let bodies = full("json", json) && full("buffer", big) && full("blob", big) && formOK
        let ticks = ((r["sse"] as? [String: Any])?["got"] as? [[Int]]) ?? []
        let streams = ticks.count == 3 && ticks[0][1] < 800 && ticks[2][1] < 2800
        let csp = (r["csp"] as? [String]) ?? []
        print("SPIKE-SUMMARY corpi=\(bodies) (json \(full("json", json)) buffer \(full("buffer", big)) blob \(full("blob", big)) form \(formOK)) sse=\(streams) \(ticks) csp=\(csp)")

        XCTAssertEqual(bodies, SpikeOutcome.bodiesReachHandler, "i corpi nello schema proprio non sono come nella prova: rivedere il ponte (Task 10)")
        XCTAssertEqual(streams, SpikeOutcome.sseStreams, "l'SSE nello schema proprio non è come nella prova: rivedere EventSource (Task 11)")
    }
}
