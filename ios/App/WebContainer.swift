import SwiftUI
import WebKit
import CalamaioKit

// La webapp del PC in una WKWebView, tutta attraverso calamaio-app:// (il token resta nell'app, spec §3.3).
struct WebContainer: UIViewRepresentable {
    let model: AppController
    // vivono nella scheda: sopravvivono alla vista rifatta dopo un ricollegamento
    let backoff: ReconnectBackoff
    let gate: ExternalOpenGate
    let onShare: (URL) -> Void

    func makeCoordinator() -> Coordinator { Coordinator(model: model, backoff: backoff, gate: gate, onShare: onShare) }

    func makeUIView(context: Context) -> WKWebView {
        let c = context.coordinator
        let cfg = WKWebViewConfiguration()
        cfg.websiteDataStore = .nonPersistent() // niente cookie né cache su disco
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0"
        cfg.applicationNameForUserAgent = WebScripts.userAgentName(version: version)
        cfg.setURLSchemeHandler(c.scheme, forURLScheme: Proxy.scheme)
        for src in WebScripts.sources(sseStreams: SpikeOutcome.sseStreams) {
            cfg.userContentController.addUserScript(WKUserScript(source: src, injectionTime: .atDocumentStart, forMainFrameOnly: true))
        }
        // ramo B1 della prova: i fetch con corpo passano dal ponte (nessun token nel JavaScript)
        if !SpikeOutcome.bodiesReachHandler { c.bridge.install(in: cfg.userContentController) }
        let wv = WKWebView(frame: .zero, configuration: cfg)
        wv.navigationDelegate = c
        wv.allowsBackForwardNavigationGestures = true
        wv.isInspectable = true // Safari › Sviluppo sul Mac, per le prove
        wv.accessibilityIdentifier = "webapp"
        wv.load(URLRequest(url: Proxy.startURL))
        return wv
    }

    func updateUIView(_ wv: WKWebView, context: Context) {}

    @MainActor
    final class Coordinator: NSObject, WKNavigationDelegate, WKDownloadDelegate {
        let model: AppController
        let backoff: ReconnectBackoff
        let onShare: (URL) -> Void
        let gate: ExternalOpenGate
        let folder = DownloadFolder()
        private var reconnect: Task<Void, Never>?
        // un trasporto solo per gestore e ponte, con l'impronta del PC abbinato
        let transport: URLSessionTransport
        let scheme: SchemeHandler
        let bridge: FetchBridge
        private var downloads: [ObjectIdentifier: URL] = [:]

        init(model: AppController, backoff: ReconnectBackoff, gate: ExternalOpenGate, onShare: @escaping (URL) -> Void) {
            self.model = model
            self.backoff = backoff
            self.gate = gate
            self.onShare = onShare
            transport = URLSessionTransport(pin: model.pc?.fingerprint ?? "")
            let up: () -> Upstream? = { [weak model] in model?.upstream }
            scheme = SchemeHandler(core: ProxyCore(transport: transport, upstream: up))
            bridge = FetchBridge(transport: transport, upstream: up)
            // I1: un 401 dal PC (telefono revocato) fa ricontrollare il PC una volta sola, poi «revocato»
            let unauthorized: () -> Void = { [weak model] in _ = model?.upstreamUnauthorized() }
            scheme.core.onUnauthorized = unauthorized
            bridge.onUnauthorized = unauthorized
        }

        // Indirizzi del PC: mai aperti in Safari (la WKWebView e Safari non hanno né il token né l'impronta).
        private var pcHosts: Set<String> {
            var s = Set(model.pc?.hosts ?? [])
            if let h = model.pc?.lastHost { s.insert(h) }
            if let h = model.upstream?.endpoint.host { s.insert(h) }
            return s
        }

        // Ogni navigazione, anche degli iframe (M6): il ponte conta sul fatto che frame di altre origini non si
        // caricano mai. Una finestra nuova (target=_blank) si apre nella stessa vista se la regola la ammette.
        func webView(_ webView: WKWebView, decidePolicyFor action: WKNavigationAction) async -> WKNavigationActionPolicy {
            let url = action.request.url
            switch NavigationPolicy.decide(url: url, shouldDownload: action.shouldPerformDownload) {
            case .download:
                return .download
            case .allow:
                if action.targetFrame == nil {
                    webView.load(action.request)
                    return .cancel
                }
                return .allow
            case .cancel:
                // M5 e I1: un link https:// (anche da script) può solo chiedere; si apre in Safari, senza token, dopo
                // la conferma dell'utente. Tutto il resto si ferma.
                if let url, NavigationPolicy.opensInSafari(url: url, linkActivated: action.navigationType == .linkActivated,
                                                           pcHosts: pcHosts, pcPort: model.pc?.port) {
                    askToOpen(url, from: webView)
                }
                return .cancel
            }
        }

