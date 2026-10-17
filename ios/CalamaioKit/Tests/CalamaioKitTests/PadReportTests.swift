import Foundation
import XCTest
@testable import CalamaioKit

// protocol/vectors/pad-report-v1.json (Node, gen-pad-vectors.mjs): gli stessi che legge la scheda (test_pad_phone).
private struct PadVectors: Decodable {
    struct Case: Decodable {
        let name: String
        let hex: String
        let valid: Bool
        let encode: Bool?
        let seq: Int?
        let buttons: Int?
        let lx: Int?
        let ly: Int?
        let fg: Bool?
    }
    let format: String
    let length: Int
    let bits: [String: Int]
    let cases: [Case]

    static func load() throws -> PadVectors {
        var url = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { url.deleteLastPathComponent() }  // …/ios/CalamaioKit/Tests/CalamaioKitTests/ → radice
        let data = try Data(contentsOf: url.appendingPathComponent("protocol/vectors/pad-report-v1.json"))
        return try JSONDecoder().decode(PadVectors.self, from: data)
    }
}

private func hex(_ d: Data) -> String { d.map { String(format: "%02x", $0) }.joined() }
private func bytes(_ s: String) -> Data {
    var d = Data()
    var i = s.startIndex
    while i < s.endIndex, let j = s.index(i, offsetBy: 2, limitedBy: s.endIndex) {
        d.append(UInt8(s[i..<j], radix: 16)!)
        i = j
    }
    return d
}

final class PadReportTests: XCTestCase {
    func testVectorsLoaded() throws {
        let v = try PadVectors.load()
        XCTAssertEqual(v.format, "calamaio-pad-report")
        XCTAssertEqual(v.length, PadReport.length)
        XCTAssertEqual(v.cases.count, 24)
    }

    func testBitsMatchFirmware() throws {
        let b = try PadVectors.load().bits
        XCTAssertEqual(PadButtons.a.rawValue, 1 << b["A"]!)
        XCTAssertEqual(PadButtons.b.rawValue, 1 << b["B"]!)
        XCTAssertEqual(PadButtons.view.rawValue, 1 << b["View"]!)
        XCTAssertEqual(PadButtons.menu.rawValue, 1 << b["Menu"]!)
        XCTAssertEqual(PadButtons.guide.rawValue, 1 << b["Guide"]!)
        XCTAssertEqual(PadButtons.up.rawValue, 1 << b["Up"]!)
        XCTAssertEqual(PadButtons.down.rawValue, 1 << b["Down"]!)
        XCTAssertEqual(PadButtons.left.rawValue, 1 << b["Left"]!)
        XCTAssertEqual(PadButtons.right.rawValue, 1 << b["Right"]!)
    }

    func testEncodeMatchesVectors() throws {
        var n = 0
        for c in try PadVectors.load().cases where c.valid && c.encode == true {
            let r = PadReport(seq: UInt8(c.seq!), buttons: PadButtons(rawValue: UInt16(c.buttons!)),
                              lx: Int8(c.lx!), ly: Int8(c.ly!), foreground: c.fg!)
            XCTAssertEqual(hex(r.encoded()), c.hex, c.name)
            n += 1
        }
        XCTAssertEqual(n, 11)
    }

    func testDecodeLikeTheBoard() throws {
        for c in try PadVectors.load().cases {
            let r = PadReport.decode(bytes(c.hex))
            if !c.valid { XCTAssertNil(r, c.name); continue }
            XCTAssertEqual(r?.seq, UInt8(c.seq!), c.name)
            XCTAssertEqual(r?.buttons.rawValue, UInt16(c.buttons!), c.name)
            XCTAssertEqual(r.map { Int($0.lx) }, c.lx, c.name)
            XCTAssertEqual(r.map { Int($0.ly) }, c.ly, c.name)
            XCTAssertEqual(r?.foreground, c.fg, c.name)
        }
    }

    func testEncodeClampsSticks() {
        let r = PadReport(seq: 1, buttons: [], lx: 127, ly: -128, foreground: true)
        XCTAssertEqual([UInt8](r.encoded())[4], 100)
        XCTAssertEqual([UInt8](r.encoded())[5], UInt8(bitPattern: -100))
    }

    func testInfoValue() {
        XCTAssertEqual(PadGATT.infoValue(appVersion: "0.2.0"), Data([1]) + Data("0.2.0".utf8))
        let long = PadGATT.infoValue(appVersion: "123456789.123456789")
        XCTAssertEqual(long.count, 16)  // ver + 15 byte
        XCTAssertEqual(long.first, 1)
    }

    func testUUIDs() {
        XCTAssertEqual(PadGATT.service, "526389E4-B789-4225-990D-0465AFB610B2")
        XCTAssertEqual(PadGATT.state, "6A34D990-D046-4834-96B8-0CBDCE4087FA")
        XCTAssertEqual(PadGATT.info, "9D00F77E-BB83-4667-A37F-6FBDB0F0F324")
    }
}
