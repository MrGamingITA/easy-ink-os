import SwiftUI
import CalamaioKit

struct RootView: View {
    @EnvironmentObject var model: AppController
    @EnvironmentObject var pad: PadController
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        TabView {
            Tab("Scheda", systemImage: "rectangle.on.rectangle") { WebTab() }
            Tab(Texts.padTab, systemImage: "gamecontroller") { ControllerTab() }
            Tab("Impostazioni", systemImage: "gearshape") { SettingsTab() }
        }
        .onChange(of: scenePhase) { _, phase in
            // di ritorno in primo piano si ricontrolla il PC (rete cambiata, servizio riavviato, telefono revocato),
            // anche da «ready» (I1): se resta «ready» la vista web non si rifà
            if phase == .active {
                model.becameActive()
                pad.becameActive()
            } else {
                pad.willResignActive()  // solo in primo piano (spec §2.1): prima «tutto rilasciato»
            }
        }
        .onAppear { if scenePhase == .active { pad.becameActive() } }  // al primo avvio onChange non scatta
    }
}
