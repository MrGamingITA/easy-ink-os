// Vedi nvs_idf.h. API dell'IDF 4.4 (core Arduino 2.0.17), dagli header del pacchetto:
//   nvs_flash/include/nvs.h        nvs_entry_find (namespace NULL = tutti), nvs_entry_next, nvs_entry_info,
//                                  nvs_get_stats, nvs_get_* / nvs_set_*, nvs_commit
//   nvs_flash/include/nvs_flash.h  nvs_flash_deinit, nvs_flash_erase_partition («se la partizione è
//                                  inizializzata, prima la de-inizializza»), nvs_flash_init
//   spi_flash/include/esp_partition.h  esp_partition_find_first, esp_partition_read
#ifdef ARDUINO
#include "hw/nvs_idf.h"

#include <Arduino.h>
#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdlib.h>
#include <string.h>

#include "core/crypto.h"
#include "hw/logtee.h"

namespace nvsidf {

using cal::NvsItem;
using cal::NvsType;

bool Port::open(const char *ns, bool rw) {
  if (open_ && rw_ == rw && !strcmp(ns_, ns)) return true;
  if (!close()) return false;
  nvs_handle_t h;
  const esp_err_t e = nvs_open(ns, rw ? NVS_READWRITE : NVS_READONLY, &h);
  if (e != ESP_OK) { LogSerial.printf("[chiavi] pulizia NVS: nvs_open «%s» %s\n", ns, esp_err_to_name(e)); return false; }
  h_ = h;
  open_ = true;
  rw_ = rw;
  strncpy(ns_, ns, sizeof ns_ - 1);
  ns_[sizeof ns_ - 1] = 0;
  return true;
}

bool Port::close() {
  if (!open_) return true;
  esp_err_t e = ESP_OK;
  if (rw_) e = nvs_commit(h_);
  nvs_close(h_);
  open_ = false;
  if (e != ESP_OK) LogSerial.printf("[chiavi] pulizia NVS: nvs_commit «%s» %s\n", ns_, esp_err_to_name(e));
  return e == ESP_OK;
}

int Port::list(NvsItem *out, int cap) {
  close();
  nvs_stats_t st;
  if (nvs_get_stats(NVS_DEFAULT_PART_NAME, &st) != ESP_OK) return -1;  // NVS non inizializzata: mai «vuota»
  int n = 0;
  nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, nullptr, NVS_TYPE_ANY);
  while (it) {
    nvs_entry_info_t e;
    nvs_entry_info(it, &e);
    if (n < cap) {
      memset(&out[n], 0, sizeof out[n]);
      strncpy(out[n].ns, e.namespace_name, sizeof out[n].ns - 1);
      strncpy(out[n].key, e.key, sizeof out[n].key - 1);
      out[n].type = static_cast<NvsType>(e.type);
    }
    n++;
    it = nvs_entry_next(it);  // NULL alla fine: niente da rilasciare
  }
  // Nessuna voce trovata ma voci occupate oltre i nomi dei namespace: l'iteratore non ha visto tutto.
  if (!n && st.used_entries > st.namespace_count) {
    LogSerial.printf("[chiavi] pulizia NVS: l'iteratore non trova voci ma la NVS ne usa %u (namespace %u)\n",
                  unsigned(st.used_entries), unsigned(st.namespace_count));
    return -1;
  }
  return n;
}

bool Port::size(const NvsItem &it, size_t *n) {
  if (!open(it.ns, false)) return false;
  const size_t isz = cal::nvsIntSize(it.type);
  if (isz) { *n = isz; return true; }
  size_t len = 0;
  esp_err_t e = ESP_ERR_NVS_TYPE_MISMATCH;
  if (it.type == NvsType::Str) e = nvs_get_str(h_, it.key, nullptr, &len);
  else if (it.type == NvsType::Blob) e = nvs_get_blob(h_, it.key, nullptr, &len);
  if (e != ESP_OK) return false;
  *n = len;
  return true;
}

