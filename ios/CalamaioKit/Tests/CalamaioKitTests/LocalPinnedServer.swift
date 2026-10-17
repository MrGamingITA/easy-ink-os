import Foundation
import XCTest
@testable import CalamaioKit

// Server HTTPS locale di prova (Python + certificato appena generato con openssl) che risponde a 302/307 verso un
// server http in chiaro e registra cosa riceve. Serve solo ai test sul Mac (`swift test`). Senza openssl e python3 con TLS 1.3
// i test FALLISCONO (coprono redirect, token vecchio e impronta): si saltano solo con CALAMAIO_SKIP_TLS_TESTS=1.
enum StartError: Error, CustomStringConvertible {
    case notStarted
    var description: String { "il server locale di prova non è partito" }
}

final class LocalPinnedServer {
    let process = Process()
    let dir: URL
    let tlsPort: Int
    let plainPort: Int
    let pin: String
    private var stdin: Pipe?

    init() throws {
        let fm = FileManager.default
        guard fm.isExecutableFile(atPath: "/usr/bin/openssl"), let py = Self.python() else {
            if ProcessInfo.processInfo.environment["CALAMAIO_SKIP_TLS_TESTS"] == "1" {
                throw XCTSkip("CALAMAIO_SKIP_TLS_TESTS=1: test col server TLS locale saltati")
            }
            struct Missing: Error, CustomStringConvertible {
                var description: String { "serve un python3 con TLS 1.3 (brew install python) e /usr/bin/openssl; per saltare questi test: CALAMAIO_SKIP_TLS_TESTS=1" }
            }
            throw Missing()
        }
        dir = fm.temporaryDirectory.appendingPathComponent("calamaio-pin-\(UUID().uuidString)")
        try fm.createDirectory(at: dir, withIntermediateDirectories: true)
        // curva con nome (LibreSSL con ec_paramgen scrive parametri espliciti, che Apple rifiuta)
        let key = dir.appendingPathComponent("k.pem").path
        try Self.run("/usr/bin/openssl", ["ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", key])
        // certificato v3 con SAN, come quello del servizio (Apple rifiuta i v1 prima ancora della sfida)
        let cnf = dir.appendingPathComponent("o.cnf")
        try "[req]\ndistinguished_name=dn\nx509_extensions=v3\nprompt=no\n[dn]\nCN=prova\n[v3]\nsubjectAltName=IP:127.0.0.1\nextendedKeyUsage=serverAuth\nbasicConstraints=critical,CA:FALSE\n"
            .write(to: cnf, atomically: true, encoding: .utf8)
        try Self.run("/usr/bin/openssl", ["req", "-x509", "-new", "-key", key, "-out", dir.appendingPathComponent("c.pem").path,
                                          "-days", "1", "-config", cnf.path])
        try Self.run("/usr/bin/openssl", ["x509", "-in", dir.appendingPathComponent("c.pem").path, "-outform", "der",
                                          "-out", dir.appendingPathComponent("c.der").path])
        pin = Pin.fingerprint(der: try Data(contentsOf: dir.appendingPathComponent("c.der")))
        try Self.script.write(to: dir.appendingPathComponent("srv.py"), atomically: true, encoding: .utf8)
        process.executableURL = URL(fileURLWithPath: py)
        process.arguments = ["-I", dir.appendingPathComponent("srv.py").path, dir.path]
        // il server esce quando la pipe si chiude (anche se il processo di test muore senza deinit)
        stdin = Pipe()
        process.standardInput = stdin
        try process.run()
        // lo script scrive le porte in ports.json quando è pronto
        var ports: [String: Int]?
        for _ in 0..<100 {
            if let d = try? Data(contentsOf: dir.appendingPathComponent("ports.json")),
               let p = try? JSONDecoder().decode([String: Int].self, from: d) { ports = p; break }
            Thread.sleep(forTimeInterval: 0.05)
        }
        guard let p = ports else { process.terminate(); throw StartError.notStarted }
        tlsPort = p["tls"]!
        plainPort = p["plain"]!
    }

    deinit {
        if process.isRunning { process.terminate() }
        try? FileManager.default.removeItem(at: dir)
    }

    // Eventi registrati: richieste arrivate al server in chiaro ("plain GET /leak") e al TLS ("tls POST /app/pair auth=…").
    func events() -> [String] {
        guard let d = try? Data(contentsOf: dir.appendingPathComponent("events.json")),
              let e = try? JSONDecoder().decode([String].self, from: d) else { return [] }
        return e
    }

    // Un python3 con TLS 1.3 (quello di sistema, con LibreSSL vecchio, non basta: il client parla solo TLS 1.3).
    static func python() -> String? {
        let path = (ProcessInfo.processInfo.environment["PATH"] ?? "").split(separator: ":").map { "\($0)/python3" }
        for c in path + ["/opt/homebrew/bin/python3", "/usr/local/bin/python3", "/usr/bin/python3"]
        where FileManager.default.isExecutableFile(atPath: c) {
            let p = Process()
            p.executableURL = URL(fileURLWithPath: c)
            p.arguments = ["-I", "-c", "import ssl,sys; sys.exit(0 if ssl.HAS_TLSv1_3 else 1)"]
            p.standardOutput = FileHandle.nullDevice
            p.standardError = FileHandle.nullDevice
            if (try? p.run()) != nil { p.waitUntilExit(); if p.terminationStatus == 0 { return c } }
        }
        return nil
    }

    static func run(_ exe: String, _ args: [String]) throws {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: exe)
        p.arguments = args
        p.standardOutput = FileHandle.nullDevice
        p.standardError = FileHandle.nullDevice
        try p.run()
        p.waitUntilExit()
        if p.terminationStatus != 0 { throw StartError.notStarted }
    }

    static let script = #"""
import http.server, json, ssl, sys, threading, os
d = sys.argv[1]
events = []
lock = threading.Lock()
def log(e):
    with lock:
        events.append(e)
        open(os.path.join(d, "events.json.tmp"), "w").write(json.dumps(events))
        os.replace(os.path.join(d, "events.json.tmp"), os.path.join(d, "events.json"))
class Plain(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        log("plain GET " + self.path); self.send_response(200); self.send_header("Content-Length", "2"); self.end_headers(); self.wfile.write(b"{}")
    do_POST = do_GET
    def log_message(self, *a): pass
class Tls(http.server.BaseHTTPRequestHandler):
    def handle_any(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n: self.rfile.read(n)
        log("tls %s %s auth=%s" % (self.command, self.path, self.headers.get("Authorization") or "-"))
        if self.path.startswith("/redir/"):
            code = int(self.path.split("/")[2])
            self.send_response(code); self.send_header("Location", "http://127.0.0.1:%d/leak" % plain.server_port)
            self.send_header("Content-Length", "0"); self.end_headers(); return
        body = json.dumps({"token": "ab" * 32}).encode()
        self.send_response(200); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
    do_GET = handle_any
    do_POST = handle_any
    def log_message(self, *a): pass
plain = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Plain)
tls = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Tls)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.minimum_version = ssl.TLSVersion.TLSv1_3
ctx.load_cert_chain(os.path.join(d, "c.pem"), os.path.join(d, "k.pem"))
tls.socket = ctx.wrap_socket(tls.socket, server_side=True)
threading.Thread(target=plain.serve_forever, daemon=True).start()
threading.Thread(target=tls.serve_forever, daemon=True).start()
open(os.path.join(d, "ports.json"), "w").write(json.dumps({"tls": tls.server_port, "plain": plain.server_port}))
sys.stdin.read()  # esce a stdin chiuso
"""#
}
