import Foundation

public enum NavDecision: Equatable { case allow, download, cancel }

public enum NavigationPolicy {
    // Solo le pagine dello schema proprio; blob: e le risposte da scaricare diventano scaricamenti. Mai https://
    // verso il PC (la WKWebView non ha né l'impronta né il token) e mai siti esterni.
    public static func decide(url: URL?, shouldDownload: Bool) -> NavDecision {
        guard let u = url, let s = u.scheme?.lowercased() else { return .cancel }
        let ours = isOurs(u)
        if shouldDownload && (ours || s == "blob") { return .download }
        if ours { return .allow }
        if u.absoluteString == "about:blank" { return .allow }
        return .cancel
    }

    // Esattamente calamaio-app://ui, senza utente, password né porta: le stesse regole di Proxy.upstream (M2), così
    // una navigazione ammessa qui non fallisce poi nel gestore (e non fa ricollegare l'app per niente).
    static func isOurs(_ u: URL) -> Bool {
        u.scheme == Proxy.scheme && u.host == Proxy.host && u.user == nil && u.password == nil && u.port == nil
    }

    // Link esterni (decisione del coordinatore, M5): solo un https:// toccato dall'utente si apre in Safari, senza
    // token; mai verso il PC (né i suoi indirizzi né la sua porta), mai con credenziali nell'URL. Tutto il resto
    // fuori dallo schema proprio si ferma e basta.
    public static func opensInSafari(url: URL?, linkActivated: Bool, pcHosts: Set<String>, pcPort: Int?) -> Bool {
        guard linkActivated, let u = url, u.scheme?.lowercased() == "https",
              let h = u.host?.lowercased(), !h.isEmpty, u.user == nil, u.password == nil else { return false }
        if pcHosts.contains(where: { $0.lowercased() == h }) { return false }
        if let p = pcPort, u.port == p { return false }
        return true
    }

    public static func decideResponse(disposition: String?, canShow: Bool) -> NavDecision {
        if let d = disposition?.lowercased(), d.hasPrefix("attachment") { return .download }
        return canShow ? .allow : .download
    }
}

public enum DownloadName {
    public static let maxLength = 120

    // Solo l'ultimo pezzo del nome proposto, senza caratteri di controllo né punti iniziali (niente file nascosti),
    // al massimo 120 caratteri; se non resta niente, «download».
    public static func safe(_ s: String) -> String {
        let last = s.split(whereSeparator: { $0 == "/" || $0 == "\\" }).last.map(String.init) ?? ""
        var clean = String(String.UnicodeScalarView(last.unicodeScalars.filter { !$0.properties.generalCategory.isControlLike }))
        while clean.hasPrefix(".") { clean.removeFirst() }
        clean = String(clean.prefix(maxLength))
        return clean.isEmpty ? "download" : clean
    }
}

extension Unicode.GeneralCategory {
    // Cc (controllo), Cf (formato, per esempio U+202E che rovescia il testo), Zl/Zp (fine riga).
    var isControlLike: Bool {
        switch self {
        case .control, .format, .lineSeparator, .paragraphSeparator: return true
        default: return false
        }
    }
}
