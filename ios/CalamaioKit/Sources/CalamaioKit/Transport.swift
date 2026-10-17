import Foundation

// Una richiesta verso il PC con la risposta a pezzi. Callback sul main thread; il valore restituito annulla.
public protocol Transport: AnyObject {
    func start(_ rq: URLRequest, onResponse: @escaping (HTTPURLResponse) -> Void, onData: @escaping (Data) -> Void,
               onDone: @escaping (Error?) -> Void) -> () -> Void
}

// URLSession con l'impronta fissata e un delegato per ogni richiesta (TaskDelegate). I redirect non si seguono:
// la pagina riceve il 30x con Location riportata sullo schema (Proxy.downstream). Come PinnedDelegate, nessun
// metodo di sfida a livello di sessione (c'è un test).
public final class URLSessionTransport: PinnedDelegate, Transport, @unchecked Sendable {
    // Silenzio massimo tra un pezzo e l'altro: l'SSE del servizio manda un commento ogni 15 s.
    public static let idleTimeout: TimeInterval = 60
    let session: URLSession

    public override init(pin: String) {
        let cfg = PCClient.sessionConfiguration(timeout: Self.idleTimeout)
        cfg.timeoutIntervalForResource = 7 * 24 * 3600 // l'SSE resta aperto finché la pagina c'è
        // Delegato di sessione separato (la sessione lo trattiene: con self sarebbe un ciclo); le sfide arrivano
        // comunque prima al delegato del task.
        session = URLSession(configuration: cfg, delegate: PinnedDelegate(pin: pin), delegateQueue: .main)
        super.init(pin: pin)
    }

    deinit { session.invalidateAndCancel() }

    public func start(_ rq: URLRequest, onResponse: @escaping (HTTPURLResponse) -> Void, onData: @escaping (Data) -> Void,
                      onDone: @escaping (Error?) -> Void) -> () -> Void {
        let d = taskDelegate(onResponse: onResponse, onData: onData, onDone: onDone)
        let t = session.dataTask(with: rq)
        t.delegate = d
        t.resume()
        return { d.silence(); t.cancel() }
    }

    func taskDelegate(onResponse: @escaping (HTTPURLResponse) -> Void, onData: @escaping (Data) -> Void,
                      onDone: @escaping (Error?) -> Void) -> TaskDelegate {
        TaskDelegate(owner: self, onResponse: onResponse, onData: onData, onDone: onDone)
    }

    // Delegato di una sola richiesta (main thread, la coda della sessione). Dopo silence() non chiama più nessuno.
    final class TaskDelegate: NSObject, URLSessionDataDelegate, @unchecked Sendable {
        let owner: PinnedDelegate
        private var onResponse: ((HTTPURLResponse) -> Void)?
        private var onData: ((Data) -> Void)?
        private var onDone: ((Error?) -> Void)?

        init(owner: PinnedDelegate, onResponse: @escaping (HTTPURLResponse) -> Void, onData: @escaping (Data) -> Void,
             onDone: @escaping (Error?) -> Void) {
            self.owner = owner
            self.onResponse = onResponse
            self.onData = onData
            self.onDone = onDone
        }

        func silence() { onResponse = nil; onData = nil; onDone = nil }

        func urlSession(_ session: URLSession, task: URLSessionTask, didReceive challenge: URLAuthenticationChallenge,
                        completionHandler: @escaping (URLSession.AuthChallengeDisposition, URLCredential?) -> Void) {
            let (d, c) = owner.decide(challenge)
            completionHandler(d, c)
        }

        // Mai seguire un redirect, nemmeno per l'SSE o un caricamento.
        func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                        newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
            completionHandler(nil)
        }

        func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse,
                        completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
            guard let h = response as? HTTPURLResponse else { return completionHandler(.cancel) }
            onResponse?(h)
            completionHandler(.allow)
        }

        func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
            onData?(data)
        }

        func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
            let done = onDone
            silence()
            done?(error)
        }
    }
}
