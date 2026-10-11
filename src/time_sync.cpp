// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_TIME
#define LOG_MODULE_TAG "TIME"
#include "time_sync.h"

#include <Arduino.h>

#include <ctime>

#include "config.h"
#include "log_config.h"

namespace {

// 「時刻が入ったか」の判定。SNTP同期前は1970年付近なので、適当に未来の
// エポック秒(2021-01-01 UTC)を下限にする。
constexpr time_t kMinValidEpoch = 1609459200;

bool systemTimeIsSet() {
  time_t now = time(nullptr);
  return now > kMinValidEpoch;
}

}  // namespace

bool timeSyncBegin() {
  // configTzTime()はTZ環境変数の設定・tzset()・SNTPクライアントの起動をまとめて行う
  // (arduino-esp32提供。内部でesp_sntpを非同期で回す)。
  configTzTime(config::kNtpTimezone, config::kNtpServer1, config::kNtpServer2, config::kNtpServer3);
  LOGI("NTP sync requested (tz=%s, server=%s)", config::kNtpTimezone, config::kNtpServer1);

  uint32_t start = millis();
  while (!systemTimeIsSet()) {
    if (millis() - start >= config::kNtpSyncTimeoutMs) {
      LOGW("NTP sync timed out after %lums; continuing with unset clock",
           static_cast<unsigned long>(config::kNtpSyncTimeoutMs));
      return false;
    }
    delay(config::kNtpSyncPollIntervalMs);
  }

  time_t now = time(nullptr);
  struct tm local{};
  localtime_r(&now, &local);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
  LOGI("NTP sync done in %lums: %s (%s)", static_cast<unsigned long>(millis() - start), buf, config::kNtpTimezone);
  return true;
}

bool timeSyncIsValid() { return systemTimeIsSet(); }
