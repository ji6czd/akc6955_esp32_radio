// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_WIFI
#define LOG_MODULE_TAG "WIFI"
#include "wifi_manager.h"

#include <WiFi.h>

#include "config.h"
#include "credentials.h"
#include "log_config.h"

namespace {

constexpr size_t kBackoffCount = sizeof(config::kWifiRetryBackoffMs) / sizeof(config::kWifiRetryBackoffMs[0]);

uint32_t backoffForAttempt(int attempt) {
  size_t index = (attempt < 0) ? 0 : static_cast<size_t>(attempt);
  if (index >= kBackoffCount) {
    index = kBackoffCount - 1;
  }
  return config::kWifiRetryBackoffMs[index];
}

void connectBlocking() {
  LOGI("connecting to \"%s\"...", credentialsWifiSsid());
  WiFi.mode(WIFI_STA);
  WiFi.begin(credentialsWifiSsid(), credentialsWifiPassword());

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    uint32_t backoff = backoffForAttempt(attempt);
    delay(backoff);
    if (WiFi.status() == WL_CONNECTED) {
      break;
    }
    LOGD("still connecting (attempt %d)...", attempt + 1);
    WiFi.begin(credentialsWifiSsid(), credentialsWifiPassword());
    attempt++;
  }

  WiFi.setSleep(false);
  LOGI("connected, IP=%s", WiFi.localIP().toString().c_str());
}

uint32_t s_lastAttemptMs = 0;
int s_attemptIndex = 0;

}  // namespace

void wifiBegin() { connectBlocking(); }

bool wifiEnsureConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    s_attemptIndex = 0;
    return true;
  }

  uint32_t now = millis();
  uint32_t backoff = backoffForAttempt(s_attemptIndex);
  if (now - s_lastAttemptMs < backoff) {
    return false;
  }

  LOGW("reconnecting (attempt %d)...", s_attemptIndex + 1);
  WiFi.begin(credentialsWifiSsid(), credentialsWifiPassword());
  s_lastAttemptMs = now;
  s_attemptIndex++;
  return WiFi.status() == WL_CONNECTED;
}

void wifiForceReconnect() {
  LOGW("forcing reconnect...");
  WiFi.disconnect(true);
  delay(200);
  connectBlocking();
  s_attemptIndex = 0;
}
