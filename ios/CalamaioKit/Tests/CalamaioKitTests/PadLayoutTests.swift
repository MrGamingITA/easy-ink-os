import CoreGraphics
import XCTest
@testable import CalamaioKit

final class PadLayoutTests: XCTestCase {
    private let portrait = PadLayout(size: CGSize(width: 393, height: 700))
    private let landscape = PadLayout(size: CGSize(width: 852, height: 360))

    private func center(_ l: PadLayout, _ k: PadLayout.Key) -> CGPoint {
        let f = l.frames[k]!
        return CGPoint(x: f.midX, y: f.midY)
    }

    func testFramesInsideAndApart() {
        for l in [portrait, landscape] {
            let bounds = CGRect(origin: .zero, size: l.size)
            XCTAssertEqual(l.frames.count, PadLayout.Key.allCases.count)
            for (k, f) in l.frames { XCTAssertTrue(bounds.contains(f), "\(k) fuori schermo") }
            let keys = PadLayout.Key.allCases
            for i in keys.indices { for j in keys.indices where j > i {
                XCTAssertFalse(l.frames[keys[i]]!.intersects(l.frames[keys[j]]!), "\(keys[i]) tocca \(keys[j])")
            } }
        }
        XCTAssertTrue(portrait.portrait)
        XCTAssertFalse(landscape.portrait)
    }

    func testKeysMapToFirmwareBits() {
        XCTAssertEqual(portrait.buttons(at: center(portrait, .a)), .a)
        XCTAssertEqual(portrait.buttons(at: center(portrait, .b)), .b)
        XCTAssertEqual(portrait.buttons(at: center(portrait, .select)), .view)
        XCTAssertEqual(portrait.buttons(at: center(portrait, .start)), .menu)
        XCTAssertEqual(portrait.buttons(at: center(portrait, .guide)), .guide)
        XCTAssertEqual(portrait.buttons(at: CGPoint(x: 5, y: 5)), [])  // zona dello stato
    }

    func testDpadSectorsAndDiagonals() {
        for l in [portrait, landscape] {
            let f = l.frames[.dpad]!
            let c = CGPoint(x: f.midX, y: f.midY), r = f.width / 2
            func at(_ dx: CGFloat, _ dy: CGFloat) -> PadButtons { l.buttons(at: CGPoint(x: c.x + dx * r, y: c.y + dy * r)) }
            XCTAssertEqual(at(0.8, 0), .right)
            XCTAssertEqual(at(-0.8, 0), .left)
            XCTAssertEqual(at(0, -0.8), .up)    // y dello schermo verso il basso
            XCTAssertEqual(at(0, 0.8), .down)
            XCTAssertEqual(at(-0.6, -0.6), [.up, .left])
            XCTAssertEqual(at(0.6, 0.6), [.down, .right])
            XCTAssertEqual(at(0.05, 0.05), [])  // centro morto
        }
    }

    func testTrackerTwoFingers() {
        var tr = TouchTracker()
        tr.moved(1, to: center(portrait, .a), in: portrait)
        tr.moved(2, to: center(portrait, .b), in: portrait)
        XCTAssertEqual(tr.buttons, [.a, .b])
        let f = portrait.frames[.dpad]!
        tr.moved(3, to: CGPoint(x: f.midX - f.width * 0.3, y: f.midY - f.width * 0.3), in: portrait)
        XCTAssertEqual(tr.buttons, [.a, .b, .up, .left])
        tr.ended(1)
        XCTAssertEqual(tr.buttons, [.b, .up, .left])
        tr.cancelAll()
        XCTAssertEqual(tr.buttons, [])
    }

    func testTrackerSlideFromAToB() {
        var tr = TouchTracker()
        tr.moved(7, to: center(portrait, .a), in: portrait)
        tr.moved(7, to: center(portrait, .b), in: portrait)
        XCTAssertEqual(tr.buttons, .b)
    }

    // M6: schermi dei simulatori (iPhone 17e, 17, Air, 18 Pro, 18 Pro Max), in verticale e in orizzontale, con
    // margini di barra di stato, isola, barra delle schede e indicatore Home da 0 a 240 punti.
    static let phones: [CGSize] = [CGSize(width: 390, height: 844), CGSize(width: 402, height: 874),
                                   CGSize(width: 420, height: 912), CGSize(width: 440, height: 956)]
    static var sizes: [CGSize] {
        var out: [CGSize] = []
        for p in phones {
            for inset in stride(from: 0, through: 240, by: 20) { out.append(CGSize(width: p.width, height: p.height - CGFloat(inset))) }
            for dx in [0, 60, 120] as [CGFloat] { for dy in [0, 21, 50, 90, 120] as [CGFloat] {
                out.append(CGSize(width: p.height - dx, height: p.width - dy))
            } }
        }
        return out
    }

    func testFramesInsideAndApartOnEveryPhone() {
        for size in Self.sizes {
            let l = PadLayout(size: size), bounds = CGRect(origin: .zero, size: size)
            for (k, f) in l.frames { XCTAssertTrue(bounds.contains(f), "\(k) fuori schermo a \(size)") }
            let keys = PadLayout.Key.allCases
            for i in keys.indices { for j in keys.indices where j > i {
                XCTAssertFalse(l.frames[keys[i]]!.intersects(l.frames[keys[j]]!), "\(keys[i]) tocca \(keys[j]) a \(size)")
            } }
        }
    }

    // Il bersaglio della croce (cerchio allargato) non tocca mai quello di un altro tasto, B compreso.
    func testDpadHitTargetNeverOverlapsOtherKeys() {
        for size in Self.sizes {
            let l = PadLayout(size: size), d = l.frames[.dpad]!
            let c = CGPoint(x: d.midX, y: d.midY)
            XCTAssertGreaterThanOrEqual(l.dpadHitRadius, d.width / 2)
            for k in PadLayout.Key.allCases where k != .dpad {
                let r = l.hitRect(k)
                XCTAssertTrue(r.contains(l.frames[k]!))
                let dx = max(r.minX - c.x, 0, c.x - r.maxX), dy = max(r.minY - c.y, 0, c.y - r.maxY)
                XCTAssertGreaterThan(hypot(dx, dy), l.dpadHitRadius, "croce e \(k) si toccano a \(size)")
            }
        }
    }

    // Lo stesso con i tocchi: un punto vicino al bordo di B non diventa mai una direzione della croce.
    func testPointsOnBEdgeAreB() {
        for size in Self.sizes {
            let l = PadLayout(size: size), b = l.frames[.b]!
            for t in stride(from: 0.0, to: 2 * Double.pi, by: Double.pi / 16) {
                let p = CGPoint(x: b.midX + cos(t) * b.width / 2, y: b.midY + sin(t) * b.height / 2)
                XCTAssertEqual(l.buttons(at: p), .b, "bordo di B a \(size)")
            }
        }
    }
}
