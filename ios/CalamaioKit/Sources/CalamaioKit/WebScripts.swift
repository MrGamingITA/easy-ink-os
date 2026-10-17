// Cosa l'app aggiunge alla webapp nella WKWebView.
public enum WebScripts {
    // In coda allo user agent: la webapp riconosce l'app da «CalamaioApp/» (spec §5, pagina Firmware nascosta in P4).
    public static func userAgentName(version: String) -> String { "CalamaioApp/" + version }

    // Script iniettati all'inizio di ogni pagina secondo la prova del Task 7. Ramo B2 (SSE che non arriva a pezzi):
    // senza EventSource la webapp passa da sola al polling ogni 2 s (api.js, watchStatus).
    public static func sources(sseStreams: Bool) -> [String] {
        sseStreams ? [] : ["delete window.EventSource;"]
    }
}
