import Combine  // ObservableObject e @Published (F12a: senza, compila solo per l'import di AppController.swift)
import Foundation

public enum PadPeripheralEvent: Equatable, Sendable {
    case poweredOn, poweredOff, unauthorized, unsupported
    case subscribed, unsubscribed, readyToSend
}

/// Cucitura verso CoreBluetooth (CBPeripheralManager nell'app). Il simulatore non ha Bluetooth: i test usano un finto.
/// Thread: l'adattatore crea il CBPeripheralManager con `queue: .main` e chiama `onEvent` sul main actor.
/// `stopAdvertising()` ferma solo l'annuncio (da iscritti il servizio resta). `setForeground(false)` (secondo piano)
/// ferma l'annuncio, toglie il servizio e chiude le iscrizioni (`.unsubscribed` se c'era qualcuno): la scheda non
/// deve ritrovare il telefono; `setForeground(true)` lo rimette, prima di `startAdvertising()`.
@MainActor public protocol PadPeripheral: AnyObject {
    var onEvent: ((PadPeripheralEvent) -> Void)? { get set }
    func setForeground(_ on: Bool)
    func startAdvertising()
    func stopAdvertising()
    /// false = coda di CoreBluetooth piena: si rimanda a .readyToSend lo stato di quel momento.
    func send(_ report: Data) -> Bool
}

/// Il telefono come controller (spec app iPhone §2): un report a ogni cambiamento e uno ogni 1 s (250 ms con un tasto tenuto), solo in primo
/// piano; prima del secondo piano «tutto rilasciato»; avviso dopo 10 s senza la scheda.
@MainActor public final class PadController: ObservableObject {
    public enum Status: Equatable, Sendable {
        case bluetoothOff, unauthorized, unsupported, searching, otherController, connected, paused
    }
    public static let keepalive: TimeInterval = 1
    /// Con un tasto tenuto: l'orologio di tenuta della scheda non corre oltre l'ultimo report (kFreshMs = 0).
    /// Tolleranza sul passo del timer (stesso valore della soglia): un tick a 245 ms manda comunque.
    static let tolerance: TimeInterval = 0.05
    public static let keepaliveHeld: TimeInterval = 0.25
    public static let hintAfter: TimeInterval = 10

    @Published public private(set) var status: Status = .bluetoothOff
    @Published public private(set) var buttons: PadButtons = []
    /// Cresce a ogni rilascio forzato (secondo piano, collegamento perso, Bluetooth giù): la vista dimentica le dita.
    @Published public private(set) var releases = 0
    public var haptics = true
    public var onHaptic: (() -> Void)?
    /// Ultimo report mandato: lo legge l'adattatore per le letture della caratteristica.
    public private(set) var lastReport = PadReport(seq: 0, buttons: []).encoded()

    private let peripheral: PadPeripheral
    private let now: () -> TimeInterval
    private var power: PadPeripheralEvent = .poweredOff
    private var active = false, subscribed = false, advertising = false, pending = false
    private var seq: UInt8 = 0
    private var lastSent: TimeInterval = -.infinity
    private var searchingSince: TimeInterval = 0

    public init(peripheral: PadPeripheral, now: @escaping () -> TimeInterval = { ProcessInfo.processInfo.systemUptime }) {
        self.peripheral = peripheral
        self.now = now
        peripheral.onEvent = { [weak self] e in self?.handle(e) }
    }

    /// Tasti premuti adesso (tutte le dita insieme). Vibrazione solo per un tasto nuovo.
    public func setButtons(_ b: PadButtons) {
        guard active, b != buttons else { return }
        let added = b.subtracting(buttons)
        buttons = b
        if haptics && !added.isEmpty { onHaptic?() }
        if subscribed { send(foreground: true) }
    }

    public func becameActive() {
        active = true
        peripheral.setForeground(true)
        updateAdvertising()
        if subscribed { send(foreground: true) }  // la scheda esce subito dal «secondo piano»
        refresh()
    }

    public func willResignActive() {
        buttons = []
        if subscribed { send(foreground: false) }  // «tutto rilasciato» prima che iOS fermi tutto
        active = false
        updateAdvertising()
        peripheral.setForeground(false)  // dopo il rilascio: niente servizio esposto in secondo piano
        releases += 1
        refresh()
    }

    /// Dal timer dell'app (ogni 0,25 s): mantenimento e avviso.
    public func tick() {
        let every = buttons.isEmpty ? Self.keepalive : Self.keepaliveHeld
        if active && subscribed && now() - lastSent >= every - Self.tolerance { send(foreground: true) }
        refresh()
    }

    private func handle(_ e: PadPeripheralEvent) {
        switch e {
        case .poweredOn:
            power = .poweredOn
        case .poweredOff, .unauthorized, .unsupported:
            power = e
            subscribed = false
            advertising = false
            pending = false
            buttons = []
            releases += 1
        case .subscribed:
            subscribed = true
            if active { send(foreground: true) }  // lo stato attuale subito
        case .unsubscribed:
            subscribed = false
            pending = false
            buttons = []
            releases += 1
        case .readyToSend:
            if pending && subscribed { send(foreground: active) }
        }
        updateAdvertising()
        refresh()
    }

    private func updateAdvertising() {
        let want = power == .poweredOn && active && !subscribed
        if want && !advertising {
            peripheral.startAdvertising()
            advertising = true
            searchingSince = now()
        } else if !want && advertising {
            peripheral.stopAdvertising()
            advertising = false
        }
    }

    private func send(foreground: Bool) {
        seq &+= 1
        let data = PadReport(seq: seq, buttons: foreground ? buttons : [], foreground: foreground).encoded()
        lastReport = data
        if peripheral.send(data) {
            pending = false
            lastSent = now()
        } else {
            pending = true  // a .readyToSend parte lo stato di allora, non questo
        }
    }

    private func refresh() {
        let s: Status
        switch power {
        case .unauthorized: s = .unauthorized
        case .unsupported: s = .unsupported
        case .poweredOn:
            if !active { s = .paused }
            else if subscribed { s = .connected }
            else if now() - searchingSince >= Self.hintAfter { s = .otherController }
            else { s = .searching }
        default: s = .bluetoothOff
        }
        if s != status { status = s }
    }
}
