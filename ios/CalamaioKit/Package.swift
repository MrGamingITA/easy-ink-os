// swift-tools-version: 6.0
// CalamaioKit: la logica dell'app iPhone, provata con `swift test` sul Mac e nel simulatore.
import PackageDescription

let package = Package(
    name: "CalamaioKit",
    platforms: [.iOS("27.0"), .macOS("27.0")],
    products: [.library(name: "CalamaioKit", targets: ["CalamaioKit"])],
    targets: [
        .target(name: "CalamaioKit", resources: [.process("Resources")]),
        .testTarget(name: "CalamaioKitTests", dependencies: ["CalamaioKit"]),
    ],
    swiftLanguageModes: [.v5]
)
