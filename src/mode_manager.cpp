// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_MAIN
#define LOG_MODULE_TAG "MODE"
#include "mode_manager.h"

#include "akc6955_mode.h"
#include "config.h"
#include "es8388.h"
#include "freq_store.h"
#include "log_config.h"
#include "mode.h"
#include "net_radio_mode.h"
#include "volume_store.h"

namespace {

struct ModeEntry {
  Mode* mode;  // nullptr = 未割り当て位置(無音)
  int variant;
};

// SP6Tロータリーの位置 → モード。
ModeEntry entryForPosition(int position) {
  switch (position) {
    case 0:
    case 1:
    case 2:
      return {&ak6955Mode(), position};
    case 3:
      return {&netRadioMode(), 0};
    default:
      return {nullptr, 0};
  }
}

Mode* s_current = nullptr;
int s_currentVariant = 0;
int s_pendingPosition = -1;  // 未適用のロータリー位置。-1=なし

void changeVolume(int step) {
  int v = static_cast<int>(es8388GetVolume()) + step;
  v = v < 0x00 ? 0x00 : (v > 0x21 ? 0x21 : v);
  es8388SetVolume(static_cast<uint8_t>(v));
  volumeStoreNotifyChanged(static_cast<uint8_t>(v));
}

void switchTo(int position) {
  const ModeEntry e = entryForPosition(position);
  if (e.mode == s_current && e.variant == s_currentVariant) return;  // 変化なし
  if (s_current != nullptr) {
    LOGI("exit mode: %s", s_current->name());
    s_current->exit();
  }
  s_current = e.mode;
  s_currentVariant = e.variant;
  if (s_current != nullptr) {
    LOGI("enter mode: %s (position %d)", s_current->name(), position);
    s_current->enter(e.variant);
  } else {
    LOGI("no mode at position %d", position);
  }
}

}  // namespace

void modeManagerBegin() {
  const int position = panelRotaryPosition(PanelRotary::kSp6t);
  if (position >= 0) switchTo(position);
}

void modeManagerHandleEvent(const PanelEvent& ev) {
  if (ev.type == PanelEventType::kRotarySelect &&
      ev.id == static_cast<uint8_t>(PanelRotary::kSp6t)) {
    s_pendingPosition = ev.value;  // 連続して回されたら最後の位置だけ使う
    return;
  }
  if (ev.type == PanelEventType::kEncoderStep &&
      !(s_current != nullptr && s_current->handlesEncoder())) {
    changeVolume(ev.value * config::kVolumeStep);
    return;
  }
  if (s_current != nullptr) s_current->onEvent(ev);
}

void modeManagerTick() {
  if (s_pendingPosition >= 0) {
    const int position = s_pendingPosition;
    s_pendingPosition = -1;
    switchTo(position);
  }
  if (s_current != nullptr) s_current->tick();
  volumeStoreTick();
  freqStoreTick();
}
