import SwiftUI
import CalamaioKit

struct PairSheet: View {
    @EnvironmentObject var model: AppController
    @Environment(\.dismiss) private var dismiss
    @State private var link = ""
    @State private var error: String?
    @State private var busy = false
    @State private var attempt = 0 // dopo un errore la fotocamera riparte da capo
    @State private var job: Task<Void, Never>? // l'abbinamento in corso: «Annulla» lo ferma

    var body: some View {
        NavigationStack {
            Form {
                if QRScanner.isAvailable {
                    Section {
                        QRScanner { text in start(text) }
                            .id(attempt)
                            .frame(height: 300)
                            .listRowInsets(EdgeInsets())
                    } footer: {
                        Text("Inquadra il codice mostrato dalla webapp sul PC: Dispositivo › Telefoni › «Abbina un telefono».")
                    }
                }
                Section {
                    TextField("calamaio://pair?…", text: $link)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .font(.system(.footnote, design: .monospaced))
                        .accessibilityIdentifier("pair-link")
                    PasteButton(payloadType: String.self) { strings in link = strings.first ?? "" }
                    Button("Abbina") { start(link) }
                        .disabled(busy || link.isEmpty)
                        .accessibilityIdentifier("pair-go")
                } header: {
                    Text("Link di abbinamento")
                } footer: {
                    Text("Senza fotocamera: nella webapp premi «Copia il link» e incollalo qui.")
                }
                if let error {
                    Section { Text(error).foregroundStyle(.red).accessibilityIdentifier("pair-error") }
                }
                if busy {
                    Section { ProgressView("Abbino il telefono…") }
                }
            }
            .navigationTitle("Abbina un PC")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Annulla") {
                        job?.cancel()
                        dismiss()
                    }
                }
            }
        }
        .onDisappear { job?.cancel() }
    }

    private func start(_ text: String) {
        guard !busy else { return }
        job = Task { await submit(text) }
    }

    private func submit(_ text: String) async {
        guard !busy else { return }
        busy = true
        error = nil
        let err = await model.pair(with: text)
        busy = false
        // annullato: prima della risposta del PC niente è salvato; dopo, il PC resta abbinato e la ricerca continua
        if Task.isCancelled { return }
        if let err {
            error = err
            attempt += 1
        } else {
            dismiss()
        }
    }
}
