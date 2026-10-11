// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_HW
#define LOG_MODULE_TAG "PANEL"
#include "panel.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/pulse_cnt.h>
#include <esp_timer.h>

#include "config.h"
#include "log_config.h"
#include "tca6424a.h"

namespace {

// --- 配線テーブル
// ----------------------------------------------------------------- bit = port
// * 8 + pin (tca6424a.h参照)。基板が変わったらここだけ直す。

struct KeyMap {
  uint8_t device;
  uint8_t bit;
  PanelKey key;
};

constexpr KeyMap kKeyMap[] = {
    {0, 9, PanelKey::kTact1},         {0, 11, PanelKey::kTact2},
    {0, 10, PanelKey::kTact3},        {0, 0x6, PanelKey::kTact4},
    {0, 8, PanelKey::kTact5},         {0, 7, PanelKey::kTact6},
    {0, 3, PanelKey::kTact7},         {0, 5, PanelKey::kTact8},
    {0, 4, PanelKey::kTact9},         {0, 0, PanelKey::kTactClear},
    {0, 2, PanelKey::kTact0},         {0, 1, PanelKey::kTactEnter},
    {1, 8, PanelKey::kFunc1},         {1, 11, PanelKey::kFunc2},
    {1, 9, PanelKey::kFunc3},         {1, 12, PanelKey::kFunc4},
    {1, 10, PanelKey::kFunc5},        {1, 13, PanelKey::kFunc6},
    {1, 16, PanelKey::kEncoderPush},  {1, 18, PanelKey::kEncoderPlus},
    {1, 17, PanelKey::kEncoderMinus}, {1, 19, PanelKey::kVoice},
};
constexpr size_t kKeyCount = sizeof(kKeyMap) / sizeof(kKeyMap[0]);

struct RotaryMap {
  uint8_t device;
  uint8_t firstBit;
  uint8_t positions;
  PanelRotary id;
};

// SP12T: TCA#1 P14-P17(bit12-15) + P20-P27(bit16-23) の連続12bit。SP6T: TCA#2
// P00-P05。
constexpr RotaryMap kRotaryMap[] = {
    {0, 12, 12, PanelRotary::kSp12t},
    {1, 0, 6, PanelRotary::kSp6t},
};
constexpr size_t kRotaryCount = sizeof(kRotaryMap) / sizeof(kRotaryMap[0]);

// A相とB相は基板の配線(A_PULSE'=GPIO18,
// B_PULSE'=GPIO19)と逆に割り当てて、回転方向を 合わせている(時計回りで正)。
constexpr int kEncoderPinA = 19;
constexpr int kEncoderPinB = 18;

// --- 状態
// -------------------------------------------------------------------------

struct DeviceState {
  uint32_t stable =
      0;               // 直前に読んだ値(デバウンスは割り込み側の待ち時間で行う)
  bool valid = false;  // 起動時の初期値を取り込み済みか
};

DeviceState s_dev[kTca6424aDeviceCount];
uint32_t s_pressStartMs[kKeyCount];
bool s_longSent[kKeyCount];
volatile int s_rotaryPos[kRotaryCount];

QueueHandle_t s_queue = nullptr;
uint8_t s_queueStorage[config::kPanelQueueDepth * sizeof(PanelEvent)];
StaticQueue_t s_queueStruct;
StackType_t s_taskStack[config::kPanelTaskStackBytes];
StaticTask_t s_taskBuffer;

pcnt_unit_handle_t s_pcnt = nullptr;
int s_lastCount = 0;
int s_remainder = 0;
uint32_t s_dropped = 0;

void push(PanelEventType type, uint8_t id, int8_t value) {
  PanelEvent ev{type, id, value};
  if (xQueueSend(s_queue, &ev, 0) != pdTRUE) {
    ++s_dropped;
    LOGW("event queue full, dropped=%u", static_cast<unsigned>(s_dropped));
  }
}

// ワンホットのビット列を位置へ。0個または複数bit(切替中)は-1。
int rotaryPositionOf(uint32_t bits, const RotaryMap& m) {
  const uint32_t mask = ((1u << m.positions) - 1u) << m.firstBit;
  const uint32_t sel = (bits & mask) >> m.firstBit;
  if (sel == 0 || (sel & (sel - 1)) != 0) return -1;
  return __builtin_ctz(sel);
}

void processDevice(int device, uint32_t bits, bool emit) {
  const uint32_t now = millis();
  DeviceState& d = s_dev[device];

  if (!d.valid) {  // 起動直後: 初期状態を取り込むだけでイベントは出さない
    d.stable = bits;
    d.valid = true;
    for (size_t i = 0; i < kRotaryCount; ++i) {
      if (kRotaryMap[i].device == device)
        s_rotaryPos[i] = rotaryPositionOf(bits, kRotaryMap[i]);
    }
    for (size_t i = 0; i < kKeyCount; ++i) {
      if (kKeyMap[i].device == device && ((bits >> kKeyMap[i].bit) & 1u)) {
        s_pressStartMs[i] = now;
        s_longSent[i] = true;  // 起動時から押されていたキーは長押し扱いにしない
      }
    }
    return;
  }

  // 割り込みから一定時間(kPanelDebounceMs)待って読んだ値なので、そのまま確定とする。
  const uint32_t changed = bits ^ d.stable;
  d.stable = bits;

  for (size_t i = 0; i < kKeyCount; ++i) {
    const KeyMap& k = kKeyMap[i];
    if (k.device != device) continue;
    const bool pressed = (bits >> k.bit) & 1u;
    if ((changed >> k.bit) & 1u) {
      s_longSent[i] = false;
      s_pressStartMs[i] = now;
      if (emit)
        push(pressed ? PanelEventType::kKeyDown : PanelEventType::kKeyUp,
             static_cast<uint8_t>(k.key), 0);
    }
  }

  for (size_t i = 0; i < kRotaryCount; ++i) {
    if (kRotaryMap[i].device != device) continue;
    const int pos = rotaryPositionOf(bits, kRotaryMap[i]);
    if (pos < 0 || pos == s_rotaryPos[i]) continue;  // 切替中は前回値を維持
    s_rotaryPos[i] = pos;
    if (emit)
      push(PanelEventType::kRotarySelect,
           static_cast<uint8_t>(kRotaryMap[i].id), static_cast<int8_t>(pos));
  }
}

// 長押し判定。確定済みの状態と時刻だけを見るのでI2Cは使わない。毎周期呼ぶ。
void checkLongPress() {
  const uint32_t now = millis();
  for (size_t i = 0; i < kKeyCount; ++i) {
    const KeyMap& k = kKeyMap[i];
    const DeviceState& d = s_dev[k.device];
    if (!d.valid || s_longSent[i]) continue;
    if (((d.stable >> k.bit) & 1u) &&
        now - s_pressStartMs[i] >= config::kPanelLongPressMs) {
      s_longSent[i] = true;
      push(PanelEventType::kKeyLongPress, static_cast<uint8_t>(k.key), 0);
    }
  }
}

// ISR(IRAM)から参照するのでDRAMに置く(フラッシュ上の定数はISR中に読めないことがある)。
DRAM_ATTR const int kIntPins[kTca6424aDeviceCount] = {config::kPanelIntPin0,
                                                      config::kPanelIntPin1};

// TCA6424AのINTピン。変化があるとアクティブになり、入力ポートを読むと解除される。
// アクティブ側のレベルは極性を仮定せず、初期状態を読んだ直後(=INT解除後)のレベルの反対とする。
// 両エッジで割り込みを受けるが、ISRは「アクティブになった」エッジだけを記録する
// (読み出しで解除されるときの逆エッジは空読みになるので無視する)。
bool s_intActiveLevel[kTca6424aDeviceCount];
volatile bool s_intFlag[kTca6424aDeviceCount];
volatile uint32_t
    s_intLastMs[kTca6424aDeviceCount];  // 最後のアクティブエッジの時刻

uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

void IRAM_ATTR onTcaInterrupt(void* arg) {
  const size_t dev = reinterpret_cast<uintptr_t>(arg);
  if ((gpio_get_level(static_cast<gpio_num_t>(kIntPins[dev])) != 0) !=
      s_intActiveLevel[dev]) {
    return;
  }
  s_intLastMs[dev] = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  s_intFlag[dev] = true;
}

// INTがあったデバイスだけを、最後のINTから kPanelDebounceMs 待ってから1回読む。
// バウンスでINTが続けて来た間は待ちをやり直す。何も無ければI2Cを使わない。
void pollSwitches() {
  static uint32_t lastErrLogMs = 0;
  for (int dev = 0; dev < kTca6424aDeviceCount; ++dev) {
    if (!s_intFlag[dev]) continue;
    if (nowMs() - s_intLastMs[dev] < config::kPanelDebounceMs) continue;
    s_intFlag[dev] =
        false;  // 読み出しの前に下ろす(読み出し中の変化を取りこぼさない)
    uint32_t bits = 0;
    if (!tca6424aRead(dev, bits)) {  // エラー時は前回値を保持して、やり直す
      const uint32_t now = millis();
      if (now - lastErrLogMs >= 5000) {
        lastErrLogMs = now;
        LOGE("TCA6424A #%d read failed", dev);
      }
      s_intLastMs[dev] = nowMs();
      s_intFlag[dev] = true;
      continue;
    }
    processDevice(dev, bits, true);
    // 読んだ後もINTがアクティブなら、読み出し中に別の変化があった。もう一度読む。
    if ((gpio_get_level(static_cast<gpio_num_t>(kIntPins[dev])) != 0) ==
        s_intActiveLevel[dev]) {
      s_intLastMs[dev] = nowMs();
      s_intFlag[dev] = true;
    }
  }
}

void pollEncoder() {
  if (s_pcnt == nullptr) return;
  int count = 0;
  if (pcnt_unit_get_count(s_pcnt, &count) != ESP_OK) return;
  s_remainder += count - s_lastCount;
  s_lastCount = count;
  // 範囲端に近づく前に0へ戻す(コストは無視できる回転数)。
  if (count > 10000 || count < -10000) {
    pcnt_unit_clear_count(s_pcnt);
    s_lastCount = 0;
  }
  const int detents = s_remainder / config::kPanelEncoderCountsPerDetent;
  if (detents == 0) return;
  s_remainder -= detents * config::kPanelEncoderCountsPerDetent;
  int v = config::kPanelEncoderInvert ? -detents : detents;
  v = v > 127 ? 127 : (v < -127 ? -127 : v);
  push(PanelEventType::kEncoderStep, 0, static_cast<int8_t>(v));
}

bool encoderBegin() {
  pinMode(kEncoderPinA, INPUT_PULLUP);
  pinMode(kEncoderPinB, INPUT_PULLUP);

  pcnt_unit_config_t unitCfg = {};
  unitCfg.high_limit = 20000;
  unitCfg.low_limit = -20000;
  if (pcnt_new_unit(&unitCfg, &s_pcnt) != ESP_OK) return false;

  pcnt_glitch_filter_config_t filter = {.max_glitch_ns = 1000};
  pcnt_unit_set_glitch_filter(s_pcnt, &filter);

  // 標準的な4逓倍直交デコード(A/Bそれぞれのエッジで、相手のレベルで方向を決める)。
  pcnt_chan_config_t chanA = {.edge_gpio_num = kEncoderPinA,
                              .level_gpio_num = kEncoderPinB};
  pcnt_chan_config_t chanB = {.edge_gpio_num = kEncoderPinB,
                              .level_gpio_num = kEncoderPinA};
  pcnt_channel_handle_t chA = nullptr;
  pcnt_channel_handle_t chB = nullptr;
  if (pcnt_new_channel(s_pcnt, &chanA, &chA) != ESP_OK ||
      pcnt_new_channel(s_pcnt, &chanB, &chB) != ESP_OK) {
    return false;
  }
  pcnt_channel_set_edge_action(chA, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                               PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  pcnt_channel_set_level_action(chA, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  pcnt_channel_set_edge_action(chB, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                               PCNT_CHANNEL_EDGE_ACTION_DECREASE);
  pcnt_channel_set_level_action(chB, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

  if (pcnt_unit_enable(s_pcnt) != ESP_OK ||
      pcnt_unit_clear_count(s_pcnt) != ESP_OK ||
      pcnt_unit_start(s_pcnt) != ESP_OK) {
    return false;
  }
  return true;
}

void panelTaskFn(void*) {
  while (true) {
    pollSwitches();
    checkLongPress();
    pollEncoder();
    vTaskDelay(pdMS_TO_TICKS(config::kPanelEncoderPollMs));
  }
}

}  // namespace

bool panelBegin() {
  bool ok = true;
  if (!tca6424aBegin()) {
    LOGE("TCA6424A init failed (continuing without all switches)");
    ok = false;
  }
  if (!encoderBegin()) {
    LOGE("encoder PCNT init failed");
    s_pcnt = nullptr;
    ok = false;
  }
  for (size_t i = 0; i < kRotaryCount; ++i) s_rotaryPos[i] = -1;

  s_queue = xQueueCreateStatic(config::kPanelQueueDepth, sizeof(PanelEvent),
                               s_queueStorage, &s_queueStruct);
  if (s_queue == nullptr) return false;  // 静的確保なので起こらない

  // 初期状態を取り込んでおく(イベントなし)。失敗したデバイスは最初の成功読み出しで取り込む。
  for (int dev = 0; dev < kTca6424aDeviceCount; ++dev) {
    uint32_t bits = 0;
    if (tca6424aRead(dev, bits)) processDevice(dev, bits, false);
  }

  // 初期状態を取り込んだあとでINT割り込みを有効にする。初期読み出しでINTは解除されて
  // いるので、このときのレベルが非アクティブ、その反対がアクティブ(極性は仮定しない)。
  // 初期読み出しに失敗したデバイスは、INT待ちにせず最初の周期で読み直す。
  for (int dev = 0; dev < kTca6424aDeviceCount; ++dev) {
    pinMode(kIntPins[dev], INPUT);
    s_intActiveLevel[dev] = (digitalRead(kIntPins[dev]) == 0);
    LOGI("TCA6424A #%d INT pin %d: idle level=%d", dev, kIntPins[dev],
         !s_intActiveLevel[dev]);
    if (!s_dev[dev].valid) {
      s_intLastMs[dev] = nowMs();
      s_intFlag[dev] = true;
    }
    attachInterruptArg(kIntPins[dev], onTcaInterrupt,
                       reinterpret_cast<void*>(static_cast<uintptr_t>(dev)),
                       CHANGE);
  }

  xTaskCreateStaticPinnedToCore(panelTaskFn, "panel",
                                config::kPanelTaskStackBytes, nullptr,
                                config::kPanelTaskPriority, s_taskStack,
                                &s_taskBuffer, config::kPanelTaskCore);
  return ok;
}

bool panelPoll(PanelEvent& ev) {
  return s_queue != nullptr && xQueueReceive(s_queue, &ev, 0) == pdTRUE;
}

int panelRotaryPosition(PanelRotary which) {
  return s_rotaryPos[static_cast<size_t>(which)];
}
