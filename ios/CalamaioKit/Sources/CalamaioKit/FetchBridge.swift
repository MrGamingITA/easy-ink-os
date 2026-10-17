import Foundation
import WebKit

// Ramo B1 della prova del Task 7: i fetch con corpo arrivano da fetch-shim.js e partono da qui, con le stesse
// regole del gestore dello schema (Proxy.upstream). Solo dalla pagina principale dello schema proprio.
@MainActor
public final class FetchBridge: NSObject, WKScriptMessageHandlerWithReply {
    public static let name = "calamaioFetch"
    let transport: Transport
    let upstream: () -> Upstream?
    // Un 401 dal PC (telefono revocato, I1): come ProxyCore.onUnauthorized.
    public var onUnauthorized: () -> Void = {}

    public init(transport: Transport, upstream: @escaping () -> Upstream?) {
        self.transport = transport
        self.upstream = upstream
    }

    public static var shimSource: String {
        guard let u = Bundle.module.url(forResource: "fetch-shim", withExtension: "js"),
              let s = try? String(contentsOf: u, encoding: .utf8) else { return "" }
        return s
    }

    // Ponte e shim nella pagina (mondo della pagina: lo shim deve sostituire window.fetch), solo frame principale:
    // la webapp non ha iframe (M2). Un iframe dello schema che facesse POST con Blob o FormData passerebbe dal
    // gestore dello schema e il corpo non arriverebbe.
    public func install(in ucc: WKUserContentController) {
        ucc.addScriptMessageHandler(self, contentWorld: .page, name: Self.name)
        ucc.addUserScript(WKUserScript(source: Self.shimSource, injectionTime: .atDocumentStart, forMainFrameOnly: true, in: .page))
    }

    // Metodi ammessi dal ponte: fetch manda qui solo i metodi con corpo (GET e HEAD passano dallo schema).
    static let methods: Set<String> = ["POST", "PUT", "DELETE", "PATCH"]
    // Lunghezza massima del corpo in base64, controllata prima di decodificarlo (16 MiB × 4/3 più margine).
    public static let maxBase64 = (Proxy.maxBody + 2) / 3 * 4 + 16
    // Richieste del ponte in corso nello stesso momento; oltre, 503 senza partire.
    public static let maxInFlight = 4
    // Risposta raccolta per intero prima di tornare alla pagina: oltre, si annulla.
    public static let maxResponse = 32 << 20

    private struct Pending {
        var cancel: () -> Void
        let reply: (Any?, String?) -> Void
    }
    private var pending: [String: Pending] = [:]
    var inFlight: Int { pending.count }

    // {method, url, headers, body (base64) | tooLarge} dalla pagina → richiesta verso il PC.
    public static func request(from body: Any, upstream up: Upstream) throws -> URLRequest {
        guard let d = body as? [String: Any], let m = d["method"] as? String, let s = d["url"] as? String,
              let url = URL(string: s) else { throw Proxy.ProxyError.notOurURL }
        guard methods.contains(m) else { throw Proxy.ProxyError.badRequest }
        var page = URLRequest(url: url)
        page.httpMethod = m
        guard let hs = (d["headers"] ?? [String: String]()) as? [String: String] else { throw Proxy.ProxyError.badRequest }
        for (k, v) in hs {
            // anche le intestazioni che poi non passano: un CR/LF vuol dire un messaggio scritto a mano
            guard Proxy.isSafeHeader(k, v) else { throw Proxy.ProxyError.badRequest }
            page.setValue(v, forHTTPHeaderField: k)
        }
        let b64 = (d["body"] as? String) ?? ""
        if d["tooLarge"] as? Bool == true || b64.utf8.count > maxBase64 {
            _ = try Proxy.upstream(page, body: nil, to: up.endpoint, token: up.token) // prima l'URL, poi il 413
            throw Proxy.ProxyError.bodyTooLarge
        }
        guard let data = Data(base64Encoded: b64) else { throw Proxy.ProxyError.badRequest }
        return try Proxy.upstream(page, body: data, to: up.endpoint, token: up.token)
    }

    public func userContentController(_ ucc: WKUserContentController, didReceive message: WKScriptMessage,
                                      replyHandler: @escaping (Any?, String?) -> Void) {
        let o = message.frameInfo.securityOrigin
        guard message.frameInfo.isMainFrame, o.protocol == Proxy.scheme, o.host == Proxy.host else {
            return replyHandler(nil, "origine non ammessa")
        }
        handle(message.body, reply: replyHandler)
    }

    // Una risposta sola per messaggio: completa (status, intestazioni, corpo base64) o un errore senza dettagli di
    // rete. {cancel: id} ferma davvero la richiesta id (AbortSignal nella pagina).
    func handle(_ body: Any, reply: @escaping (Any?, String?) -> Void) {
        if let d = body as? [String: Any], let id = d["cancel"] as? String {
            if let p = pending.removeValue(forKey: id) {
                p.cancel()
                p.reply(nil, "AbortError")
            }
            return reply(nil, nil)
        }
        guard let d = body as? [String: Any], let id = d["id"] as? String, !id.isEmpty, id.utf8.count <= 64,
              pending[id] == nil else { return reply(nil, "richiesta non valida") }
        let pageURL = (d["url"] as? String).flatMap(URL.init(string:)) ?? Proxy.startURL
        guard let up = upstream() else { return reply(Self.local(503, pageURL, Texts.pcUnreachable), nil) }
        // il tetto prima di decodificare il corpo: oltre maxInFlight non si spende niente
        guard pending.count < Self.maxInFlight else { return reply(Self.local(503, pageURL, Texts.busy), nil) }
        let rq: URLRequest
        do { rq = try Self.request(from: body, upstream: up) } catch Proxy.ProxyError.bodyTooLarge {
            return reply(Self.local(413, pageURL, Texts.tooLarge), nil)
        } catch { return reply(nil, "richiesta non valida") }
        var status = 0
        var headers: [String: String] = [:]
        var data = Data()
        pending[id] = Pending(cancel: {}, reply: reply)
        let cancel = transport.start(rq, onResponse: { [weak self] r in
            status = r.statusCode
            headers = Proxy.headers(r)
            if r.statusCode == 401, self?.pending[id] != nil { self?.onUnauthorized() }
        }, onData: { [weak self] chunk in
            guard let self, self.pending[id] != nil else { return }
            if data.count + chunk.count > Self.maxResponse {
                let p = self.pending.removeValue(forKey: id)
                p?.cancel()
                return reply(nil, Texts.responseTooLarge)
            }
            data.append(chunk)
        }, onDone: { [weak self] err in
            // dopo un annullamento o una risposta troppo grande la voce non c'è più: nessuna seconda risposta
            guard let self, self.pending.removeValue(forKey: id) != nil else { return }
            if err != nil { reply(nil, Texts.pcUnreachable) }
            else { reply(["status": status, "headers": headers, "body": data.base64EncodedString()], nil) }
        })
        if pending[id] != nil { pending[id]?.cancel = cancel } // un trasporto che finisce subito l'ha già tolta
    }

    static func local(_ status: Int, _ url: URL, _ message: String) -> [String: Any] {
        let (r, d) = Proxy.local(status: status, url: url, message: message)
        return ["status": status, "headers": Proxy.headers(r), "body": d.base64EncodedString()]
    }
}
