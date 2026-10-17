// Esito della prova del Task 7 (piano app iPhone P1-P2) su iOS 27: cosa riceve un WKURLSchemeHandler.
// Lo controllano i test della prova (se iOS cambia comportamento il test fallisce) e lo leggono
// SchemeHandler/WebContainer per scegliere i rami.
public enum SpikeOutcome {
    /// fetch con corpo (JSON, ArrayBuffer, Blob, FormData) arriva intero al gestore dello schema.
    public static let bodiesReachHandler = false
    /// un EventSource sullo schema proprio riceve gli eventi mentre arrivano (non a fine flusso).
    public static let sseStreams = true
}
