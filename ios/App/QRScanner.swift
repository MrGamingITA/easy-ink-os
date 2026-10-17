import SwiftUI
import VisionKit

// Fotocamera (VisionKit): il primo QR «calamaio://» va a onCode, una volta sola. Nel simulatore non c'è.
struct QRScanner: UIViewControllerRepresentable {
    let onCode: (String) -> Void

    static var isAvailable: Bool { DataScannerViewController.isSupported }

    func makeCoordinator() -> Coordinator { Coordinator(onCode: onCode) }

    func makeUIViewController(context: Context) -> DataScannerViewController {
        let vc = DataScannerViewController(recognizedDataTypes: [.barcode(symbologies: [.qr])], qualityLevel: .balanced,
                                           recognizesMultipleItems: false, isHighlightingEnabled: true)
        vc.delegate = context.coordinator
        try? vc.startScanning() // senza permesso della fotocamera resta nero: c'è il link da incollare
        return vc
    }

    func updateUIViewController(_ vc: DataScannerViewController, context: Context) {}

    static func dismantleUIViewController(_ vc: DataScannerViewController, coordinator: Coordinator) { vc.stopScanning() }

    final class Coordinator: NSObject, DataScannerViewControllerDelegate {
        let onCode: (String) -> Void
        private var done = false
        init(onCode: @escaping (String) -> Void) { self.onCode = onCode }

        func dataScanner(_ scanner: DataScannerViewController, didAdd items: [RecognizedItem], allItems: [RecognizedItem]) {
            guard !done else { return }
            for case let .barcode(b) in items {
                if let s = b.payloadStringValue, s.hasPrefix("calamaio://") {
                    done = true
                    onCode(s)
                    return
                }
            }
        }
    }
}
