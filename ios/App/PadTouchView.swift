import SwiftUI
import UIKit
import CalamaioKit

/// Superficie dei tocchi: più dita insieme (A+B, croce in diagonale), ogni dito seguito finché si stacca.
/// `releaseToken` che cambia = tutte le dita dimenticate (M1: nessun tasto bloccato).
struct PadTouchSurface: UIViewRepresentable {
    let layout: PadLayout
    let releaseToken: Int
    let onChange: (PadButtons) -> Void

    func makeUIView(context: Context) -> PadTouchView {
        let v = PadTouchView()
        v.isMultipleTouchEnabled = true
        v.backgroundColor = .clear
        return v
    }

    func updateUIView(_ v: PadTouchView, context: Context) {
        v.onChange = onChange
        v.layout = layout
        v.releaseToken = releaseToken
    }
}

final class PadTouchView: UIView {
    var onChange: (PadButtons) -> Void = { _ in }
    /// Cornici cambiate (rotazione): le dita di prima non valgono più.
    var layout = PadLayout(size: .zero) {
        didSet { if layout != oldValue { cancelAll() } }
    }
    var releaseToken = 0 {
        didSet { if releaseToken != oldValue { cancelAll() } }
    }
    private var tracker = TouchTracker()

    func track(_ touches: [(Int, CGPoint)]) {
        for (id, p) in touches { tracker.moved(id, to: p, in: layout) }
        onChange(tracker.buttons)
    }

    func end(_ ids: [Int]) {
        for id in ids { tracker.ended(id) }
        onChange(tracker.buttons)
    }

    func cancelAll() {
        tracker.cancelAll()
        onChange(tracker.buttons)
    }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        if window == nil { cancelAll() }  // scheda lasciata: niente touchesEnded in arrivo
    }

    private func id(_ t: UITouch) -> Int { ObjectIdentifier(t).hashValue }
    private func items(_ t: Set<UITouch>) -> [(Int, CGPoint)] { t.map { (id($0), $0.location(in: self)) } }
    override func touchesBegan(_ t: Set<UITouch>, with e: UIEvent?) { track(items(t)) }
    override func touchesMoved(_ t: Set<UITouch>, with e: UIEvent?) { track(items(t)) }
    override func touchesEnded(_ t: Set<UITouch>, with e: UIEvent?) { end(t.map(id)) }
    override func touchesCancelled(_ t: Set<UITouch>, with e: UIEvent?) {
        cancelAll()  // chiamata, Centro di Controllo: tutto rilasciato
    }
}
