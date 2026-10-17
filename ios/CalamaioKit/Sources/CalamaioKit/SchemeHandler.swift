import Foundation
import WebKit

// Il compito di WebKit visto dal gestore (permette le prove senza WKWebView).
public protocol SchemeTask: AnyObject {
    var request: URLRequest { get }
    func didReceive(_ response: URLResponse)
    func didReceive(_ data: Data)
    func didFinish()
    func didFailWithError(_ error: Error)
}

// Ogni richiesta della pagina (pagine, JS, API, SSE) va al PC con l'impronta fissata e il token. Dopo lo stop di
// WebKit nessuna chiamata al compito (WebKit si fermerebbe con un errore grave).
@MainActor
public final class ProxyCore {
    let transport: Transport
    public var upstream: () -> Upstream?
    // Un 401 dal PC (telefono revocato, I1): l'app ricontrolla (AppController.upstreamUnauthorized, con la pausa).
    public var onUnauthorized: () -> Void = {}
    private var live: [ObjectIdentifier: () -> Void] = [:]

    public init(transport: Transport, upstream: @escaping () -> Upstream?) {
        self.transport = transport
        self.upstream = upstream
    }

    // onEnd: il compito è finito (risposta completa, errore o risposta locale); non viene chiamata dopo uno stop.
    public func start(_ task: SchemeTask, onEnd: @escaping () -> Void = {}) {
        let id = ObjectIdentifier(task)
        guard let url = task.request.url else { task.didFailWithError(URLError(.badURL)); onEnd(); return }
        guard let up = upstream() else { return reply(task, Proxy.local(status: 503, url: url, message: Texts.pcUnreachable), onEnd) }
        let rq: URLRequest
        do {
            rq = try Proxy.upstream(task.request, body: Proxy.readBody(task.request), to: up.endpoint, token: up.token)
        } catch Proxy.ProxyError.bodyTooLarge {
            return reply(task, Proxy.local(status: 413, url: url, message: Texts.tooLarge), onEnd)
        } catch {
            task.didFailWithError(URLError(.unsupportedURL, userInfo: [NSURLErrorFailingURLErrorKey: url]))
            onEnd()
            return
        }
        live[id] = {}
        let document = Proxy.isDocument(task.request)
        var redirected = false
        let cancel = transport.start(rq, onResponse: { [weak self] r in
            guard let self, self.live[id] != nil else { return }
            if r.statusCode == 401 { self.onUnauthorized() } // la pagina riceve comunque il 401
            // 3xx di un documento: pagina dell'app al posto del redirect (WebKit non lo seguirebbe), PC fermato
            if document, let page = Proxy.redirectPage(r, for: url) {
                redirected = true
                self.live.removeValue(forKey: id)?()
                return self.reply(task, page, onEnd)
            }
            task.didReceive(Proxy.downstream(r, for: url))
        }, onData: { [weak self] d in
            guard self?.live[id] != nil else { return }
            task.didReceive(d)
        }, onDone: { [weak self] err in
            guard let self, self.live.removeValue(forKey: id) != nil else { return }
            if let err { task.didFailWithError(Proxy.pageError(err, url: url)) } else { task.didFinish() }
            onEnd()
        })
        if live[id] != nil { live[id] = cancel } // un trasporto che finisce subito ha già tolto il segnaposto
        else if redirected { cancel() } // redirect arrivato dentro start: il segnaposto non fermava niente
    }

    public func stop(_ task: SchemeTask) {
        live.removeValue(forKey: ObjectIdentifier(task))?()
    }

    private func reply(_ task: SchemeTask, _ r: (HTTPURLResponse, Data), _ onEnd: () -> Void) {
        task.didReceive(r.0)
        task.didReceive(r.1)
        task.didFinish()
        onEnd()
    }
}

// WKURLSchemeTask → SchemeTask (un box per compito, ritrovato dall'identità del compito).
final class WKTaskBox: SchemeTask {
    let t: WKURLSchemeTask
    init(_ t: WKURLSchemeTask) { self.t = t }
    var request: URLRequest { t.request }
    func didReceive(_ response: URLResponse) { t.didReceive(response) }
    func didReceive(_ data: Data) { t.didReceive(data) }
    func didFinish() { t.didFinish() }
    func didFailWithError(_ error: Error) { t.didFailWithError(error) }
}

@MainActor
public final class SchemeHandler: NSObject, WKURLSchemeHandler {
    public let core: ProxyCore
    private var boxes: [ObjectIdentifier: WKTaskBox] = [:]

    public init(core: ProxyCore) { self.core = core }

    public func webView(_ webView: WKWebView, start urlSchemeTask: WKURLSchemeTask) {
        let id = ObjectIdentifier(urlSchemeTask)
        let box = WKTaskBox(urlSchemeTask)
        boxes[id] = box // il box vive finché il compito finisce o WebKit lo ferma
        core.start(box, onEnd: { [weak self] in self?.boxes[id] = nil })
    }

    public func webView(_ webView: WKWebView, stop urlSchemeTask: WKURLSchemeTask) {
        if let box = boxes.removeValue(forKey: ObjectIdentifier(urlSchemeTask)) { core.stop(box) }
    }
}
