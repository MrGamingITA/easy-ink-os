import Combine
import SwiftUI
import UIKit  // vibrazione del controller
import CalamaioKit

@main
struct CalamaioApp: App {
    @StateObject private var model = CalamaioApp.makeModel()
    @StateObject private var pad = CalamaioApp.makePad()
    /// Passo del mantenimento del controller (PadController.tick).
    private static let padTimer = Timer.publish(every: 0.25, on: .main, in: .common).autoconnect()

    // «--reset-for-tests» (prove UI): si parte senza PC abbinato.
    @MainActor static func makeModel() -> AppController {
        DownloadFolder().purge() // file scaricati rimasti da un avvio precedente
        let store = PairingStore(defaults: .standard, secrets: KeychainStore(service: "com.mrgamingita.calamaio.pc"))
        if ProcessInfo.processInfo.arguments.contains("--reset-for-tests") { store.forget() }
        return AppController(store: store, deps: .live(deviceName: { UIDevice.current.name }))
    }

    @MainActor static func makePad() -> PadController {
        if ProcessInfo.processInfo.arguments.contains("--reset-for-tests") {
            UserDefaults.standard.removeObject(forKey: "padHaptics")
        }
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0.0.0"
        let ble = CBPadPeripheral(appVersion: version)
        let pad = PadController(peripheral: ble)
        ble.currentReport = { [unowned pad] in pad.lastReport }
        pad.haptics = UserDefaults.standard.object(forKey: "padHaptics") as? Bool ?? true
        let haptic = UIImpactFeedbackGenerator(style: .light)
        haptic.prepare()
        pad.onHaptic = { haptic.impactOccurred() }
        return pad
    }

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(model)
                .environmentObject(pad)
                .onReceive(Self.padTimer) { _ in pad.tick() }
                .task { if model.pc != nil { await model.connect() } }
        }
    }
}
