import Foundation

// Regole del passaggio pagina → PC (spec §3.3): solo calamaio-app://ui, intestazioni in elenco, token aggiunto qui.
public enum Proxy {
    public static let scheme = "calamaio-app"
    public static let host = "ui"
    public static let maxBody = 16 << 20 // come maxBody del servizio
    public static let startURL = URL(string: "calamaio-app://ui/")!

    // CSP delle risposte fatte dall'app (M3): la stessa della webapp, `csp` in service/internal/api/web.go (un test
    // le confronta).
    public static let csp = "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; " +
        "font-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; form-action 'self'; " +
        "frame-ancestors 'none'"

    // badRequest: metodo che non è un token HTTP, intestazione con CR/LF/NUL, messaggio del ponte malformato.
    public enum ProxyError: Error, Equatable { case notOurURL, bodyTooLarge, badRequest }

    // tchar di RFC 7230 §3.2.6: il metodo deve essere fatto solo di questi.
    static let tchar = Set("!#$%&'*+-.^_`|~0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ".utf8)

    static func isToken(_ s: String) -> Bool { !s.isEmpty && s.utf8.allSatisfy(tchar.contains) }

    // Nome e valore senza CR, LF o NUL (niente intestazioni iniettate); il nome deve essere un token.
    static func isSafeHeader(_ name: String, _ value: String) -> Bool {
        isToken(name) && !value.utf8.contains(where: { $0 == 0x0d || $0 == 0x0a || $0 == 0 })
    }

    // Le sole intestazioni della pagina che arrivano al PC: Origin, Cookie, Referer e un Authorization messo dal
    // JavaScript non passano mai.
    static let forwarded: Set<String> = ["accept", "content-type", "range", "last-event-id", "if-none-match"]
    // Intestazioni di trasporto che la pagina non deve vedere (il corpo è già decompresso e a pezzi).
    static let dropped: Set<String> = ["content-length", "content-encoding", "transfer-encoding", "connection",
                                       "set-cookie", "strict-transport-security"]

    public static func upstream(_ rq: URLRequest, body: Data?, to ep: Endpoint, token: String) throws -> URLRequest {
        guard let u = rq.url, u.scheme?.lowercased() == scheme, u.host == host,
              let c = URLComponents(url: u, resolvingAgainstBaseURL: false),
              c.user == nil, c.password == nil, c.port == nil else { throw ProxyError.notOurURL }
        let method = rq.httpMethod ?? "GET"
        guard isToken(method) else { throw ProxyError.badRequest }
        if let b = body, b.count > maxBody { throw ProxyError.bodyTooLarge }
        // Percorso e query copiati così come sono: il servizio risponde 404 a ogni codifica non canonica, quindi
        // niente URLComponents.path né ricodifiche.
        let path = c.percentEncodedPath.isEmpty ? "/" : c.percentEncodedPath
        let query = c.percentEncodedQuery.map { "?" + $0 } ?? ""
        let h = ep.host.contains(":") ? "[\(ep.host)]" : ep.host
        guard path.hasPrefix("/"), let target = URL(string: "https://\(h):\(ep.port)\(path)\(query)") else {
            throw ProxyError.notOurURL
        }
        var out = URLRequest(url: target, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: URLSessionTransport.idleTimeout)
        out.httpMethod = method
        for (k, v) in rq.allHTTPHeaderFields ?? [:] where forwarded.contains(k.lowercased()) {
            guard isSafeHeader(k, v) else { throw ProxyError.badRequest }
            out.setValue(v, forHTTPHeaderField: k)
        }
        out.setValue("Bearer " + token, forHTTPHeaderField: "Authorization")
        out.httpShouldHandleCookies = false
        out.httpBody = body
        return out
    }

    // La risposta del PC vista dalla pagina: URL dello schema proprio, Location riportata sullo schema.
    public static func downstream(_ r: HTTPURLResponse, for url: URL) -> HTTPURLResponse {
        HTTPURLResponse(url: url, statusCode: r.statusCode, httpVersion: "HTTP/1.1", headerFields: headers(r))!
    }

    static func headers(_ r: HTTPURLResponse) -> [String: String] {
        var h: [String: String] = [:]
        for (k, v) in r.allHeaderFields {
            guard let ks = k as? String, let vs = v as? String, !dropped.contains(ks.lowercased()) else { continue }
            h[ks] = ks.lowercased() == "location" ? onScheme(vs) : vs
        }
        return h
    }

