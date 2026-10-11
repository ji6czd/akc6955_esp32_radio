// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_STORE
#define LOG_MODULE_TAG "STORE"
#include "channel_store.h"

#include <Arduino.h>
#include <Preferences.h>

#include "channel_list.h"
#include "config.h"
#include "log_config.h"

namespace {

constexpr char kNamespace[] = "m5rajiru";
constexpr char kKeyChannel[] = "channel";
// インデックスと一緒に、そのとき何局のリストだったかも保存する。局一覧が
// station_id_list.hの固定テーブルになった今、起動ごとに局数が変わることは無いが、
// ファームを書き換えてテーブルを差し替えたときは「範囲内だが全く別の局」を
// 復元してしまう。局数が違えばリストの構成が前回と違うということなので、
// その場合は復元をあきらめてデフォルトへ落とす。
constexpr char kKeyCount[] = "chcount";

Preferences s_prefs;
bool s_dirty = false;
int s_pendingIndex = 0;
uint32_t s_lastChangeMs = 0;

}  // namespace

int channelStoreLoadIndex(int defaultIndex) {
  s_prefs.begin(kNamespace, /*readOnly=*/true);
  int index = s_prefs.getInt(kKeyChannel, -1);
  int savedCount = s_prefs.getInt(kKeyCount, -1);
  s_prefs.end();

  int count = static_cast<int>(g_channelList.count());
  if (index < 0 || index >= count) {
    return defaultIndex;
  }
  if (savedCount != count) {
    LOGW("channel list changed (saved count=%d, now=%d), using default", savedCount, count);
    return defaultIndex;
  }
  LOGI("restored channel %d: %s", index, g_channelList.getChannel(index)->getName());
  return index;
}

void channelStoreNotifyChanged(int index) {
  s_pendingIndex = index;
  s_dirty = true;
  s_lastChangeMs = millis();
}

void channelStoreTick() {
  if (!s_dirty) {
    return;
  }
  if (millis() - s_lastChangeMs < config::kChannelSaveDebounceMs) {
    return;
  }
  s_prefs.begin(kNamespace, /*readOnly=*/false);
  s_prefs.putInt(kKeyChannel, s_pendingIndex);
  s_prefs.putInt(kKeyCount, static_cast<int>(g_channelList.count()));
  s_prefs.end();
  s_dirty = false;
  LOGI("saved channel=%d", s_pendingIndex);
}
