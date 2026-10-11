// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_STORE
#define LOG_MODULE_TAG "CRED"
#include "credentials.h"

#include <Arduino.h>
#include <Preferences.h>
#include <nvs.h>

#include <cstring>

#include "log_config.h"

// secrets.h は任意。ローカルに置けばビルド時デフォルトとして使われ、無ければ
// 全項目が空("")で起動する(初回はsetup_portalで設定する)。gitignore済み。
#if defined(__has_include) && __has_include("secrets.h")
#include "secrets.h"
#else
namespace {
constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";
constexpr char RADIKO_ID[] = "";
constexpr char RADIKO_PASSWORD[] = "";
}  // namespace
#endif

namespace {

constexpr char kNamespace[] = "creds";
constexpr char kKeyWifiSsid[] = "wifi_ssid";
constexpr char kKeyWifiPass[] = "wifi_pass";
constexpr char kKeyRadikoId[] = "rdk_id";
constexpr char kKeyRadikoPass[] = "rdk_pass";

// SSIDは最大32バイト+NUL、それ以外はradikoの実値に余裕を見た長さ。
char s_wifiSsid[33] = "";
char s_wifiPass[64] = "";
char s_radikoId[65] = "";
char s_radikoPass[65] = "";

void copyStr(char* dst, size_t dstSize, const char* src) {
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

enum class KeyLoad { kOk, kMissing, kError };

// NVSから1キーを読む。
//   kOk      … 値を読み込んだ(空文字も含む)
//   kMissing … キーが存在しない(初回起動 / 未プロビジョニング)。fallbackを入れる
//   kError   … キーはあるが読めない(NVS異常 / 型不一致 / バッファ不足)
KeyLoad loadKey(nvs_handle_t handle, const char* key, const char* fallback, char* dst, size_t dstSize) {
  size_t len = dstSize;
  esp_err_t err = nvs_get_str(handle, key, dst, &len);
  if (err == ESP_OK) {
    dst[dstSize - 1] = '\0';
    return KeyLoad::kOk;
  }
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    copyStr(dst, dstSize, fallback);
    return KeyLoad::kMissing;
  }
  LOGE("NVS read failed for key \"%s\": %s", key, esp_err_to_name(err));
  return KeyLoad::kError;
}

struct KeySpec {
  const char* key;
  const char* fallback;
  char* dst;
  size_t size;
};

const KeySpec kKeys[] = {
    {kKeyWifiSsid, WIFI_SSID, s_wifiSsid, sizeof(s_wifiSsid)},
    {kKeyWifiPass, WIFI_PASSWORD, s_wifiPass, sizeof(s_wifiPass)},
    {kKeyRadikoId, RADIKO_ID, s_radikoId, sizeof(s_radikoId)},
    {kKeyRadikoPass, RADIKO_PASSWORD, s_radikoPass, sizeof(s_radikoPass)},
};

}  // namespace

bool credentialsBegin() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // namespace自体が無い = 一度も保存していない。全項目を既定値にする(異常ではない)。
    for (const KeySpec& k : kKeys) {
      copyStr(k.dst, k.size, k.fallback);
    }
    LOGI("no stored credentials yet (namespace \"%s\" absent)", kNamespace);
    return true;
  }
  if (err != ESP_OK) {
    LOGE("nvs_open(\"%s\") failed: %s", kNamespace, esp_err_to_name(err));
    return false;
  }

  bool ok = true;
  for (const KeySpec& k : kKeys) {
    if (loadKey(handle, k.key, k.fallback, k.dst, k.size) == KeyLoad::kError) {
      ok = false;  // 全キーを試してからまとめて失敗にする(ログを出し切るため)
    }
  }
  nvs_close(handle);
  if (!ok) {
    LOGE("credentials load failed");
    return false;
  }

  // パスワードはログに出さない。SSIDとradiko IDの有無だけ残す。
  LOGI("loaded: wifi_ssid=\"%s\", radiko_id=%s", s_wifiSsid, s_radikoId[0] ? "set" : "(empty)");
  return true;
}

const char* credentialsWifiSsid() { return s_wifiSsid; }
const char* credentialsWifiPassword() { return s_wifiPass; }
const char* credentialsRadikoId() { return s_radikoId; }
const char* credentialsRadikoPassword() { return s_radikoPass; }

bool credentialsSave(const char* ssid, const char* password, const char* radikoId, const char* radikoPassword) {
  if (ssid == nullptr || ssid[0] == '\0') {
    LOGW("save rejected: empty SSID");
    return false;
  }
  if (password == nullptr) password = "";
  if (radikoId == nullptr) radikoId = "";
  if (radikoPassword == nullptr) radikoPassword = "";

  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
    LOGE("save: NVS open failed for namespace \"%s\"", kNamespace);
    return false;
  }
  bool ok = prefs.putString(kKeyWifiSsid, ssid) == strlen(ssid);
  ok = (prefs.putString(kKeyWifiPass, password) == strlen(password)) && ok;
  ok = (prefs.putString(kKeyRadikoId, radikoId) == strlen(radikoId)) && ok;
  ok = (prefs.putString(kKeyRadikoPass, radikoPassword) == strlen(radikoPassword)) && ok;
  prefs.end();
  if (!ok) {
    LOGE("save: NVS write failed");
    return false;
  }

  copyStr(s_wifiSsid, sizeof(s_wifiSsid), ssid);
  copyStr(s_wifiPass, sizeof(s_wifiPass), password);
  copyStr(s_radikoId, sizeof(s_radikoId), radikoId);
  copyStr(s_radikoPass, sizeof(s_radikoPass), radikoPassword);

  LOGI("saved: wifi_ssid=\"%s\", radiko_id=%s", s_wifiSsid, s_radikoId[0] ? "set" : "(empty)");
  return true;
}
