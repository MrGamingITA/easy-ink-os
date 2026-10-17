// Costruzione di stringhe UTF-8 in buffer a capienza fissa, senza allocazioni.
// Se il testo non ci sta si tronca su un confine di carattere (mai a metà di «è» o «·»)
// e truncated() lo segnala: i test lo usano per accorgersi dei buffer troppo corti.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {

class Sb {
 public:
  Sb(char *buf, size_t cap) : b_(buf), cap_(cap), n_(0), trunc_(false) {
    if (cap_) b_[0] = 0;
  }
  Sb &s(const char *t);           // accoda una stringa (nullptr = niente)
  Sb &i(long long v);             // accoda un intero in base 10
  Sb &pad2(long long v);          // come pad2() del simulatore: almeno due cifre
  Sb &ch(char c);
  const char *str() const { return b_; }
  size_t len() const { return n_; }
  bool truncated() const { return trunc_; }

 private:
  char *b_;
  size_t cap_;
  size_t n_;
  bool trunc_;
};

template <size_t N>
inline Sb sb(char (&a)[N]) { return Sb(a, N); }

// Copia con troncamento sicuro per UTF-8. Restituisce false se ha troncato.
bool scopy(char *dst, size_t cap, const char *src);
template <size_t N>
inline bool scopy(char (&dst)[N], const char *src) { return scopy(dst, N, src); }

bool startsWith(const char *s, const char *prefix);
bool streq(const char *a, const char *b);

}  // namespace cal
