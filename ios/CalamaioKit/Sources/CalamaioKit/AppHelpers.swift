import Foundation

// Link esterni (I1): navigationType .linkActivated non prova un tocco vero (anche a.click() da script lo è), quindi
// ogni apertura in Safari chiede conferma con un avviso nativo. Un avviso alla volta; una richiesta mentre è aperto,
// o entro 2 s dalla chiusura, si scarta in silenzio; solo con l'app attiva.
@MainActor
public final class ExternalOpenGate {
    public static let cooldown: TimeInterval = 2
    private var showing = false
    private var closedAt: Date?

    public init() {}

    // true: si può mostrare l'avviso (e da qui è «aperto» fino a dismissed)
    public func mayAsk(now: Date = Date(), appActive: Bool) -> Bool {
        guard appActive, !showing else { return false }
        if let c = closedAt, now.timeIntervalSince(c) < Self.cooldown { return false }
        showing = true
        return true
    }

    public func dismissed(now: Date = Date()) {
        showing = false
        closedAt = now
    }
}

// Attesa prima di ricollegarsi dopo una pagina che non arriva (M3): 1, 2, 5, poi ogni 10 s; da capo dopo un successo.
@MainActor
public final class ReconnectBackoff {
    static let steps: [TimeInterval] = [1, 2, 5, 10]
    private var n = 0

    public init() {}

    public func next() -> TimeInterval {
        defer { n += 1 }
        return Self.steps[min(n, Self.steps.count - 1)]
    }

    public func reset() { n = 0 }
}

// File scaricati (M4): una cartella per scaricamento sotto <tmp>/downloads, tolta quando il foglio di condivisione
// si chiude; all'avvio si toglie quello che è rimasto.
public struct DownloadFolder: Sendable {
    public let root: URL

    public init(base: URL = FileManager.default.temporaryDirectory) {
        root = base.appendingPathComponent("downloads", isDirectory: true)
    }

    public func file(named suggested: String) throws -> URL {
        let dir = root.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir.appendingPathComponent(DownloadName.safe(suggested))
    }

    // Toglie la cartella del file, solo se è proprio una delle nostre.
    public func remove(_ file: URL) {
        let dir = file.standardizedFileURL.deletingLastPathComponent()
        guard dir.deletingLastPathComponent().path == root.standardizedFileURL.path else { return }
        try? FileManager.default.removeItem(at: dir)
    }

    public func purge() { try? FileManager.default.removeItem(at: root) }
}
