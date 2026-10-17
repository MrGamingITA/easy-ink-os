package m5

// Mac dei messaggi dell'aggiornamento remoto (docs/OTA.md §2-§3, docs/PROTOCOLLO.md «OTA»). Stessa
// forma dei mac dei permessi: etichetta e campi di testo separati da 0x1F, HMAC-SHA256 con la chiave di
// sessione SK troncato a 16 byte.

// OtaKeyMac = HMAC(SK, "ota-key" ␟ pub), pub = chiave pubblica Ed25519 in esadecimale minuscolo.
func OtaKeyMac(SK []byte, pubHex string) []byte { return Mac16(SK, joined("ota-key", pubHex)) }

// OtaOfferMac = HMAC(SK, "ota-offer" ␟ id ␟ sha256), sha256 dell'immagine in esadecimale minuscolo.
func OtaOfferMac(SK []byte, id, sha256Hex string) []byte {
	return Mac16(SK, joined("ota-offer", id, sha256Hex))
}

// OtaCancelMac = HMAC(SK, "ota-cancel" ␟ id): il servizio annulla un aggiornamento prima di «scrivo».
func OtaCancelMac(SK []byte, id string) []byte { return Mac16(SK, joined("ota-cancel", id)) }
