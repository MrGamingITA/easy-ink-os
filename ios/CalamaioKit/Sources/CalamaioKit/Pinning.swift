import Foundation
import CryptoKit
import Security

public enum Pin {
    // SHA-256 del certificato (DER) in esadecimale minuscolo, come phones.Fingerprint nel servizio.
    public static func fingerprint(der: Data) -> String {
        SHA256.hash(data: der).map { String(format: "%02x", $0) }.joined()
    }

    // Confronto senza uscite anticipate.
    public static func matches(der: Data, pin: String) -> Bool {
        constantTimeEqual(fingerprint(der: der), pin.lowercased())
    }

    static func constantTimeEqual(_ x: String, _ y: String) -> Bool {
        let a = Array(x.utf8), b = Array(y.utf8)
        guard a.count == b.count else { return false }
        var d: UInt8 = 0
        for i in 0..<a.count { d |= a[i] ^ b[i] }
        return d == 0
    }

    // Il certificato foglia del server.
    public static func leafDER(_ trust: SecTrust) -> Data? {
        guard let chain = SecTrustCopyCertificateChain(trust) as? [SecCertificate], let leaf = chain.first else { return nil }
        return SecCertificateCopyData(leaf) as Data
    }
}

// Delegato delle sessioni verso il PC: accetta solo il certificato con l'impronta abbinata. Nessuna autorità di
// certificazione, nessuna eccezione TLS generica, nessun redirect (la risposta deve venire dall'endpoint fissato).
// `mismatch` dice se un errore recente era l'impronta (per sessione); chi vuole l'esito di una sola richiesta usa
// `PinnedDelegate.perRequest()` (vedi PCClient.send).
// ATTENZIONE (sottoclassi, Task 10): non aggiungere `urlSession(_:didReceive:completionHandler:)` a livello di sessione.
// Riceverebbe la sfida del certificato al posto del metodo di task e farebbe perdere l'esito per richiesta; c'è un test.
public class PinnedDelegate: NSObject, URLSessionTaskDelegate, @unchecked Sendable {
    public let pin: String
    private let lock = NSLock()
    private var sawMismatch = false

    public init(pin: String) { self.pin = pin.lowercased() }

    public var mismatch: Bool { lock.lock(); defer { lock.unlock() }; return sawMismatch }
    public func reset() { lock.lock(); sawMismatch = false; lock.unlock() }

    // Decide su una sfida; `onMismatch` è chiamato quando l'impronta non corrisponde.
    func decide(_ challenge: URLAuthenticationChallenge, onMismatch: () -> Void = {})
        -> (URLSession.AuthChallengeDisposition, URLCredential?) {
        // Nessun ripiego sulla fiducia di sistema: ogni sfida diversa dal certificato del server è rifiutata.
        guard challenge.protectionSpace.authenticationMethod == NSURLAuthenticationMethodServerTrust,
              let trust = challenge.protectionSpace.serverTrust else {
            return (.cancelAuthenticationChallenge, nil)
        }
        if let der = Pin.leafDER(trust), Pin.matches(der: der, pin: pin) {
            return (.useCredential, URLCredential(trust: trust))
        }
        lock.lock(); sawMismatch = true; lock.unlock()
        onMismatch()
        return (.cancelAuthenticationChallenge, nil)
    }

    // Livello di task (non di sessione): così ogni richiesta può avere il proprio esito (RequestDelegate).
    public func urlSession(_ session: URLSession, task: URLSessionTask, didReceive challenge: URLAuthenticationChallenge,
                           completionHandler: @escaping (URLSession.AuthChallengeDisposition, URLCredential?) -> Void) {
        let (d, c) = decide(challenge)
        completionHandler(d, c)
    }

    // Mai seguire un redirect: la 3xx arriva a chi chiama, che la tratta come errore.
    public func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                           newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
        completionHandler(nil)
    }

    // Delegato di una sola richiesta: stessa decisione, ma l'esito dell'impronta resta legato a quella richiesta.
    func perRequest() -> RequestDelegate { RequestDelegate(owner: self) }
}

final class RequestDelegate: NSObject, URLSessionTaskDelegate, @unchecked Sendable {
    let owner: PinnedDelegate
    private let lock = NSLock()
    private var flag = false
    init(owner: PinnedDelegate) { self.owner = owner }
    var mismatch: Bool { lock.lock(); defer { lock.unlock() }; return flag }

    func urlSession(_ session: URLSession, task: URLSessionTask, didReceive challenge: URLAuthenticationChallenge,
                    completionHandler: @escaping (URLSession.AuthChallengeDisposition, URLCredential?) -> Void) {
        let (d, c) = owner.decide(challenge) { lock.lock(); flag = true; lock.unlock() }
        completionHandler(d, c)
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
        completionHandler(nil)
    }
}
