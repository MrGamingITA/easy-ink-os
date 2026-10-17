import Foundation

/// Tasti del report v1: stessi bit di pad::State del firmware (firmware/src/core/pad.h).
public struct PadButtons: OptionSet, Hashable, Sendable {
    public let rawValue: UInt16
    public init(rawValue: UInt16) { self.rawValue = rawValue }
    public static let a = PadButtons(rawValue: 1 << 0)
    public static let b = PadButtons(rawValue: 1 << 1)
    public static let view = PadButtons(rawValue: 1 << 6)   // tasto SELECT dell'app
    public static let menu = PadButtons(rawValue: 1 << 7)   // tasto START: apre il Menu della scheda
    public static let guide = PadButtons(rawValue: 1 << 8)  // tasto GUIDA: pausa del Game Boy
    public static let up = PadButtons(rawValue: 1 << 12)
    public static let down = PadButtons(rawValue: 1 << 13)
    public static let left = PadButtons(rawValue: 1 << 14)
    public static let right = PadButtons(rawValue: 1 << 15)
    public static let dpad: PadButtons = [.up, .down, .left, .right]
}

/// Report v1 (spec app iPhone §2.1): ver, seq, buttons u16 LE, lx, ly, flags (bit0 primo piano), rsv.
public struct PadReport: Equatable, Sendable {
    public static let version: UInt8 = 1
    public static let length = 8

    public var seq: UInt8
    public var buttons: PadButtons
    public var lx: Int8
    public var ly: Int8
    public var foreground: Bool

    public init(seq: UInt8, buttons: PadButtons, lx: Int8 = 0, ly: Int8 = 0, foreground: Bool = true) {
        self.seq = seq
        self.buttons = buttons
        self.lx = lx
        self.ly = ly
        self.foreground = foreground
    }

    public func encoded() -> Data {
        Data([Self.version, seq, UInt8(buttons.rawValue & 0xff), UInt8(buttons.rawValue >> 8),
              UInt8(bitPattern: max(-100, min(100, lx))), UInt8(bitPattern: max(-100, min(100, ly))), foreground ? 1 : 0, 0])
    }
}

/// Servizio «Calamaio pad» (spec §2.1). Stringhe: CoreBluetooth sta solo nell'app (CBUUID(string:)).
public enum PadGATT {
    public static let service = "526389E4-B789-4225-990D-0465AFB610B2"
    public static let state = "6A34D990-D046-4834-96B8-0CBDCE4087FA"
    public static let info = "9D00F77E-BB83-4667-A37F-6FBDB0F0F324"
    public static let localName = "Calamaio"

    /// ver = 1, poi la versione dell'app in UTF-8, al massimo 15 byte (si tronca su un carattere intero).
    public static func infoValue(appVersion: String) -> Data {
        var v = Data()
        for ch in appVersion {
            let u = Data(String(ch).utf8)
            if v.count + u.count > 15 { break }
            v.append(u)
        }
        return Data([1]) + v
    }
}
