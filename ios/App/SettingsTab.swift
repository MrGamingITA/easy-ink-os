import SwiftUI
import CalamaioKit

struct SettingsTab: View {
    @EnvironmentObject var model: AppController
    @EnvironmentObject var pad: PadController
    @AppStorage("padHaptics") private var haptics = true
    @State private var pairing = false
    @State private var confirmForget = false

    private var version: String {
        let v = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?"
        let b = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "?"
        return "\(v) (\(b))"
    }

    private var address: String {
        if case let .ready(ep) = model.phase { return "\(ep.host):\(ep.port)" }
        return model.pc?.lastHost.map { "\($0) (l'ultimo)" } ?? "—"
    }

    private var stateText: String {
        switch model.phase {
        case .ready: return "Collegato"
        case .connecting: return Texts.searching
        case let .failed(msg): return msg
        case .unpaired: return "Non abbinato"
        }
    }

    // «abcd ef01 …»: le prime 16 cifre dell'impronta, come nel TXT Bonjour e nella webapp
    static func grouped(_ s: Substring) -> String {
        stride(from: 0, to: s.count, by: 4).map { i in
            let a = s.index(s.startIndex, offsetBy: i)
            return String(s[a..<s.index(a, offsetBy: min(4, s.count - i))])
        }.joined(separator: " ")
    }

    var body: some View {
        NavigationStack {
            Form {
                Section("PC abbinato") {
                    if let pc = model.pc {
                        LabeledContent("Nome", value: pc.name)
                        LabeledContent("Indirizzo", value: address)
                        LabeledContent("Impronta", value: Self.grouped(pc.fingerprint.prefix(16)))
                        // un elemento solo per l'accessibilità: la prova UI legge il valore (decisione P8)
                        LabeledContent("Stato", value: stateText)
                            .accessibilityElement(children: .ignore)
                            .accessibilityLabel("Stato")
                            .accessibilityValue(stateText)
                            .accessibilityIdentifier("settings-state")
                        Button("Abbina di nuovo") { pairing = true }
                        Button("Dimentica questo PC", role: .destructive) { confirmForget = true }
                    } else {
                        Text(Texts.notPaired).foregroundStyle(.secondary)
                        Button("Abbina un PC") { pairing = true }.accessibilityIdentifier("settings-pair")
                    }
                }
                Section(Texts.padTab) {
                    Toggle(Texts.padHaptics, isOn: $haptics)
                        .onChange(of: haptics) { _, on in pad.haptics = on }
                    LabeledContent(Texts.padBluetooth, value: Texts.padStatus(pad.status))
                }
                Section("App") {
                    LabeledContent("Versione", value: version)
                }
            }
            .navigationTitle("Impostazioni")
            .sheet(isPresented: $pairing) { PairSheet().environmentObject(model) }
            .confirmationDialog("Dimenticare questo PC?", isPresented: $confirmForget, titleVisibility: .visible) {
                Button("Dimentica", role: .destructive) { model.forget() }
                Button("Annulla", role: .cancel) {}
            } message: {
                Text("Il telefono smette di usarlo. Revocalo anche dalla webapp sul PC: Dispositivo › Telefoni › Revoca.")
            }
        }
    }
}
