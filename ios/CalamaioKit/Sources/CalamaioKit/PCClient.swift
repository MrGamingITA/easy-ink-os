import Foundation

public struct WhoAmI: Decodable, Equatable, Sendable {
    public struct Phone: Decodable, Equatable, Sendable { public let id: String; public let name: String }
    public struct PC: Decodable, Equatable, Sendable { public let name: String; public let version: String }
    public let phone: Phone
    public let pc: PC
    public let hosts: [String]
    public let port: Int
}

public enum PCError: Error, Equatable {
    case pinMismatch              // il PC risponde con un certificato diverso da quello abbinato
    case unauthorized             // 401: token revocato o sconosciuto
    case pairingRejected(String)  // /app/pair rifiutato (codice sbagliato, finestra chiusa)
    case unreachable              // nessuna risposta (rete, porta chiusa, tempo scaduto)
    case badResponse(Int)
    case cancelled                // richiesta annullata da chi chiama
}

// Richieste native al PC (abbinamento e prova del collegamento), con l'impronta fissata. Il token passa solo da qui
// e dal gestore dello schema: mai dal JavaScript.
public final class PCClient: @unchecked Sendable {
    let delegate: PinnedDelegate
    let session: URLSession

    // Configurazione unica delle sessioni verso il PC (usata anche dal gestore dello schema, Task 10):
    // effimera, senza cache né cookie, senza attesa di connettività.
    public static func sessionConfiguration(timeout: TimeInterval) -> URLSessionConfiguration {
        let cfg = URLSessionConfiguration.ephemeral
        cfg.timeoutIntervalForRequest = timeout
        cfg.timeoutIntervalForResource = timeout * 2
        cfg.waitsForConnectivity = false
        cfg.urlCache = nil
        cfg.httpCookieStorage = nil
        cfg.urlCredentialStorage = nil
        cfg.httpShouldSetCookies = false
        cfg.tlsMinimumSupportedProtocolVersion = .TLSv13 // il servizio parla solo TLS 1.3
        return cfg
    }

    public init(pin: String, timeout: TimeInterval = 4) {
        delegate = PinnedDelegate(pin: pin)
        session = URLSession(configuration: Self.sessionConfiguration(timeout: timeout), delegate: delegate, delegateQueue: nil)
    }

    deinit { session.invalidateAndCancel() }

    static func url(_ host: String, _ port: Int, _ path: String) -> URL {
        var c = URLComponents()
        c.scheme = "https"
        c.host = host
        c.port = port
        c.path = path
        return c.url!
    }

    // POST /app/pair → token. oldToken è quello di prima per lo stesso PC (il servizio revoca il telefono vecchio):
    // parte solo se oldTokenFingerprint, l'impronta salvata insieme a quel token, è uguale a quella di questo client
    // (cioè del nuovo QR). Altrimenti un QR ostile riceverebbe il token del PC vero: in quel caso non si invia.
    public func pair(host: String, port: Int, code: String, name: String,
                     oldToken: String? = nil, oldTokenFingerprint: String? = nil) async throws -> String {
        try await pair(host: host, port: port, code: code, name: name, oldToken: oldToken, oldTokenFingerprint: oldTokenFingerprint,
                       path: "/app/pair")
    }

    func pair(host: String, port: Int, code: String, name: String, oldToken: String?, oldTokenFingerprint: String?,
              path: String) async throws -> String {
        var rq = URLRequest(url: Self.url(host, port, path))
        rq.httpMethod = "POST"
        rq.setValue("application/json", forHTTPHeaderField: "Content-Type")
        if let o = oldToken, PairingLink.isLowerHex(o, 64), let f = oldTokenFingerprint, Pin.constantTimeEqual(f.lowercased(), delegate.pin) {
            rq.setValue("Bearer " + o, forHTTPHeaderField: "Authorization")
        }
        rq.httpBody = try JSONSerialization.data(withJSONObject: ["code": code, "name": name])
        let (data, status) = try await send(rq)
        guard status == 200 else { throw PCError.pairingRejected(Self.errorText(data) ?? "HTTP \(status)") }
        struct R: Decodable { let token: String }
        guard let r = try? JSONDecoder().decode(R.self, from: data), PairingLink.isLowerHex(r.token, 64) else {
            throw PCError.badResponse(status)
        }
        return r.token
    }

    public func whoami(host: String, port: Int, token: String) async throws -> WhoAmI {
        try await whoami(host: host, port: port, token: token, path: "/app/whoami")
    }

    func whoami(host: String, port: Int, token: String, path: String) async throws -> WhoAmI {
        // Un token malformato non parte nemmeno (niente CR/LF nell'intestazione, niente valori corrotti dal Portachiavi).
        guard PairingLink.isLowerHex(token, 64) else { throw PCError.unauthorized }
        var rq = URLRequest(url: Self.url(host, port, path))
        rq.setValue("Bearer " + token, forHTTPHeaderField: "Authorization")
        let (data, status) = try await send(rq)
        guard status == 200, let me = try? JSONDecoder().decode(WhoAmI.self, from: data) else { throw PCError.badResponse(status) }
        return me
    }

    // L'esito dell'impronta è legato alla singola richiesta (delegato per richiesta), non alla sessione.
    func send(_ rq: URLRequest) async throws -> (Data, Int) {
        let per = delegate.perRequest()
        let data: Data
        let resp: URLResponse
        do {
            (data, resp) = try await session.data(for: rq, delegate: per)
        } catch {
            if per.mismatch { throw PCError.pinMismatch }
            if error is CancellationError || (error as? URLError)?.code == .cancelled { throw PCError.cancelled }
            throw PCError.unreachable
        }
        guard let h = resp as? HTTPURLResponse else { throw PCError.badResponse(0) }
        if h.statusCode == 401 { throw PCError.unauthorized }
        return (data, h.statusCode)
    }

    static func errorText(_ d: Data) -> String? {
        (try? JSONSerialization.jsonObject(with: d) as? [String: Any])?["error"] as? String
    }
}
