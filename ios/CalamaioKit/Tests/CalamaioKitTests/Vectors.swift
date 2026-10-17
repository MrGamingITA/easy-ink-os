import Foundation
import XCTest

// protocol/vectors/app-pairing.json, scritti dal servizio (service/internal/phones, TestAppPairingVectors).
struct Vectors: Decodable {
    struct Cert: Decodable { let der_b64: String; let sha256: String }
    struct Link: Decodable { let hosts: [String]; let port: Int; let code: String; let name: String; let uri: String }
    let cert: Cert
    let link: Link

    static func load() throws -> Vectors {
        var url = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { url.deleteLastPathComponent() } // …/ios/CalamaioKit/Tests/CalamaioKitTests/Vectors.swift → radice
        let data = try Data(contentsOf: url.appendingPathComponent("protocol/vectors/app-pairing.json"))
        return try JSONDecoder().decode(Vectors.self, from: data)
    }
}
