import SwiftUI
import CalamaioKit

/// Scheda «Controller» (spec §2.4): in alto lo stato del collegamento, sotto la pulsantiera.
struct ControllerTab: View {
    @EnvironmentObject var pad: PadController
    @Environment(\.scenePhase) private var scenePhase
    @State private var releaseToken = 0

    var body: some View {
        GeometryReader { geo in
            let layout = PadLayout(size: geo.size)
            ZStack(alignment: .top) {
                ForEach(PadLayout.Key.allCases, id: \.self) { key in
                    keyView(key, layout: layout)
                }
                VStack(spacing: 6) {
                    Text(Texts.padStatus(pad.status))
                        .font(.headline)
                        .multilineTextAlignment(.center)
                        .accessibilityIdentifier("pad-status")
                    if pad.status != .connected {
                        Text(Texts.padFirstTime).font(.footnote).foregroundStyle(.secondary).multilineTextAlignment(.center)
                    }
                }
                .padding(.horizontal, 16)
                .padding(.top, layout.portrait ? 24 : 8)
                .allowsHitTesting(false)
                PadTouchSurface(layout: layout, releaseToken: releaseToken) { pad.setButtons($0) }
            }
        }
        .ignoresSafeArea(.keyboard)
        // M1: le dita si dimenticano fuori dal primo piano, lasciando la scheda e a ogni rilascio del controller
        // (collegamento perso, Bluetooth giù); touchesCancelled lo fa la vista
        .onChange(of: scenePhase) { _, phase in if phase != .active { release() } }
        .onChange(of: pad.releases) { _, _ in release() }
        .onDisappear { release() }
    }

    private func release() {
        releaseToken &+= 1
        pad.setButtons([])
    }

    @ViewBuilder private func keyView(_ key: PadLayout.Key, layout: PadLayout) -> some View {
        let f = layout.frames[key] ?? .zero
        let on = isOn(key)
        Group {
            switch key {
            case .dpad: DpadShape(pressed: pad.buttons.intersection(.dpad))
            case .a, .b:
                Circle().fill(on ? Color.primary : Color.secondary.opacity(0.25))
                    .overlay(Text(label(key)).font(.title.bold()).foregroundStyle(on ? Color(.systemBackground) : .primary))
            case .select, .start, .guide:
                Capsule().fill(on ? Color.primary : Color.secondary.opacity(0.25))
                    .overlay(Text(label(key)).font(.caption.bold()).foregroundStyle(on ? Color(.systemBackground) : .primary))
            }
        }
        .frame(width: f.width, height: f.height)
        .position(x: f.midX, y: f.midY)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(label(key))
        .accessibilityIdentifier("pad-\(id(key))")
        .allowsHitTesting(false)
    }

    private func isOn(_ k: PadLayout.Key) -> Bool {
        switch k {
        case .a: return pad.buttons.contains(.a)
        case .b: return pad.buttons.contains(.b)
        case .select: return pad.buttons.contains(.view)
        case .start: return pad.buttons.contains(.menu)
        case .guide: return pad.buttons.contains(.guide)
        case .dpad: return !pad.buttons.intersection(.dpad).isEmpty
        }
    }

    private func label(_ k: PadLayout.Key) -> String {
        switch k {
        case .a: return Texts.padKeyA
        case .b: return Texts.padKeyB
        case .select: return Texts.padKeySelect
        case .start: return Texts.padKeyStart
        case .guide: return Texts.padKeyGuide
        case .dpad: return Texts.padKeyDpad
        }
    }

    private func id(_ k: PadLayout.Key) -> String {
        switch k {
        case .a: return "a"
        case .b: return "b"
        case .select: return "select"
        case .start: return "start"
        case .guide: return "guide"
        case .dpad: return "dpad"
        }
    }
}

/// Croce disegnata: le quattro braccia, piene quelle premute.
private struct DpadShape: View {
    let pressed: PadButtons
    var body: some View {
        GeometryReader { g in
            let s = min(g.size.width, g.size.height), k = s / 3
            ZStack {
                arm(.up, x: s / 2, y: k / 2, w: k, h: k)
                arm(.down, x: s / 2, y: s - k / 2, w: k, h: k)
                arm(.left, x: k / 2, y: s / 2, w: k, h: k)
                arm(.right, x: s - k / 2, y: s / 2, w: k, h: k)
                Rectangle().fill(Color.secondary.opacity(0.25)).frame(width: k, height: k).position(x: s / 2, y: s / 2)
            }
        }
    }
    private func arm(_ b: PadButtons, x: CGFloat, y: CGFloat, w: CGFloat, h: CGFloat) -> some View {
        RoundedRectangle(cornerRadius: 6).fill(pressed.contains(b) ? Color.primary : Color.secondary.opacity(0.25))
            .frame(width: w, height: h).position(x: x, y: y)
    }
}