    static func onScheme(_ location: String) -> String {
        guard var c = URLComponents(string: location), c.scheme == "https" else { return location }
        c.scheme = scheme
        c.host = host
        c.port = nil
        return c.string ?? location
    }

    // Richiesta di un documento (pagina o iframe): WebKit la manda con Accept text/html, i fetch con */*.
    static func isDocument(_ rq: URLRequest) -> Bool {
        rq.value(forHTTPHeaderField: "Accept")?.lowercased().contains("text/html") == true
    }

    // M4: WebKit non segue un 3xx restituito dal gestore dello schema (prova nel simulatore, iOS 27). Per un documento
    // l'app risponde con una pagina che porta al Location riportato sullo schema (la navigazione nuova passa di nuovo
    // da NavigationPolicy); un Location fuori dallo schema diventa 502, senza l'indirizzo. nil = non è un redirect.
    public static func redirectPage(_ r: HTTPURLResponse, for url: URL) -> (HTTPURLResponse, Data)? {
        guard (300..<400).contains(r.statusCode), let loc = r.value(forHTTPHeaderField: "Location") else { return nil }
        guard let to = URL(string: onScheme(loc), relativeTo: url)?.absoluteURL, to.scheme?.lowercased() == scheme,
              to.host == host, to.user == nil, to.password == nil, to.port == nil else {
            return local(status: 502, url: url, message: Texts.pcUnreachable)
        }
        var attr = to.absoluteString
        for (a, b) in [("&", "&amp;"), ("\"", "&quot;"), ("'", "&#39;"), ("<", "&lt;"), (">", "&gt;")] {
            attr = attr.replacingOccurrences(of: a, with: b)
        }
        let html = #"<!doctype html><meta charset="utf-8"><meta http-equiv="refresh" content="0;url="# + attr + #"">"#
        let resp = HTTPURLResponse(url: url, statusCode: 200, httpVersion: "HTTP/1.1",
                                   headerFields: ["Content-Type": "text/html; charset=utf-8", "Cache-Control": "no-store",
                                                 "Content-Security-Policy": csp])!
        return (resp, Data(html.utf8))
    }

    // Il corpo della richiesta della pagina: httpBody, o lo stream fino a maxBody+1 byte (oltre: 413).
    // Si legge sul main thread, ma con il ramo B1 lo stream non arriva mai qui: la prova del Task 7 ha misurato
    // httpBodyStream assente per JSON, ArrayBuffer, Blob e FormData, e fetch-shim.js manda dal ponte ogni metodo
    // diverso da GET/HEAD, quindi il gestore dello schema vede solo richieste senza corpo. Se iOS cambiasse,
    // SchemeBodySpikeTests fallirebbe e questa lettura andrebbe spostata fuori dal main thread.
    public static func readBody(_ rq: URLRequest) -> Data? {
        if let b = rq.httpBody { return b }
        guard let s = rq.httpBodyStream else { return nil }
        s.open()
        defer { s.close() }
        var out = Data()
        var buf = [UInt8](repeating: 0, count: 64 << 10)
        while out.count <= maxBody {
            let n = s.read(&buf, maxLength: min(buf.count, maxBody + 1 - out.count))
            if n <= 0 { break }
            out.append(buf, count: n)
        }
        return out
    }

    // Una risposta fatta dall'app (PC non raggiungibile, file troppo grande), JSON come quelle del servizio.
    public static func local(status: Int, url: URL, message: String) -> (HTTPURLResponse, Data) {
        let body = (try? JSONSerialization.data(withJSONObject: ["error": message])) ?? Data()
        let r = HTTPURLResponse(url: url, statusCode: status, httpVersion: "HTTP/1.1",
                                headerFields: ["Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store",
                                                "Content-Security-Policy": csp])!
        return (r, body)
    }

    // L'errore di rete visto dalla pagina: solo il codice e l'URL dello schema (mai l'indirizzo del PC).
    static func pageError(_ e: Error, url: URL) -> Error {
        let code = (e as? URLError)?.code ?? .unknown
        return URLError(code, userInfo: [NSURLErrorFailingURLErrorKey: url, NSLocalizedDescriptionKey: Texts.pcUnreachable])
    }
}
