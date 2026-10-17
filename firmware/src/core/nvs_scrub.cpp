#include "nvs_scrub.h"

#include <string.h>

#include "crypto.h"
#include "strbuf.h"

namespace cal {

size_t nvsIntSize(NvsType t) {
  switch (t) {
    case NvsType::U8: case NvsType::I8: return 1;
    case NvsType::U16: case NvsType::I16: return 2;
    case NvsType::U32: case NvsType::I32: return 4;
    case NvsType::U64: case NvsType::I64: return 8;
    default: return 0;
  }
}

const char *nvsTypeKey(NvsType t) {
  switch (t) {
    case NvsType::U8: return "u8";
    case NvsType::I8: return "i8";
    case NvsType::U16: return "u16";
    case NvsType::I16: return "i16";
    case NvsType::U32: return "u32";
    case NvsType::I32: return "i32";
    case NvsType::U64: return "u64";
    case NvsType::I64: return "i64";
    case NvsType::Str: return "stringa";
    case NvsType::Blob: return "blob";
  }
  return "?";
}

const char *scrubOutcomeText(ScrubOutcome o) {
  switch (o) {
    case ScrubOutcome::Done: return "fatta";
    case ScrubOutcome::ListFailed: return "elenco non riuscito";
    case ScrubOutcome::TooMany: return "troppe voci";
    case ScrubOutcome::TooBig: return "oltre il limite di sicurezza";
    case ScrubOutcome::ReadFailed: return "lettura non riuscita";
    case ScrubOutcome::EraseFailed: return "cancellazione fisica non riuscita";
    case ScrubOutcome::WriteFailed: return "riscrittura non riuscita";
  }
  return "?";
}

int countOccurrences(const uint8_t *hay, size_t n, const uint8_t *needle, size_t m) {
  if (!m || m > n) return 0;
  int c = 0;
  for (size_t i = 0; i + m <= n; i++)
    if (hay[i] == needle[0] && memcmp(hay + i, needle, m) == 0) c++;
  return c;
}

namespace {

bool sameEntry(const NvsItem &a, const NvsItem &b) { return !strcmp(a.ns, b.ns) && !strcmp(a.key, b.key); }

bool droppedNs(const ScrubConfig &c, const char *ns) {
  for (int i = 0; i < c.nDropNs; i++)
    if (c.dropNs[i] && !strcmp(c.dropNs[i], ns)) return true;
  return false;
}

// Rilettura dopo la riscrittura: stesse voci (né una in più né una in meno), stessi tipi, stessi valori.
// tail: spazio libero della tabella; scratch: spazio libero dell'arena (almeno il valore più lungo).
bool verify(NvsPort &port, const NvsItem *kept, int nKept, const uint8_t *arena, NvsItem *tail, int tailCap,
            uint8_t *scratch, size_t scratchCap, Sb &L) {
  const int n = port.list(tail, tailCap);
  if (n != nKept) { L.s("rilettura: ").i(n).s(" voci invece di ").i(nKept).s(". "); return false; }
  if (n <= tailCap)
    for (int i = 0; i < n; i++) {
      bool found = false;
      for (int k = 0; k < nKept && !found; k++) found = sameEntry(tail[i], kept[k]) && tail[i].type == kept[k].type;
      if (!found) { L.s("rilettura: voce inattesa ").s(tail[i].ns).s("/").s(tail[i].key).s(". "); return false; }
    }
  for (int k = 0; k < nKept; k++) {
    size_t sz = 0;
    const bool ok = port.size(kept[k], &sz) && sz == kept[k].len && sz <= scratchCap && port.read(kept[k], scratch, sz) &&
                    memcmp(scratch, arena + kept[k].off, sz) == 0;
    cry::wipe(scratch, sz <= scratchCap ? sz : scratchCap);
    if (!ok) { L.s("rilettura diversa: ").s(kept[k].ns).s("/").s(kept[k].key).s(". "); return false; }
  }
  return true;
}

}  // namespace

ScrubReport scrubNvs(NvsPort &port, const ScrubConfig &cfg, NvsItem *items, int itemCap, uint8_t *arena,
                     size_t arenaCap, char *log, size_t logCap) {
  ScrubReport r{ScrubOutcome::Done, 0, 0, 0, 0, 0};
  Sb L(log, logCap);
  const int n = port.list(items, itemCap);
  if (n < 0) { r.outcome = ScrubOutcome::ListFailed; L.s("elenco delle voci non riuscito: niente cancellato"); return r; }
  r.found = n;
  if (n > itemCap) {
    r.outcome = ScrubOutcome::TooMany;
    L.i(n).s(" voci, tabella da ").i(itemCap).s(": niente cancellato");
    return r;
  }
  // 1. Copia in RAM. Le voci da riscrivere si compattano in testa alla tabella.
  int kept = 0;
  size_t used = 0, longest = 0;
  for (int i = 0; i < n; i++) {
    NvsItem it = items[i];
    if (droppedNs(cfg, it.ns)) { r.dropped++; continue; }
    bool dup = false;
    for (int k = 0; k < kept && !dup; k++) dup = sameEntry(items[k], it);
    if (dup) continue;  // l'iteratore non dovrebbe ripetere una voce: si tiene la prima copia
    size_t sz = 0;
    if (!port.size(it, &sz) || (nvsIntSize(it.type) && sz != nvsIntSize(it.type))) {
      r.outcome = ScrubOutcome::ReadFailed;
      L.s("misura di ").s(it.ns).s("/").s(it.key).s(" (").s(nvsTypeKey(it.type)).s(") non riuscita: niente cancellato");
      break;
    }
    if (sz > cfg.maxBytes - used || sz > arenaCap - used) {
      r.outcome = ScrubOutcome::TooBig;
      L.s("i valori superano ").i(static_cast<long long>(cfg.maxBytes)).s(" byte (a ").s(it.ns).s("/").s(it.key)
          .s("): niente cancellato");
      break;
    }
    if (!port.read(it, arena + used, sz)) {
      r.outcome = ScrubOutcome::ReadFailed;
      L.s("lettura di ").s(it.ns).s("/").s(it.key).s(" (").s(nvsTypeKey(it.type)).s(") non riuscita: niente cancellato");
      break;
    }
    it.len = static_cast<uint32_t>(sz);
    it.off = static_cast<uint32_t>(used);
    used += sz;
    if (sz > longest) longest = sz;
    items[kept++] = it;
  }
  // La rilettura di verifica ha bisogno del valore più lungo nell'arena libera: se non c'è, non si parte.
  if (r.outcome == ScrubOutcome::Done && longest > arenaCap - used) {
    r.outcome = ScrubOutcome::TooBig;
    L.s("arena troppo piccola per la rilettura (").i(static_cast<long long>(longest)).s(" byte): niente cancellato");
  }
  r.kept = kept;
  r.bytes = used;
  if (r.outcome != ScrubOutcome::Done) {
    cry::wipe(arena, used);
    return r;
  }
  // 2. Cancellazione fisica, riscrittura, rilettura. Un secondo giro se la rilettura non torna:
  //    i valori sono ancora nell'arena.
  bool erased = false, ok = false;
  for (int attempt = 1; attempt <= 2 && !ok; attempt++) {
    r.attempts = attempt;
    erased = port.erasePartition();
    if (!erased) L.s("cancellazione fisica non riuscita (giro ").i(attempt).s("). ");
    ok = true;
    for (int k = 0; k < kept; k++) {
      if (!port.write(items[k], arena + items[k].off)) {
        L.s("scrittura di ").s(items[k].ns).s("/").s(items[k].key).s(" non riuscita. ");
        ok = false;
      }
    }
    if (!port.commit()) { L.s("commit non riuscito. "); ok = false; }
    ok = ok && verify(port, items, kept, arena, items + kept, itemCap - kept, arena + used, arenaCap - used, L);
  }
  cry::wipe(arena, arenaCap);
  if (!ok) {
    r.outcome = ScrubOutcome::WriteFailed;
    L.s("ERRORE: voci non riscritte dopo ").i(r.attempts).s(" giri");
  } else if (!erased) {
    r.outcome = ScrubOutcome::EraseFailed;
    L.s("voci riscritte e rilette, ma la cancellazione fisica non è riuscita");
  } else {
    L.i(kept).s(" voci riscritte e rilette uguali");
  }
  return r;
}

}  // namespace cal
