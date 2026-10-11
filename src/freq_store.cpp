// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_STORE
#define LOG_MODULE_TAG "STORE"
#include "freq_store.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstdio>

#include "config.h"
#include "log_config.h"

namespace {

constexpr char kNamespace[] = "m5rajiru";

Preferences s_prefs;
bool s_dirty[kFreqStoreMaxBands];
uint32_t s_pendingKhz[kFreqStoreMaxBands];
uint32_t s_lastChangeMs = 0;

// キー名 "fq0", "fq1", ...
void keyFor(int band, char (&key)[8]) { snprintf(key, sizeof(key), "fq%d", band); }

}  // namespace

uint32_t freqStoreLoad(int band, uint32_t defaultKhz) {
  if (band < 0 || band >= kFreqStoreMaxBands) return defaultKhz;
  if (s_dirty[band]) return s_pendingKhz[band];  // 未書き込みの変更を優先

  char key[8];
  keyFor(band, key);
  s_prefs.begin(kNamespace, /*readOnly=*/true);
  const uint32_t khz = s_prefs.getUInt(key, 0);
  s_prefs.end();
  if (khz == 0) return defaultKhz;
  LOGI("restored band %d: %lu kHz", band, static_cast<unsigned long>(khz));
  return khz;
}

void freqStoreNotifyChanged(int band, uint32_t khz) {
  if (band < 0 || band >= kFreqStoreMaxBands) return;
  s_pendingKhz[band] = khz;
  s_dirty[band] = true;
  s_lastChangeMs = millis();
}

void freqStoreTick() {
  bool any = false;
  for (int i = 0; i < kFreqStoreMaxBands; ++i) any = any || s_dirty[i];
  if (!any) return;
  if (millis() - s_lastChangeMs < config::kFreqSaveDebounceMs) return;

  s_prefs.begin(kNamespace, /*readOnly=*/false);
  for (int i = 0; i < kFreqStoreMaxBands; ++i) {
    if (!s_dirty[i]) continue;
    char key[8];
    keyFor(i, key);
    s_prefs.putUInt(key, s_pendingKhz[i]);
    s_dirty[i] = false;
    LOGI("saved band %d: %lu kHz", i, static_cast<unsigned long>(s_pendingKhz[i]));
  }
  s_prefs.end();
}