        // Avviso nativo, fuori dalla portata della pagina: «Annulla» è la scelta proposta.
        private func askToOpen(_ url: URL, from webView: WKWebView) {
            guard gate.mayAsk(appActive: UIApplication.shared.applicationState == .active) else { return }
            var top = webView.window?.rootViewController
            while let p = top?.presentedViewController { top = p }
            guard let top else { gate.dismissed(); return }
            let alert = UIAlertController(title: Texts.openInSafari, message: url.host, preferredStyle: .alert)
            let cancel = UIAlertAction(title: Texts.cancel, style: .cancel) { [gate] _ in gate.dismissed() }
            alert.addAction(cancel)
            alert.addAction(UIAlertAction(title: Texts.open, style: .default) { [gate] _ in
                gate.dismissed()
                UIApplication.shared.open(url, options: [:], completionHandler: nil)
            })
            alert.preferredAction = cancel
            // se l'avviso non si mostra (top in chiusura) dismissed non arriva e il cancello resta chiuso fino al
            // prossimo avvio dell'app: niente si apre, si sbaglia dal lato sicuro (R3)
            top.present(alert, animated: true)
        }

        func webView(_ webView: WKWebView, decidePolicyFor navigationResponse: WKNavigationResponse) async -> WKNavigationResponsePolicy {
            // con lo schema proprio WebKit passa un NSURLResponse senza intestazioni (prova M4): gli scaricamenti
            // della webapp arrivano da <a download> o blob:, cioè da decidePolicyFor action
            let disp = (navigationResponse.response as? HTTPURLResponse)?.value(forHTTPHeaderField: "Content-Disposition")
            return NavigationPolicy.decideResponse(disposition: disp, canShow: navigationResponse.canShowMIMEType) == .download ? .download : .allow
        }

        func webView(_ webView: WKWebView, navigationAction: WKNavigationAction, didBecome download: WKDownload) { download.delegate = self }
        func webView(_ webView: WKWebView, navigationResponse: WKNavigationResponse, didBecome download: WKDownload) { download.delegate = self }

        // Scaricamenti (export di temi e salvataggi): in una cartella temporanea, poi il foglio di condivisione.
        func download(_ download: WKDownload, decideDestinationUsing response: URLResponse, suggestedFilename: String) async -> URL? {
            guard let url = try? folder.file(named: suggestedFilename) else { return nil }
            downloads[ObjectIdentifier(download)] = url
            return url
        }

        func downloadDidFinish(_ download: WKDownload) {
            if let u = downloads.removeValue(forKey: ObjectIdentifier(download)) { onShare(u) }
        }

        func download(_ download: WKDownload, didFailWithError error: Error, resumeData: Data?) {
            if let u = downloads.removeValue(forKey: ObjectIdentifier(download)) { folder.remove(u) }
        }

        // La pagina non arriva (PC sparito, impronta cambiata, telefono revocato): si ricontrolla il PC e lo stato
        // dice perché. Le navigazioni fermate da noi (link esterni, scaricamenti) non contano.
        func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
            let e = error as NSError
            if e.domain == "WebKitErrorDomain" || (e.domain == NSURLErrorDomain && e.code == NSURLErrorCancelled) { return }
            // M3: mai di seguito senza pausa (1, 2, 5, poi 10 s)
            let delay = backoff.next()
            reconnect?.cancel()
            reconnect = Task { [model] in
                try? await Task.sleep(for: .seconds(delay))
                if Task.isCancelled { return }
                await model.connect()
            }
        }

        func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) { backoff.reset() }

        // Mai accettare un certificato nella WKWebView: l'impronta la controlla solo URLSessionTransport.
        func webView(_ webView: WKWebView, didReceive challenge: URLAuthenticationChallenge) async -> (URLSession.AuthChallengeDisposition, URLCredential?) {
            (.cancelAuthenticationChallenge, nil)
        }
    }
}
