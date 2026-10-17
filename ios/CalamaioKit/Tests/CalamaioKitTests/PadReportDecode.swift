import Foundation
@testable import CalamaioKit

// D1: la decodifica del report serve solo alle prove (vettori, PadController): l'app non la usa, resta fuori dal
// pacchetto. Come la scheda (core/pad_phone): nil con lunghezza o versione sbagliata, levette a ±100, secondo
// piano = vuoto.
extension PadReport {
    static func decode(_ d: Data) -> PadReport? {
        guard d.count == length else { return nil }
        let b = [UInt8](d)
        guard b[0] == version else { return nil }
        let fg = b[6] & 1 == 1
        func clamp(_ v: UInt8) -> Int8 { Int8(max(-100, min(100, Int(Int8(bitPattern: v))))) }
        return PadReport(seq: b[1], buttons: fg ? PadButtons(rawValue: UInt16(b[2]) | UInt16(b[3]) << 8) : [],
                         lx: fg ? clamp(b[4]) : 0, ly: fg ? clamp(b[5]) : 0, foreground: fg)
    }
}