bool Port::read(const NvsItem &it, uint8_t *out, size_t n) {
  if (!open(it.ns, false)) return false;
  esp_err_t e = ESP_ERR_NVS_TYPE_MISMATCH;
  uint64_t v = 0;  // gli interi passano da qui: l'arena non è allineata
  switch (it.type) {
    case NvsType::U8: e = nvs_get_u8(h_, it.key, reinterpret_cast<uint8_t *>(&v)); break;
    case NvsType::I8: e = nvs_get_i8(h_, it.key, reinterpret_cast<int8_t *>(&v)); break;
    case NvsType::U16: e = nvs_get_u16(h_, it.key, reinterpret_cast<uint16_t *>(&v)); break;
    case NvsType::I16: e = nvs_get_i16(h_, it.key, reinterpret_cast<int16_t *>(&v)); break;
    case NvsType::U32: e = nvs_get_u32(h_, it.key, reinterpret_cast<uint32_t *>(&v)); break;
    case NvsType::I32: e = nvs_get_i32(h_, it.key, reinterpret_cast<int32_t *>(&v)); break;
    case NvsType::U64: e = nvs_get_u64(h_, it.key, &v); break;
    case NvsType::I64: e = nvs_get_i64(h_, it.key, reinterpret_cast<int64_t *>(&v)); break;
    case NvsType::Str: {
      size_t len = n;
      e = nvs_get_str(h_, it.key, reinterpret_cast<char *>(out), &len);
      if (e == ESP_OK && len != n) e = ESP_ERR_NVS_INVALID_LENGTH;
      return e == ESP_OK;
    }
    case NvsType::Blob: {
      size_t len = n;
      e = nvs_get_blob(h_, it.key, out, &len);
      if (e == ESP_OK && len != n) e = ESP_ERR_NVS_INVALID_LENGTH;
      return e == ESP_OK;
    }
  }
  if (e != ESP_OK || n != cal::nvsIntSize(it.type)) return false;
  memcpy(out, &v, n);  // little endian, come in memoria
  return true;
}

bool Port::erasePartition() {
  close();
  nvs_flash_deinit();  // tutti gli handle diventano non validi (qui non ce ne sono)
  const esp_err_t e = nvs_flash_erase_partition(NVS_DEFAULT_PART_NAME);  // tutti i settori a 0xFF
  const esp_err_t i = nvs_flash_init();
  if (e != ESP_OK || i != ESP_OK)
    LogSerial.printf("[chiavi] pulizia NVS: cancellazione %s, nvs_flash_init %s\n", esp_err_to_name(e), esp_err_to_name(i));
  return e == ESP_OK && i == ESP_OK;
}

bool Port::write(const NvsItem &it, const uint8_t *d) {
  if (!open(it.ns, true)) return false;
  uint64_t v = 0;
  const size_t isz = cal::nvsIntSize(it.type);
  if (isz) {
    if (it.len != isz) return false;
    memcpy(&v, d, isz);
  }
  esp_err_t e = ESP_ERR_NVS_TYPE_MISMATCH;
  switch (it.type) {
    case NvsType::U8: e = nvs_set_u8(h_, it.key, uint8_t(v)); break;
    case NvsType::I8: e = nvs_set_i8(h_, it.key, int8_t(v)); break;
    case NvsType::U16: e = nvs_set_u16(h_, it.key, uint16_t(v)); break;
    case NvsType::I16: e = nvs_set_i16(h_, it.key, int16_t(v)); break;
    case NvsType::U32: e = nvs_set_u32(h_, it.key, uint32_t(v)); break;
    case NvsType::I32: e = nvs_set_i32(h_, it.key, int32_t(v)); break;
    case NvsType::U64: e = nvs_set_u64(h_, it.key, v); break;
    case NvsType::I64: e = nvs_set_i64(h_, it.key, int64_t(v)); break;
    case NvsType::Str:
      // nvs_get_str dà la lunghezza con il terminatore: senza, la copia non è una stringa.
      if (!it.len || d[it.len - 1] != 0) return false;
      e = nvs_set_str(h_, it.key, reinterpret_cast<const char *>(d));
      break;
    case NvsType::Blob: e = nvs_set_blob(h_, it.key, d, it.len); break;
  }
  if (e != ESP_OK)
    LogSerial.printf("[chiavi] pulizia NVS: scrittura %s/%s (%s, %u byte) %s\n", it.ns, it.key, cal::nvsTypeKey(it.type),
                  unsigned(it.len), esp_err_to_name(e));
  return e == ESP_OK;
}

bool Port::commit() { return close(); }

int liveEntries() {
  nvs_stats_t st;
  if (nvs_get_stats(NVS_DEFAULT_PART_NAME, &st) != ESP_OK) return -1;
  int n = 0;
  for (nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, nullptr, NVS_TYPE_ANY); it; it = nvs_entry_next(it)) n++;
  return n;
}

int countInPartition(const uint8_t *needle, size_t n, size_t *partBytes) {
  const esp_partition_t *p =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NVS_DEFAULT_PART_NAME);
  if (!p) return -1;
  if (partBytes) *partBytes = p->size;
  uint8_t *buf = static_cast<uint8_t *>(malloc(p->size));
  if (!buf) return -1;
  int c = -1;
  if (esp_partition_read(p, 0, buf, p->size) == ESP_OK) c = cal::countOccurrences(buf, p->size, needle, n);
  cal::cry::wipe(buf, p->size);
  free(buf);
  return c;
}

}  // namespace nvsidf
#endif  // ARDUINO
