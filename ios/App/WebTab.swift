import SwiftUI
import CalamaioKit

struct ShareItem: Identifiable {
    let url: URL
    var id: URL { url }
}

struct WebTab: View {
    @EnvironmentObject var model: AppController
    @State private var pairing = false
    @State private var share: ShareItem?
    @State private var shared: URL? // il file dell'ultimo foglio, da togliere quando si chiude
    @State private var backoff = ReconnectBackoff()
    @State private var gate = ExternalOpenGate()

    var body: some View {
        Group {
            switch model.phase {
            case .unpaired:
                ContentUnavailableView {
                    Label("Nessun PC abbinato", systemImage: "link")
                } description: {
                    Text(Texts.notPaired)
                } actions: {
                    Button("Abbina un PC") { pairing = true }
                        .buttonStyle(.borderedProminent)
                        .accessibilityIdentifier("open-pair")
                }
            case .connecting:
                ProgressView(Texts.searching)
            case let .failed(msg):
                ContentUnavailableView {
                    Label(Texts.pcUnreachable, systemImage: "wifi.exclamationmark")
                } description: {
                    Text(msg)
                } actions: {
                    Button("Riprova") { Task { await model.connect() } }
                        .buttonStyle(.borderedProminent)
                    Button("Abbina di nuovo") { pairing = true }
                }
            case .ready:
                // un PC nuovo (impronta diversa) vuol dire un trasporto nuovo: la vista si rifà da capo
                WebContainer(model: model, backoff: backoff, gate: gate, onShare: { shared = $0; share = ShareItem(url: $0) })
                    .id(model.pc?.fingerprint)
                    .ignoresSafeArea(edges: .bottom)
            }
        }
        .sheet(isPresented: $pairing) { PairSheet().environmentObject(model) }
        .sheet(item: $share, onDismiss: {
            if let u = shared { DownloadFolder().remove(u) }
            shared = nil
        }) { ShareSheet(items: [$0.url]) }
    }
}
