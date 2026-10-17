// Verifica delle firme Ed25519 (RFC 8032) per l'aggiornamento remoto (docs/OTA.md §2). Solo verifica: la
// scheda non firma mai. Codice portabile, uguale in native e sulla scheda: aritmetica e SHA-512 di
// tweetnacl.c (D. J. Bernstein, B. van Gastel, W. Janssen, T. Lange, P. Schwabe, S. Smetsers, versione
// 20140427, pubblico dominio), la stessa base di X25519 in core/crypto_soft.cpp. Rispetto a
// crypto_sign_open: niente copia del messaggio (SHA-512 incrementale), S ≥ L rifiutato (RFC 8032 §5.1.7,
// TweetNaCl non lo controlla) e nessuno spostamento a sinistra di numeri negativi.
// Vettori: protocol/vectors/ota-vectors.json (RFC 8032 §7.1 e casi negativi, generati con Node/OpenSSL).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {
namespace ed {

const size_t kPub = 32, kSig = 64;

// true solo se sig è una firma valida di msg con la chiave pubblica pk. Una chiave fuori dalla curva o una
// S non canonica danno false. Lenta (~decine di ms in native, stimata sotto 1 s sulla scheda): una volta per offerta.
bool verify(const uint8_t sig[kSig], const uint8_t *msg, size_t n, const uint8_t pk[kPub]);

// SHA-512 (FIPS 180-4), esposta per i test.
void sha512(const uint8_t *d, size_t n, uint8_t out[64]);

}  // namespace ed
}  // namespace cal
