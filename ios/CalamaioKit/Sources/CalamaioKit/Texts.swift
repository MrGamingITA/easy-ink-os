// Testi dell'app, in italiano. Nuovi rispetto al simulatore della scheda (che non ha schermate dell'app):
// elenco completo in docs/DECISIONI.md, «App iPhone», voce «Testi nuovi dell'app».
public enum Texts {
    public static let notFound = "PC non trovato: telefono e PC devono essere sulla stessa rete"
    public static let revoked = "Il PC ha revocato questo telefono: abbinalo di nuovo da Impostazioni."
    public static let certChanged = "Il PC ha un certificato diverso da quello abbinato: abbina di nuovo il telefono."
    public static let badLink = "Questo codice non è un abbinamento di Calamaio."
    public static let pairPinMismatch = "Il certificato del PC non corrisponde al codice: apri un abbinamento nuovo dalla webapp e riprova."
    public static let pairRejected = "Abbinamento rifiutato dal PC: "
    public static let keychain = "Il token non si è salvato nel Portachiavi: riprova."
    public static let notPaired = "Nessun PC abbinato. Sul PC apri la webapp, Dispositivo › Telefoni › «Abbina un telefono», e inquadra il codice."
    public static let searching = "Cerco il PC…"
    public static let pcUnreachable = "PC non raggiungibile"
    public static let tooLarge = "File troppo grande per l'app (al massimo 16 MiB)."
    public static let cancelled = "Abbinamento annullato."
    public static let searchStopped = "Ricerca del PC interrotta: premi «Riprova»."
    public static let busy = "Troppe richieste in corso verso il PC: riprova tra poco."
    public static let responseTooLarge = "Risposta del PC troppo grande per l'app."
    // conferma per i link esterni (titolo; il messaggio è il nome dell'host)
    public static let openInSafari = "Aprire in Safari?"
    public static let open = "Apri"
    public static let cancel = "Annulla"

    // Scheda Controller (P3)
    public static let padTab = "Controller"
    public static let padConnected = "Collegato alla scheda"
    public static let padSearching = "Cerco la scheda…"
    public static let padOther = "La scheda usa un altro controller: spegnilo per usare il telefono"
    public static let padBluetoothOff = "Bluetooth spento: accendilo dal Centro di Controllo."
    public static let padUnauthorized = "Bluetooth non permesso: consentilo in Impostazioni › Calamaio."
    public static let padUnsupported = "Bluetooth non disponibile su questo dispositivo."
    public static let padPaused = "In pausa"
    public static let padFirstTime = "La prima volta: sulla scheda Menu › Gioco › Controller › «Associa telefono», poi scrivi qui il codice che mostra."
    public static let padHaptics = "Vibrazione al tocco"
    public static let padBluetooth = "Bluetooth"
    // tasti (scritta e nome per VoiceOver; la croce ha solo il nome)
    public static let padKeyA = "A"
    public static let padKeyB = "B"
    public static let padKeySelect = "SELECT"
    public static let padKeyStart = "START"
    public static let padKeyGuide = "GUIDA"
    public static let padKeyDpad = "Croce"

    public static func padStatus(_ s: PadController.Status) -> String {
        switch s {
        case .connected: return padConnected
        case .searching: return padSearching
        case .otherController: return padOther
        case .bluetoothOff: return padBluetoothOff
        case .unauthorized: return padUnauthorized
        case .unsupported: return padUnsupported
        case .paused: return padPaused
        }
    }
}
