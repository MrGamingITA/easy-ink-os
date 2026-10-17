import CoreGraphics

/// Pulsantiera (spec §2.4): verticale con croce a sinistra, A/B a destra, Select/Start sotto e Guida; orizzontale con
/// croce e A/B ai lati. Le stesse cornici servono al disegno e ai tocchi (PadTouchView).
public struct PadLayout: Equatable, Sendable {
    public enum Key: CaseIterable, Sendable { case dpad, a, b, select, start, guide }

    public let size: CGSize
    public let frames: [Key: CGRect]
    public var portrait: Bool { size.height >= size.width }

    public init(size: CGSize) {
        self.size = size
        let w = size.width, h = size.height, u = min(w, h)
        func round(_ x: CGFloat, _ y: CGFloat, _ r: CGFloat) -> CGRect { CGRect(x: x - r, y: y - r, width: 2 * r, height: 2 * r) }
        func pill(_ x: CGFloat, _ y: CGFloat, _ pw: CGFloat, _ ph: CGFloat) -> CGRect {
            CGRect(x: x - pw / 2, y: y - ph / 2, width: pw, height: ph)
        }
        if h >= w {
            // B lontana dalla croce quanto basta per i bersagli allargati (M6: prima si toccavano)
            frames = [.dpad: round(0.24 * w, 0.55 * h, 0.22 * u), .a: round(0.85 * w, 0.50 * h, 0.11 * u),
                      .b: round(0.67 * w, 0.66 * h, 0.11 * u), .select: pill(0.36 * w, 0.80 * h, 0.24 * u, 0.10 * u),
                      .start: pill(0.64 * w, 0.80 * h, 0.24 * u, 0.10 * u), .guide: pill(0.5 * w, 0.91 * h, 0.24 * u, 0.09 * u)]
        } else {
            frames = [.dpad: round(0.17 * w, 0.58 * h, 0.30 * u), .a: round(0.88 * w, 0.48 * h, 0.13 * u),
                      .b: round(0.72 * w, 0.66 * h, 0.13 * u), .select: pill(0.41 * w, 0.86 * h, 0.20 * u, 0.12 * u),
                      .start: pill(0.59 * w, 0.86 * h, 0.20 * u, 0.12 * u), .guide: pill(0.5 * w, 0.62 * h, 0.20 * u, 0.12 * u)]
        }
    }

    /// Raggio del bersaglio della croce: un po' più largo del disegno.
    public var dpadHitRadius: CGFloat { (frames[.dpad]?.width ?? 0) / 2 * 1.2 }

    /// Bersaglio di un tasto tondo o a pillola: la cornice allargata del 15% per lato.
    public func hitRect(_ k: Key) -> CGRect {
        let f = frames[k] ?? .zero
        return f.insetBy(dx: -f.width * 0.15, dy: -f.height * 0.15)
    }

    /// Tasti sotto un dito: al più un tasto, o due direzioni della croce in diagonale. Bersagli un po' più larghi
    /// del disegno, che non si toccano mai con quello della croce; fra due tasti vince il più vicino.
    public func buttons(at p: CGPoint) -> PadButtons {
        if let d = frames[.dpad] {
            let dx = p.x - d.midX, dy = p.y - d.midY, r = d.width / 2
            let dist = (dx * dx + dy * dy).squareRoot()
            if dist <= dpadHitRadius {
                if dist < r * 0.2 { return [] }  // centro morto
                let deg = atan2(-dy, dx) * 180 / .pi  // 0 = destra, 90 = su
                let sector = Int((deg + 360 + 22.5).truncatingRemainder(dividingBy: 360) / 45)
                let map: [PadButtons] = [.right, [.up, .right], .up, [.up, .left], .left, [.down, .left], .down, [.down, .right]]
                return map[sector]
            }
        }
        var best: (Key, CGFloat)?
        for k in [Key.a, .b, .select, .start, .guide] {
            guard let f = frames[k], hitRect(k).contains(p) else { continue }
            let d = hypot(p.x - f.midX, p.y - f.midY)
            if best == nil || d < best!.1 { best = (k, d) }
        }
        switch best?.0 {
        case .a: return .a
        case .b: return .b
        case .select: return .view
        case .start: return .menu
        case .guide: return .guide
        default: return []
        }
    }
}

/// Dita sullo schermo → tasti premuti (OR di tutte le dita). Un dito che scivola cambia tasto.
public struct TouchTracker: Sendable {
    private var touches: [Int: PadButtons] = [:]
    public init() {}
    public var buttons: PadButtons { touches.values.reduce(into: []) { $0.formUnion($1) } }
    public mutating func moved(_ id: Int, to p: CGPoint, in layout: PadLayout) { touches[id] = layout.buttons(at: p) }
    public mutating func ended(_ id: Int) { touches[id] = nil }
    public mutating func cancelAll() { touches.removeAll() }
}
