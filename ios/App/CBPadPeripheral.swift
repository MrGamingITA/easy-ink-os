import CoreBluetooth
import CalamaioKit

/// Adattatore CoreBluetooth del controller: periferica col servizio «Calamaio pad» (spec §2.1). Le due
/// caratteristiche si leggono solo cifrate; le letture rispondono con l'ultimo report e con le informazioni.
/// CoreBluetooth non dice se chi si iscrive ha cifrato: la scheda cifra sempre prima di iscriversi.
/// Thread (M3): gestore sulla coda principale, eventi sul main actor. Servizio solo in primo piano (M2).
@MainActor
final class CBPadPeripheral: NSObject, PadPeripheral, CBPeripheralManagerDelegate {
    /// Attesa prima di togliere il servizio con qualcuno iscritto: il «tutto rilasciato» appena in coda parte prima.
    static let withdrawDelay: Duration = .milliseconds(300)

    var onEvent: ((PadPeripheralEvent) -> Void)?
    var currentReport: () -> Data = { Data() }

    private let info: Data
    private var manager: CBPeripheralManager!
    private var state: CBMutableCharacteristic?
    private var added = false, adding = false, wantAdvertising = false, foreground = false
    private var centrals = Set<UUID>()
    private var withdrawal: Task<Void, Never>?

    init(appVersion: String) {
        info = PadGATT.infoValue(appVersion: appVersion)
        super.init()
        manager = CBPeripheralManager(delegate: self, queue: .main)
    }

    func setForeground(_ on: Bool) {
        foreground = on
        withdrawal?.cancel()
        withdrawal = nil
        if on {
            if manager.state == .poweredOn { addService() }
            return
        }
        wantAdvertising = false
        if manager.isAdvertising { manager.stopAdvertising() }
        if centrals.isEmpty {
            removeService()
        } else {
            withdrawal = Task { @MainActor [weak self] in
                try? await Task.sleep(for: Self.withdrawDelay)
                guard let self, !Task.isCancelled, !self.foreground else { return }
                self.removeService()
            }
        }
    }

    func startAdvertising() {
        wantAdvertising = true
        if added {
            if !manager.isAdvertising {
                manager.startAdvertising([CBAdvertisementDataServiceUUIDsKey: [CBUUID(string: PadGATT.service)],
                                          CBAdvertisementDataLocalNameKey: PadGATT.localName])
            }
        } else if foreground && manager.state == .poweredOn {
            addService()  // all'aggiunta parte l'annuncio
        }
    }

    func stopAdvertising() {
        wantAdvertising = false
        if manager.isAdvertising { manager.stopAdvertising() }
    }

    func send(_ report: Data) -> Bool {
        guard let state else { return true }  // servizio non pronto: niente da mandare
        return manager.updateValue(report, for: state, onSubscribedCentrals: nil)
    }

    /// Via il servizio: le iscrizioni si chiudono e il controller lo sa da `.unsubscribed`.
    private func removeService() {
        manager.removeAllServices()
        added = false
        adding = false
        state = nil
        let had = !centrals.isEmpty
        centrals.removeAll()
        if had { onEvent?(.unsubscribed) }
    }

    private func addService() {
        guard foreground, !added, !adding else { return }
        let st = CBMutableCharacteristic(type: CBUUID(string: PadGATT.state), properties: [.notify, .read],
                                         value: nil, permissions: [.readEncryptionRequired])
        let inf = CBMutableCharacteristic(type: CBUUID(string: PadGATT.info), properties: [.read],
                                          value: nil, permissions: [.readEncryptionRequired])
        let svc = CBMutableService(type: CBUUID(string: PadGATT.service), primary: true)
        svc.characteristics = [st, inf]
        state = st
        adding = true
        manager.removeAllServices()
        manager.add(svc)
    }

    nonisolated func peripheralManagerDidUpdateState(_ p: CBPeripheralManager) {
        MainActor.assumeIsolated {
            switch p.state {
            case .poweredOn:
                addService()
                onEvent?(.poweredOn)
            default:
                withdrawal?.cancel()
                withdrawal = nil
                added = false
                adding = false
                state = nil
                centrals.removeAll()
                switch p.state {
                case .unauthorized: onEvent?(.unauthorized)
                case .unsupported: onEvent?(.unsupported)
                default: onEvent?(.poweredOff)
                }
            }
        }
    }

    nonisolated func peripheralManager(_ p: CBPeripheralManager, didAdd service: CBService, error: Error?) {
        MainActor.assumeIsolated {
            guard adding else { return }  // tolto nel frattempo
            adding = false
            guard error == nil else { state = nil; return }
            guard foreground else { removeService(); return }  // secondo piano arrivato durante l'aggiunta
            added = true
            if wantAdvertising { startAdvertising() }
        }
    }

    nonisolated func peripheralManager(_ p: CBPeripheralManager, didReceiveRead request: CBATTRequest) {
        MainActor.assumeIsolated {
            let value: Data
            switch request.characteristic.uuid {
            case CBUUID(string: PadGATT.state): value = currentReport()
            case CBUUID(string: PadGATT.info): value = info
            default: p.respond(to: request, withResult: .attributeNotFound); return
            }
            guard request.offset <= value.count else { p.respond(to: request, withResult: .invalidOffset); return }
            request.value = value.subdata(in: request.offset..<value.count)
            p.respond(to: request, withResult: .success)
        }
    }

    nonisolated func peripheralManager(_ p: CBPeripheralManager, central: CBCentral, didSubscribeTo c: CBCharacteristic) {
        MainActor.assumeIsolated {
            guard added else { return }  // servizio già ritirato
            centrals.insert(central.identifier)
            onEvent?(.subscribed)
        }
    }

    nonisolated func peripheralManager(_ p: CBPeripheralManager, central: CBCentral, didUnsubscribeFrom c: CBCharacteristic) {
        MainActor.assumeIsolated {
            // già tolti con il servizio: niente secondo «.unsubscribed»
            if centrals.remove(central.identifier) != nil && centrals.isEmpty { onEvent?(.unsubscribed) }
        }
    }

    nonisolated func peripheralManagerIsReady(toUpdateSubscribers p: CBPeripheralManager) {
        MainActor.assumeIsolated { onEvent?(.readyToSend) }
    }
}
