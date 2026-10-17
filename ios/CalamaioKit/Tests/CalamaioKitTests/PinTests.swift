import XCTest
import Security
@testable import CalamaioKit

final class PinTests: XCTestCase {
    func testFingerprintMatchesService() throws {
        let v = try Vectors.load()
        let der = try XCTUnwrap(Data(base64Encoded: v.cert.der_b64))
        XCTAssertEqual(Pin.fingerprint(der: der), v.cert.sha256)
        XCTAssertTrue(Pin.matches(der: der, pin: v.cert.sha256))
        XCTAssertFalse(Pin.matches(der: der, pin: String(repeating: "0", count: 64)))
        XCTAssertFalse(Pin.matches(der: der, pin: ""))
        var other = der
        other[other.count - 1] ^= 1
        XCTAssertFalse(Pin.matches(der: other, pin: v.cert.sha256))
    }

    func testLeafFromTrust() throws {
        let v = try Vectors.load()
        let der = try XCTUnwrap(Data(base64Encoded: v.cert.der_b64))
        let cert = try XCTUnwrap(SecCertificateCreateWithData(nil, der as CFData))
        var trust: SecTrust?
        XCTAssertEqual(SecTrustCreateWithCertificates(cert, SecPolicyCreateBasicX509(), &trust), errSecSuccess)
        XCTAssertEqual(Pin.leafDER(try XCTUnwrap(trust)), der)
    }

    // Se una sottoclasse aggiungesse il metodo di sessione, la sfida TLS arriverebbe lì e non al delegato di
    // richiesta: l'esito dell'impronta per richiesta andrebbe perso. Task 10: ripetere questo controllo sulla sottoclasse.
    func testNoSessionLevelChallengeMethod() {
        let sel = #selector(URLSessionDelegate.urlSession(_:didReceive:completionHandler:))
        XCTAssertFalse(PinnedDelegate(pin: "").responds(to: sel))
        XCTAssertTrue(PinnedDelegate(pin: "").responds(to: #selector(URLSessionTaskDelegate.urlSession(_:task:didReceive:completionHandler:))))
    }
}

// Stesso controllo sui delegati del gestore dello schema (Task 10).
final class TransportDelegateTests: XCTestCase {
    func testTransportDelegatesHaveNoSessionLevelChallenge() {
        let sel = #selector(URLSessionDelegate.urlSession(_:didReceive:completionHandler:))
        let task = #selector(URLSessionTaskDelegate.urlSession(_:task:didReceive:completionHandler:))
        let redirect = #selector(URLSessionTaskDelegate.urlSession(_:task:willPerformHTTPRedirection:newRequest:completionHandler:))
        let transport = URLSessionTransport(pin: "")
        let per = transport.taskDelegate(onResponse: { _ in }, onData: { _ in }, onDone: { _ in })
        for d in [transport as NSObject, per] {
            XCTAssertFalse(d.responds(to: sel), "\(type(of: d))")
            XCTAssertTrue(d.responds(to: task), "\(type(of: d))")
            XCTAssertTrue(d.responds(to: redirect), "\(type(of: d))")
        }
    }
}
