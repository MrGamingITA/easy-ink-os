import XCTest
import UIKit
import CalamaioKit
@testable import Calamaio

// M1: nessun tasto resta premuto. Le dita si dimenticano con touchesCancelled, al cambio del segnale di rilascio
// (secondo piano, collegamento perso, scheda lasciata) e quando la vista esce dalla finestra.
@MainActor
final class PadTouchViewTests: XCTestCase {
    private var view: PadTouchView!
    private var seen: [PadButtons] = []

    override func setUp() async throws {
        seen = []
        view = PadTouchView(frame: CGRect(x: 0, y: 0, width: 393, height: 700))
        view.layout = PadLayout(size: view.bounds.size)
        view.onChange = { [unowned self] in self.seen.append($0) }
    }

    private func center(_ k: PadLayout.Key) -> CGPoint {
        let f = view.layout.frames[k]!
        return CGPoint(x: f.midX, y: f.midY)
    }

    func testTwoFingersThenEnd() {
        view.track([(1, center(.a)), (2, center(.b))])
        XCTAssertEqual(seen.last, [.a, .b])
        view.end([1])
        XCTAssertEqual(seen.last, .b)
    }

    func testTouchesCancelledReleasesAll() {
        view.track([(1, center(.a))])
        view.touchesCancelled([], with: nil)
        XCTAssertEqual(seen.last, [])
        view.track([(2, center(.b))])  // il dito di A non torna
        XCTAssertEqual(seen.last, .b)
    }

    func testReleaseTokenForgetsFingers() {
        view.track([(1, center(.a))])
        view.releaseToken = 0  // stesso valore: niente
        XCTAssertEqual(seen.last, .a)
        view.releaseToken = 1
        XCTAssertEqual(seen.last, [])
        view.track([(2, center(.start))])
        XCTAssertEqual(seen.last, .menu)  // A non riappare con un altro dito
    }

    func testLeavingWindowReleasesAll() {
        let window = UIWindow(frame: view.frame)
        window.addSubview(view)
        view.track([(1, center(.a))])
        view.removeFromSuperview()
        XCTAssertEqual(seen.last, [])
        window.addSubview(view)
        view.track([(2, center(.b))])
        XCTAssertEqual(seen.last, .b)
    }

    func testLayoutChangeReleasesAll() {
        view.track([(1, center(.a))])
        view.layout = PadLayout(size: view.bounds.size)  // uguale: niente
        XCTAssertEqual(seen.last, .a)
        view.layout = PadLayout(size: CGSize(width: 852, height: 360))  // rotazione
        XCTAssertEqual(seen.last, [])
    }
}
