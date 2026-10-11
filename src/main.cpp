// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_MAIN
#define LOG_MODULE_TAG "MAIN"
#include <Arduino.h>
#include <WiFi.h>

#include "aac_player.h"
#include "channel_list.h"
#include "credentials.h"
#include "es8388.h"
#include "i2s_speaker.h"
#include "log_config.h"
#include "mode_manager.h"
#include "morse_code.h"
#include "panel.h"
#include "radio_stream.h"
#include "time_sync.h"
#include "wifi_manager.h"

// NHK R1(ラジオ第1 = NHKAM 東京)を再生する。直接選局コードは1005。
namespace {

constexpr int kRssiWarnDbm = -70;                 // これ未満でログを出す
constexpr size_t kBufferedWarnBytes = 15 * 1024;  // これ未満でログを出す
constexpr uint8_t kOutVolume = 0x01;              // ES8388 LOUT/ROUT。0x1Eで0dB

void fatalHalt() {
  morsePlay("err");  // fatal halt indicator
  while (true) {
    delay(1000);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(700);
  logInit();
  LOGI("setup start, PSRAM=%u bytes",
       static_cast<unsigned>(ESP.getPsramSize()));

  // I2Sを先に起動してMCLKを出してから、コーデックのレジスタを書く。
  i2s_chan_handle_t tx = es8388CreateI2sTxChannel();
  if (tx == nullptr || !i2sSpeakerBegin(tx)) {
    LOGE("I2S speaker init failed");
    fatalHalt();
  }
  delay(100);
  if (!es8388Begin(kOutVolume)) {
    LOGE("ES8388 codec init failed (I2C error?)");
    fatalHalt();
  }
  es8388SetAmpEnabled(true);
  LOGI("speaker ready, free heap=%u", static_cast<unsigned>(ESP.getFreeHeap()));
  morseBegin();
  morsePlay("p");

  // Helixのバッファはヒープから取るので、Wi-Fi/TLSより前(ヒープが一番空いている間)に確保する。
  if (!aacPlayerBegin()) {
    LOGE("AAC decoder init failed");
    fatalHalt();
  }

  if (!credentialsBegin()) {
    LOGE("credentials init failed (NVS error?)");
    fatalHalt();
  }
  if (credentialsWifiSsid()[0] == '\0') {
    LOGE("Wi-Fi SSID is empty. Set WIFI_SSID/WIFI_PASSWORD in src/secrets.h");
    fatalHalt();
  }

  if (!panelBegin()) {
    LOGE("panel init failed (continuing without all switches)");
  }

  // Wi-Fi/TLSがヒープを使い始める前に、大きなバッファを確保しておく。
  if (!radioStreamReserveMemory()) {
    fatalHalt();
  }
  LOGI("buffers reserved, free heap=%u, largest block=%u",
       static_cast<unsigned>(ESP.getFreeHeap()),
       static_cast<unsigned>(ESP.getMaxAllocHeap()));

  wifiBegin();
  LOGI("wifi ready, free heap=%u", static_cast<unsigned>(ESP.getFreeHeap()));
  timeSyncBegin();

  // SP6Tロータリーの位置に対応するモードへ入る(インターネットラジオならここで再生開始)。
  modeManagerBegin();
}

void loop() {
  PanelEvent ev;
  while (panelPoll(ev)) {
    LOGI("panel event type=%u id=%d value=%d", static_cast<unsigned>(ev.type),
         static_cast<unsigned>(ev.id), static_cast<int>(ev.value));
    modeManagerHandleEvent(ev);
  }
  modeManagerTick();

  delay(50);
}
