// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_HLS
#define LOG_MODULE_TAG "HLS"
#include "radio_stream.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>

#include <atomic>
#include <cstring>
#include <ctime>
#include <deque>

#include "aac_player.h"
#include "aes_decryptor.h"
#include "channel_list.h"
#include "channel_store.h"
#include "config.h"
#include "hls_playlist.h"
#include "i2s_speaker.h"
#include "jcba_client.h"
#include "listenradio_client.h"
#include "log_config.h"
#include "nhk_client.h"
#include "opus_player.h"
#include "radiko_client.h"
#include "time_sync.h"
#include "ts_demux.h"
#include "wifi_manager.h"

// 全体構成:
//   - ネットワーク取得タスク(別コア, fetchTaskFn):
//       NHK/radiko(HLS) …
//       プレイリスト追跡・鍵取得・セグメントのHTTP取得/AES復号/ID3v2
//                          スキップまでを行い、結果のADTSバイト列を流す。
//       JCBA/FMぷらぷら(WebSocket) … jcba_client が受けた Ogg ページから Opus
//       パケットを取り出して流す。
//     どちらも s_audioStreamBuffer(FreeRTOS StreamBuffer)へ、AudioRecord
//     ([codec][len][payload])の形で書き込む。ここは自由にブロッキングしてよい
//     (Wi-Fiの瞬断やプレイリスト再取得の待ちがあっても後段に影響しない)。
//   - デコード専用タスク(decodeTaskFn):
//   s_audioStreamBufferからレコードを取り出し、
//     codec に応じて AAC(aac_player)か Opus(opus_player)へ渡すだけの処理。codec
//     が
//     インバンドで届くので、切り替え時に「どちらのデコーダで読むか」を別経路で同期する
//     必要が無い(レコードの codec が今のデコーダと違えば、その場で作り直す)。
//     デコード結果のPCMはi2s_speaker.cpp側のリングバッファへenqueueされ、
//     さらに別の専用タスクが実際のI2S書き込み(実時間ペースのブロッキング)を行う。
//   -
//   Arduinoのloop()はキー入力のポーリング/コマンド解釈のみを行い、上記どちらのタスクの
//     ブロッキングにも巻き込まれない。
// fetchTaskFn/decodeTaskFnが共有する可変状態はs_audioStreamBufferのみ(単一Producer/
// 単一ConsumerのFreeRTOS
// StreamBufferなので追加のロックは不要)。プレイリスト/キュー/鍵
// などの状態はすべてfetchTaskFn側だけが触るため、こちらもロック不要。
//
// TLS接続は「プレイリスト/鍵/セグメント全体で1つだけ」を使い回す(s_sharedClient/
// s_sharedHttp)。以前はプレイリスト用とセグメント用で別々に持たせていたが、
// PSRAM非搭載のCardputer ADV(ESP32-S3FN8, 320KB
// SRAM)ではTLSセッションを2つ同時に
// 保持するだけでヒープが尽き、2つ目の接続確立が "SSL - Memory allocation
// failed" で
// 失敗することを実機で確認した。すべて同一ホスト(simulosa.drdi.st.nhk)なので、
// 1本の接続で足りる。

namespace {

// 実行時のヒープ確保(xStreamBufferCreate)はWiFi/TLSスタックとの競合で失敗しうる
// (この場合ハンドルがnullptrのまま返り、xStreamBufferReceive/Send側でFreeRTOSの
// configASSERTに落ちて原因が分かりにくい)。サイズはコンパイル時に確定しているので、
// 静的確保(xStreamBufferCreateStatic)にして実行時の確保失敗そのものをなくす。
// ESP32(PSRAMなし)では64KBの静的配列が静的DRAM(約124KB)に収まらないので、
// radioStreamBegin()でヒープから1回だけ確保する(以降は解放しない)。
uint8_t* s_audioStreamStorage = nullptr;
StaticStreamBuffer_t s_audioStreamBufferStruct;
StreamBufferHandle_t s_audioStreamBuffer = nullptr;

// チャンネル切り替え直後、s_audioStreamBufferに残っている「旧チャンネルの
// 数秒分のクッション」を再生せず高速に読み捨てるためのフラグ。
// radioStreamNextChannel()(キー処理タスク側から呼ばれうる)とconsumerStep()
// (デコード専用タスク)という異なるタスクから読み書きされるのでstd::atomic。
std::atomic<bool> s_discardMode{false};

// s_audioStreamBufferが「たまたま一瞬空に見えた」ことと「もう旧チャンネルの
// バイトがfeedDownstream()されることは絶対にない」ことは同義ではない。
// requestChannelSwitch()呼び出し直後は、ネットワーク取得タスク側の
// playSegment()がまだxQueuePeek()で切り替えに気づいておらず、あと数回
// feedDownstream()する可能性がある。producerStep()がs_channelQueueから
// 切り替え要求を実際に取り出す(=もう旧チャンネル分のplaySegment()呼び出しは
// 存在しない、と確定する)まではfalseにしておき、consumerStep()側の破棄完了
// (デコーダリセット)をブロックする。これが無いと、リセット直後のデコーダに
// 旧チャンネルの残りバイトが正規の新チャンネルデータとして渡ってしまい、
// 一瞬だけ前のチャンネルの音が鳴る。
std::atomic<bool> s_oldChannelFetchStopped{true};

// 通知音(morse_code.cpp)を鳴らすあいだ、デコード→I2S供給を止めるためのフラグ。
// 通知音タスク(別タスク)とconsumerStep()(デコード専用タスク)から読み書きするのでatomic。
// s_notifyPausedAckはconsumerStep()側の「もう供給していない」確認応答で、
// radioStreamPauseForNotification()はこれが立つまで待ってから戻る。
std::atomic<bool> s_notifyPaused{false};
std::atomic<bool> s_notifyPausedAck{false};

// 「停止」機能(radioStreamStop/Resume)。true
// の間、producerStep()は通常のプレイリスト
// 追跡・セグメント取得・JCBA通信を行わず、consumerStep()はデコーダを解放したまま
// レコードの取り出し・デコードを行わない。どちらもこのフラグを見て初回だけ解放処理をし、
// あとはアイドルするだけ(実際の解放処理の呼び出しは各タスク内のローカルなラッチで
// 一度きりに抑える。書き込みは複数タスクから起こりうるのでatomic)。
// switchToChannel()が呼ばれた時点(=radioStreamResume()によるものでも、それ以外の経路
// からの選局要求でも)で必ずfalseへ戻す(呼び出し経路に依らず「選局できた=もう停止中では
// ない」という不変条件をそこ一箇所で保証するため)。
std::atomic<bool> s_stopped{false};

// s_audioStreamBuffer に流すレコードのヘッダ。ペイロードは AAC なら ADTS
// のチャンク (任意の切れ目でよい)、Opus なら Ogg
// のパケット境界どおりの1パケット。
enum class AudioCodec : uint8_t { kAac = 0, kOpus = 1 };
struct AudioRecordHeader {
  uint8_t codec;
  uint8_t lenLo;
  uint8_t lenHi;
};
static_assert(sizeof(AudioRecordHeader) == 3, "record header must be packed");

// レコード(ヘッダ+ペイロード)は必ず1回の xStreamBufferSend()
// で書く。portMAX_DELAY なら FreeRTOS
// は全量ぶんの空きを待ってから一括コピーし、tail を1回だけ更新するので、
// 消費側からはレコードが原子的に見える(「バッファが空」= 必ずレコード境界)。
// 以前はヘッダとペイロードを別々に書いていたため、チャンネル切り替えの読み捨て
// (consumerStep の
// s_discardMode)が「ヘッダ書き込み直後の一瞬の空」を終了条件として
// 拾い、ペイロードの途中から読み始めて境界がずれた(実機で再現: bad record
// header 連発)。
uint8_t s_recordBuf[sizeof(AudioRecordHeader) + config::kAudioRecordMaxPayload];

void sendRecord(AudioCodec codec, const uint8_t* data, size_t len) {
  AudioRecordHeader hdr{static_cast<uint8_t>(codec),
                        static_cast<uint8_t>(len & 0xFF),
                        static_cast<uint8_t>(len >> 8)};
  memcpy(s_recordBuf, &hdr, sizeof(hdr));
  memcpy(s_recordBuf + sizeof(hdr), data, len);
  size_t total = sizeof(hdr) + len;
  size_t n =
      xStreamBufferSend(s_audioStreamBuffer, s_recordBuf, total, portMAX_DELAY);
  if (n != total) {
    // portMAX_DELAY
    // で待つのでここに来るのはバッファ破棄時のみ。境界は次の選局の
    // 読み捨てで復旧する。
    LOGE("record send short: %u of %u bytes", static_cast<unsigned>(n),
         static_cast<unsigned>(total));
  }
}

// ネットワーク取得タスク側から呼ぶ。デコード側(別コア)へレコードとして橋渡しするだけ。
// AAC はバイト列なので kAudioRecordMaxPayload ごとに分割してよい。Opus
// は1パケットが
// 意味の単位なので分割できない(上限を超えるパケットは捨てる。jcba_client
// 側で先に弾かれる)。
void feedDownstream(AudioCodec codec, const uint8_t* data, size_t len) {
  if (len == 0) {
    return;
  }
  if (codec == AudioCodec::kAac) {
    while (len > 0) {
      size_t n = len < config::kAudioRecordMaxPayload
                     ? len
                     : config::kAudioRecordMaxPayload;
      sendRecord(codec, data, n);
      data += n;
      len -= n;
    }
    return;
  }
  if (len > config::kAudioRecordMaxPayload) {
    LOGE("opus packet %u bytes exceeds record payload limit %u; dropped",
         static_cast<unsigned>(len),
         static_cast<unsigned>(config::kAudioRecordMaxPayload));
    return;
  }
  sendRecord(codec, data, len);
}

// jcbaReadPage() のコールバック。Opus パケット1つ = レコード1つ。
void feedOpusPacket(const uint8_t* data, size_t len) {
  feedDownstream(AudioCodec::kOpus, data, len);
}

// TsDemuxer(ListenRadio向け)のコールバック。抜き出したADTS
// AACバイト列をそのまま
// 通常のAAC経路へ渡す(デコーダ側からはNHK/radikoのADTSチャンクと区別が付かない)。
void feedTsAudio(const uint8_t* data, size_t len) {
  feedDownstream(AudioCodec::kAac, data, len);
}

// プレイリスト・鍵・セグメントすべてで使い回す唯一のTLS接続。
WiFiClientSecure s_sharedClient;
HTTPClient s_sharedHttp;
bool s_sharedClientReady = false;

void ensureSharedClient() {
  if (!s_sharedClientReady) {
    s_sharedClient.setInsecure();
    s_sharedClientReady = true;
  }
}

// 再生モード。radikoチャンネルのみ kTimefree を取りうる(NHK/未同期時は常に
// kLive)。
enum class PlayMode : uint8_t { kLive, kTimefree };
PlayMode s_playMode = PlayMode::kLive;
// kTimefree
// のときの「今流している音声の壁時計位置」(UNIX時間)。シーク要求の基準と
// ライブ復帰判定に使う。成功セグメントごとに概算で前進させる。
time_t s_timefreeHeadEpoch = 0;

// switchToChannel()へ渡す要求。選局(isSeek=false)とシーク(isSeek=true)を1本の
// キューで運ぶことで、意図がすべてキュー経由で原子的に渡る(別途のatomicフラグが不要)。
//   isSeek=false           … 通常の選局。radikoなら常にライブから。
//   isSeek=true, seekDelta  … 現在位置(kLiveならnow / kTimefreeならhead)から
//   seekDelta 秒ずらす。 isSeek=true, seekDelta==kSeekToLive …
//   明示的にライブへ復帰。 isSeek=true, isAbsolute … seekDelta
//   は無視し、absEpoch(絶対UNIX時間)を再生位置にする
//                             (resolveSeekTarget
//                             を通さず、ウィンドウへクランプするだけ)。
constexpr int32_t kSeekToLive = INT32_MIN;
struct SwitchRequest {
  int index;
  int32_t seekDelta;
  bool isSeek;
  bool isAbsolute;
  time_t absEpoch;
};

// チャンネル切り替え要求の橋渡し(loop()側/別コア → fetchTaskFn)。
// 要素数1で常に上書き(xQueueOverwrite)することで、「最後の要求」だけが残るようにする
// (連打しても順番に全部処理されるわけではない)。
uint8_t s_channelQueueStorage[sizeof(SwitchRequest)];
StaticQueue_t s_channelQueueStruct;
QueueHandle_t s_channelQueue = nullptr;

// fetch/decodeタスクのTCBとスタックも静的確保にする(s_audioStreamBufferと同じ理由)。
// xTaskCreatePinnedToCoreはTCB+スタックをヒープから取るため、WiFi/TLS確立後で
// ヒープが逼迫したこのタイミングだと失敗しうる(しかも戻り値pdFAILを見ていない)。
// xTaskCreateStaticPinnedToCoreならサイズがコンパイル時確定なので確保失敗が起こらない。
// StackType_t=uint8_tなのでスタック長の単位はバイト(config::k*StackBytesをそのまま渡す)。
StackType_t s_fetchTaskStack[config::kFetchTaskStackBytes];
StaticTask_t s_fetchTaskBuffer;
StackType_t s_decodeTaskStack[config::kDecodeTaskStackBytes];
StaticTask_t s_decodeTaskBuffer;
TaskHandle_t s_fetchTask = nullptr;
TaskHandle_t s_decodeTask = nullptr;

// 両タスクのスタック最小残量(バイト。ESP-IDF の StackType_t は uint8_t
// なので単位はバイト)。 定期ログに載せて、config::k*TaskStackBytes
// の余裕を実機で確かめるためのもの。
unsigned fetchStackFree() {
  return s_fetchTask ? uxTaskGetStackHighWaterMark(s_fetchTask) : 0;
}
unsigned decodeStackFree() {
  return s_decodeTask ? uxTaskGetStackHighWaterMark(s_decodeTask) : 0;
}

String s_mediaPlaylistUrl;

// 現在選局中のチャンネルがRadikoの場合のみ非空。master/media
// playlist・セグメント
// 取得のすべてでX-Radiko-AuthTokenヘッダとして使う(NHKチャンネルでは常に空文字)。
String s_currentRadikoToken;

// 現在選局中のチャンネルがListenRadioの場合のみ非空(常にkListenRadioOrigin)。
// master/media playlist・セグメント取得のすべてでOriginヘッダとして使う。
String s_currentOriginHeader;

// 現在選局中のチャンネルのプラットフォーム。playSegment()がセグメント本体を
// 生MPEG-TS(ListenRadio)として扱うかADTSの生バイト列(NHK/radiko)として扱うかの分岐に使う
// (JCBAはHLSを使わないのでここには来ない)。
ChannelPlatform s_currentPlatform = ChannelPlatform::kNhk;

uint8_t s_aesKey[16];
bool s_haveKey = false;
String s_keyUrl;

std::deque<hls::Segment> s_queue;
uint64_t s_nextSequenceToPlay = 0;
uint64_t s_lastQueuedSequence = 0;

// メディアプレイリスト再取得の間隔制御(RFC 8216 §6.3.4 / hls.js
// と同じ)。前回の取得で 新しいセグメントがあれば
// TARGETDURATION、無ければ(または失敗なら)その半分を空ける。
// 以前はキューが空の間 200ms
// ごとに取り直しており、公式ウェブアプリより桁違いに多かった。
uint32_t s_lastPlaylistFetchMs = 0;
uint32_t s_playlistReloadWaitMs = 0;
int s_targetDurationSec = config::kDefaultTargetDurationSec;

bool playlistReloadDue() {
  return millis() - s_lastPlaylistFetchMs >= s_playlistReloadWaitMs;
}

// タイムフリーの medialist に #EXT-X-ENDLIST が付いた(要求した to
// に達した)。キューを 使い切ったら producerStep() が
// continueTimefreeAfterEndList() で続きを張り直す。
bool s_timefreeEndReached = false;

// continueTimefreeAfterEndList() が張り直すときだけ非0。初回 refreshPlaylist()
// は、放送時刻が
// これより前のセグメント(前のセッションで再生済みの分)を積まない。
time_t s_timefreeSkipBeforeEpoch = 0;

int s_consecutiveSegmentFailures = 0;
int s_consecutivePlaylistFailures = 0;

bool s_firstSegmentChecked = false;

// 復号後の先頭にあるID3v2ヘッダ(Appleのcom.apple.streaming.transportStreamTimestamp)を
// スキップし、最初のADTS同期語までのオフセットを返す。ID3v2タグは先頭チャンク内に収まる
// 前提(実測で10+63バイト程度)。収まらない場合はこのチャンク全体をスキップする。
size_t skipId3v2(const uint8_t* buf, size_t len) {
  if (len < 10 || buf[0] != 'I' || buf[1] != 'D' || buf[2] != '3') {
    return 0;
  }
  uint32_t size = (static_cast<uint32_t>(buf[6] & 0x7F) << 21) |
                  (static_cast<uint32_t>(buf[7] & 0x7F) << 14) |
                  (static_cast<uint32_t>(buf[8] & 0x7F) << 7) |
                  static_cast<uint32_t>(buf[9] & 0x7F);
  size_t total = 10 + size;
  return total <= len ? total : len;
}

bool fetchAesKey(const String& url, uint8_t out[16]) {
  ensureSharedClient();
  if (!s_sharedHttp.begin(s_sharedClient, url)) {
    return false;
  }
  int code = s_sharedHttp.GET();
  if (code != HTTP_CODE_OK) {
    s_sharedHttp.end();
    return false;
  }

  WiFiClient* stream = s_sharedHttp.getStreamPtr();
  size_t got = 0;
  uint32_t start = millis();
  while (got < 16 && millis() - start < 5000) {
    if (stream->available()) {
      int n = stream->read(out + got, 16 - got);
      if (n > 0) {
        got += n;
      }
    } else {
      delay(5);
    }
  }
  s_sharedHttp.end();
  return got == 16;
}

bool ensureKey(const hls::MediaPlaylist& playlist) {
  if (playlist.keyUrl.length() == 0) {
    // s_haveKeyをリセットしないと、直前のチャンネルが暗号化(NHK)されていた場合に
    // 古い鍵がtrueのまま残る。Radikoは暗号化なしなので、NHK→Radiko切り替え時に
    // playSegment()が暗号化されていないセグメントを誤って旧鍵でAES-CBC復号して
    // しまい、AACデコーダに渡るバイト列が壊れて(フレームを検出できず)無音になる。
    s_haveKey = false;
    return true;  // 暗号化なし(Radikoはこちら)
  }
  if (s_haveKey && playlist.keyUrl == s_keyUrl) {
    return true;
  }
  LOGI("fetching AES key...");
  if (!fetchAesKey(playlist.keyUrl, s_aesKey)) {
    LOGE("failed to fetch AES key");
    return false;
  }
  s_keyUrl = playlist.keyUrl;
  s_haveKey = true;
  LOGI("AES key fetched (16 bytes)");
  return true;
}

// メディアプレイリストを再取得し、キューへ新規セグメントを反映する。
// isInitial=true の場合はライブエッジ付近から再生キューを組み立て直す。
bool refreshPlaylist(bool isInitial) {
  ensureSharedClient();
  hls::MediaPlaylist playlist;
  s_lastPlaylistFetchMs = millis();
  s_playlistReloadWaitMs = static_cast<uint32_t>(s_targetDurationSec) *
                           500;  // 失敗時は半分で取り直す
  if (!hls::fetchMediaPlaylist(s_sharedClient, s_sharedHttp, s_mediaPlaylistUrl,
                               playlist, s_currentRadikoToken,
                               s_currentOriginHeader)) {
    s_consecutivePlaylistFailures++;
    return false;
  }
  s_consecutivePlaylistFailures = 0;
  s_targetDurationSec = playlist.targetDurationSec > 0
                            ? playlist.targetDurationSec
                            : config::kDefaultTargetDurationSec;

  if (!ensureKey(playlist)) {
    return false;
  }

  if (isInitial) {
    LOGI(
        "media playlist fetched: N=%u segments, first_seq=%llu, "
        "target_duration=%ds%s",
        static_cast<unsigned>(playlist.segments.size()),
        static_cast<unsigned long long>(playlist.firstSequence),
        playlist.targetDurationSec, playlist.endList ? ", ENDLIST" : "");

    // ライブは「末尾=ライブエッジ」なので、末尾から
    // config::kLiveStartMarginSegments 本
    // 手前を開始位置にする(遅延↔アンダーラン/期限切れ耐性のトレードオフ。config.h参照)。
    // タイムフリーは初回medialistが seek
    // 指定時刻を先頭に一括で返るので、末尾基準にすると
    // その先頭を丸ごと捨ててしまう。先頭(0)から積む。
    size_t startIdx = 0;
    if (s_playMode != PlayMode::kTimefree) {
      const size_t margin =
          static_cast<size_t>(config::kLiveStartMarginSegments);
      startIdx = playlist.segments.size() > margin
                     ? playlist.segments.size() - margin
                     : 0;
    } else if (s_timefreeSkipBeforeEpoch > 0) {
      // ENDLIST 後の張り直し。seek
      // は前のセッションの最後のセグメントの終端より少し先に してあり、radiko
      // はそれをセグメント境界へ切り下げるので、通常は続きのセグメントが
      // 先頭に来る。念のため、前のセッションで再生済みのセグメント(放送開始がその終端より
      // 1セグメント近く前)が混ざっていたら飛ばす。境界は秒単位の丸め誤差(±2秒)を見込む。
      while (startIdx < playlist.segments.size() &&
             playlist.segments[startIdx].programDateTime > 0 &&
             playlist.segments[startIdx].programDateTime + 2 <
                 s_timefreeSkipBeforeEpoch) {
        ++startIdx;
      }
      if (startIdx > 0) {
        LOGI("timefree continuation: skipped %u already-played segs",
             static_cast<unsigned>(startIdx));
      }
    }
    s_queue.clear();
    for (size_t i = startIdx; i < playlist.segments.size(); ++i) {
      s_queue.push_back(playlist.segments[i]);
    }
    s_nextSequenceToPlay = startIdx < playlist.segments.size()
                               ? playlist.segments[startIdx].sequence
                               : playlist.segments.back().sequence + 1;
    s_lastQueuedSequence = playlist.segments.back().sequence;
    s_playlistReloadWaitMs = static_cast<uint32_t>(s_targetDurationSec) * 1000;
    // 積めるものが無いまま ENDLIST
    // 扱いにすると、張り直しが空回りし続ける。その場合は 通常の再取得に任せる。
    s_timefreeEndReached = (s_playMode == PlayMode::kTimefree) &&
                           playlist.endList && !s_queue.empty();

    // タイムフリーの再生位置を、要求した seek 値ではなく radiko が実際に返した
    // 先頭セグメントの #EXT-X-PROGRAM-DATE-TIME へ貼り直す。seek
    // はセグメント境界
    // (≒5秒グリッド)へ切り下げられるので最大5秒ぶん実音声より新しくなっており、
    // それを基準に相対シークすると誤差が乗る。PDT
    // が取れない場合は要求値のまま。
    if (s_playMode == PlayMode::kTimefree && !s_queue.empty() &&
        s_queue.front().programDateTime > 0) {
      time_t actual = s_queue.front().programDateTime;
      long drift = static_cast<long>(actual - s_timefreeHeadEpoch);
      if (drift != 0) {
        LOGI("timefree head realigned by PDT: requested=%ld actual=%ld (%+lds)",
             static_cast<long>(s_timefreeHeadEpoch), static_cast<long>(actual),
             drift);
      }
      s_timefreeHeadEpoch = actual;
    }

    LOGI("starting at sequence %llu (%s, queued %u segs)",
         static_cast<unsigned long long>(s_nextSequenceToPlay),
         s_playMode == PlayMode::kTimefree ? "timefree seek head" : "live edge",
         static_cast<unsigned>(s_queue.size()));
    return true;
  }

  if (playlist.firstSequence > s_nextSequenceToPlay) {
    LOGW(
        "fell behind live window (next=%llu, window starts at %llu), resyncing",
        static_cast<unsigned long long>(s_nextSequenceToPlay),
        static_cast<unsigned long long>(playlist.firstSequence));
    s_queue.clear();
    s_nextSequenceToPlay = playlist.firstSequence;
    s_lastQueuedSequence =
        playlist.firstSequence > 0 ? playlist.firstSequence - 1 : 0;
  }

  size_t added = 0;
  for (const auto& seg : playlist.segments) {
    if (seg.sequence > s_lastQueuedSequence) {
      s_queue.push_back(seg);
      s_lastQueuedSequence = seg.sequence;
      ++added;
    }
  }

  // RFC 8216 §6.3.4: 新しいセグメントがあれば
  // TARGETDURATION、無ければその半分待って取り直す。
  s_playlistReloadWaitMs =
      static_cast<uint32_t>(s_targetDurationSec) * (added > 0 ? 1000 : 500);
  if (s_playMode == PlayMode::kTimefree && playlist.endList) {
    s_timefreeEndReached = true;
  }

  // added==0が続く = プレイリストは取得できているのに新しいセグメントが1つも
  // 現れない状態。この場合キューが空のままproducerStep()が無言の空回りをするため、
  // ログが一切出ずに無音になる。再取得は上の間隔制御で TARGETDURATION/2
  // 以上空くので、 毎回出しても溢れない。
  LOGD("refresh: N=%u first_seq=%llu last_queued=%llu added=%u queue=%u%s",
       static_cast<unsigned>(playlist.segments.size()),
       static_cast<unsigned long long>(playlist.firstSequence),
       static_cast<unsigned long long>(s_lastQueuedSequence),
       static_cast<unsigned>(added), static_cast<unsigned>(s_queue.size()),
       playlist.endList ? " ENDLIST" : "");
  return true;
}

enum class SegmentResult { Ok, Failed, AbortedForChannelSwitch };

// outFedBytesには、実際にfeedDownstream()した(=デコーダへ渡ることが確定した)
// バイト数を返す。「セグメントは取れているのにデコーダがフレームを出さない」のか
// 「そもそもバイトが流れていない」のかを切り分けるための診断用。
SegmentResult playSegment(const hls::Segment& seg, size_t& outFedBytes) {
  outFedBytes = 0;
  ensureSharedClient();
  if (!s_sharedHttp.begin(s_sharedClient, seg.url)) {
    LOGE("segment %llu: http.begin failed",
         static_cast<unsigned long long>(seg.sequence));
    return SegmentResult::Failed;
  }
  if (s_currentRadikoToken.length() > 0) {
    s_sharedHttp.addHeader("X-Radiko-AuthToken", s_currentRadikoToken);
  }
  if (s_currentOriginHeader.length() > 0) {
    s_sharedHttp.addHeader("Origin", s_currentOriginHeader);
    // addHeader("Accept-Encoding",
    // ...)はHTTPClientに無視される(hls_playlist.cppの
    // 同種コメント参照)。setAcceptEncoding()を使う。
    s_sharedHttp.setAcceptEncoding("identity");
  }
  int code = s_sharedHttp.GET();
  if (code != HTTP_CODE_OK) {
    LOGE("segment %llu: HTTP %d", static_cast<unsigned long long>(seg.sequence),
         code);
    s_sharedHttp.end();
    s_sharedClient.stop();
    return SegmentResult::Failed;
  }
  int remaining = s_sharedHttp.getSize();
  LOGD("segment %llu: HTTP 200, content-length=%d",
       static_cast<unsigned long long>(seg.sequence), remaining);

  AesCbcStreamDecryptor decryptor;
  if (s_haveKey) {
    decryptor.begin(s_aesKey, seg.sequence);
  }
  // ListenRadioのセグメントは生MPEG-TS(H.264映像+AAC-LC音声の多重)なので、AAC経路へ渡す前に
  // 音声ESだけを抜き出す必要がある。他局は素のADTSバイト列なのでそのまま流す。
  bool isRawTs = (s_currentPlatform == ChannelPlatform::kListenRadio);
  TsDemuxer tsDemux(feedTsAudio);

  WiFiClient* stream = s_sharedHttp.getStreamPtr();
  static uint8_t chunk[config::kSegmentReadChunkSize];
  size_t carry = 0;
  bool firstChunk = true;
  SegmentResult result = SegmentResult::Ok;
  uint32_t lastByteMs = millis();

  while (remaining != 0) {
    // 覗き見るだけ(取り出さない)。実際の切り替え処理はproducerStep()先頭が行う。
    // xQueuePeekはuxItemSize分コピーするので、受けは必ずSwitchRequest全体を置くこと
    // (intだけ渡すとスタックを踏む)。
    SwitchRequest pendingReq;
    if (xQueuePeek(s_channelQueue, &pendingReq, 0) == pdTRUE) {
      LOGD("segment %llu: aborting early for channel switch",
           static_cast<unsigned long long>(seg.sequence));
      result = SegmentResult::AbortedForChannelSwitch;
      break;
    }

    size_t want = sizeof(chunk) - carry;
    if (remaining > 0 && static_cast<int>(want) > remaining) {
      want = remaining;
    }
    int n = stream->available() ? stream->read(chunk + carry, want) : 0;
    if (n <= 0) {
      if (millis() - lastByteMs > 8000) {
        LOGW("segment %llu: read timeout",
             static_cast<unsigned long long>(seg.sequence));
        result = SegmentResult::Failed;
        break;
      }
      if (!s_sharedClient.connected() && stream->available() == 0) {
        if (remaining < 0) {
          break;  // Content-Length不明のまま接続終了 = 正常終了とみなす
        }
        LOGW("segment %llu: connection closed early",
             static_cast<unsigned long long>(seg.sequence));
        result = SegmentResult::Failed;
        break;
      }
      delay(2);
      continue;
    }
    lastByteMs = millis();
    if (remaining > 0) {
      remaining -= n;
    }

    size_t have = carry + n;
    // 16バイト境界に切り揃えるのはAES-CBCの都合。非暗号化(Radiko)では不要なうえ、
    // セグメント長が16の倍数とは限らないため、末尾の端数(最大15バイト)がループを
    // 抜けたところで捨てられてしまう。セグメントごとにADTSフレームが1つ壊れるので、
    // 暗号化されていないときは切り揃えずそのまま流す。
    // (NHKはAES-CBCなのでセグメント長が必ず16の倍数になり、この問題は起きない。)
    size_t alignedLen = s_haveKey ? have - (have % 16) : have;
    if (s_haveKey && alignedLen > 0) {
      decryptor.update(chunk, alignedLen);
    }

    if (firstChunk && !s_firstSegmentChecked && alignedLen >= 3) {
      LOGI("first segment decrypted OK, header=%02X %02X %02X", chunk[0],
           chunk[1], chunk[2]);
      s_firstSegmentChecked = true;
    }

    const uint8_t* payload = chunk;
    size_t usableLen = alignedLen;
    if (firstChunk && usableLen > 0) {
      size_t skip = skipId3v2(chunk, usableLen);
      payload += skip;
      usableLen -= skip;
      firstChunk = false;
    }
    if (usableLen > 0) {
      if (isRawTs) {
        tsDemux.feed(payload, usableLen);
      } else {
        feedDownstream(AudioCodec::kAac, payload, usableLen);
      }
      outFedBytes += usableLen;
    }

    carry = have - alignedLen;
    if (carry > 0) {
      memmove(chunk, chunk + alignedLen, carry);
    }
  }

  s_sharedHttp.end();
  if (result != SegmentResult::Ok) {
    // 本文を最後まで読み切れなかった場合、HTTPClient内部の「keep-aliveで
    // 再利用可能」判定(その時点でクライアント側バッファにデータが残って
    // いないかどうかだけを見る)を信用しない。TLS層にはまだ届いていない
    // 残りのセグメントデータが後から流れてくる可能性があり、それを次の
    // リクエスト(master/mediaプレイリスト等、同一ホストなので接続が
    // 使い回される)がレスポンスとして読んでしまうと、パース失敗
    // (例: error(-7) no HTTP server)を引き起こす。確実に切断する。
    s_sharedClient.stop();
  }
  return result;
}

// radiko再生中(トークンを持っている)なら、authTokenの失効を疑って認証をやり直し、
// 以降のHLS取得に使うトークンを差し替える。radiko以外の再生中は何もしない。
// 再認証に失敗したら古いトークンのまま(次の失敗でまた試す)。
void reauthorizeRadikoIfActive() {
  if (s_currentRadikoToken.length() == 0) {
    return;
  }
  LOGW("radiko playback failing; re-authorizing in case the token expired");
  radikoInvalidateAuth();
  ensureSharedClient();
  if (radikoEnsureAuthorized(s_sharedClient, s_sharedHttp)) {
    s_currentRadikoToken = radikoAuthToken();
  } else {
    LOGE("radiko re-authorization failed");
  }
}

void resyncFromLiveEdge() {
  s_queue.clear();
  while (!refreshPlaylist(true)) {
    LOGE("resync failed, retrying...");
    reauthorizeRadikoIfActive();
    delay(2000);
  }
  s_consecutiveSegmentFailures = 0;
  s_consecutivePlaylistFailures = 0;
}

// タイムフリーで到達できる時刻レンジ [oldest, newest](now
// 基準)。相対シークのクランプと
// 日時ジャンプの範囲チェック・クランプで共有する。
void timefreeWindowBounds(time_t now, time_t* oldest, time_t* newest) {
  *oldest =
      now - config::kTimefreeMaxAgeSec;  // 約30日前(config.h、1時間の余裕込み)
  // 直近端。2026-09-03 に TBS で now-30s まで
  // 1秒刻みで実測したところ、playlist_create は すべて 200 で返り、先頭 PDT
  // のズレはセグメント境界への切り下げ(≤5s、常に手前)だけ
  // だった。8月に見られた「最後の1〜2分は未タイムフリー化」は再現せず。ここは端末時計の
  // SNTP 誤差(数秒進みうる)で seek がサーバ now
  // を越えないための最小マージンに留める。
  *newest = now - 45;
}

// kTimefree のシーク目標を決める。base(kLiveならnow / kTimefreeならhead)に
// delta を
// 足し、遡り限界(約30日)と直近端(タイムフリー化されている端)でクランプする。
// 結果が now に十分近ければ kLive を返す(ライブ再生に戻す)。
PlayMode resolveSeekTarget(int32_t delta, time_t* outHeadEpoch) {
  time_t now = time(nullptr);
  time_t base = (s_playMode == PlayMode::kTimefree) ? s_timefreeHeadEpoch : now;
  time_t target = base + delta;

  // 現在に追いついた/追い越したらライブへ(クランプより前に判定する。
  // そうしないと未来方向シークが下の「直近端」で止まってライブに戻れない)。
  // ただし後退シーク(delta<0)では通さない。1分ぶんの後退は now-90 しきい値を
  // 1回で越えられず、ここで弾くとライブから timefree に入れなくなる。後退は
  // 下のクランプ経由で kNewestAllowed まで飛ばす。
  if (delta >= 0 && target >= now - 45) {
    return PlayMode::kLive;
  }
  // ライブ端付近からの初回シークは kNewestAllowed まで飛ぶが、以降は head
  // が離れているので 1 分刻みがそのまま通る。
  time_t kOldestAllowed = 0, kNewestAllowed = 0;
  timefreeWindowBounds(now, &kOldestAllowed, &kNewestAllowed);
  if (target < kOldestAllowed) {
    target = kOldestAllowed;
  }
  if (target > kNewestAllowed) {
    target = kNewestAllowed;
  }
  *outHeadEpoch = target;
  return PlayMode::kTimefree;
}

// JCBA/FMぷらぷら(WebSocket)再生中かどうか。fetch タスクだけが触る。true の間
// producerStep() は HLS の処理(プレイリスト/セグメント)の代わりに jcbaStep()
// を回す。
bool s_jcbaActive = false;
uint32_t s_jcbaLastDataMs = 0;
int s_jcbaConsecutiveFailures = 0;
uint32_t s_jcbaLastLogMs = 0;

// JcbaService は kJcba/kFmPlapla の2値なので、ChannelPlatform
// から素直に対応させる。
static JcbaService jcbaServiceForPlatform(ChannelPlatform platform) {
  return (platform == ChannelPlatform::kFmPlapla) ? JcbaService::kFmPlapla
                                                  : JcbaService::kJcba;
}

// NHK・JCBA・FMぷらぷらは常に可、radiko は無料開放エリアの判定に従う。
static bool channelPlayableNow(const Channel* c, size_t idx) {
  if (c->getPlatform() == ChannelPlatform::kRadiko) {
    return radikoStationPlayable(g_channelList.getFreeAreaMask(idx));
  }
  return true;
}

// start から
// dir(+1/-1)方向に、いま再生できる局のインデックスを探す(全体を一周)。
// どれも該当しなければ
// -1。無料利用で在圏外(プレミアム専用)の局を飛ばして選局するために使う。
int playableChannelFrom(int start, int dir) {
  int n = static_cast<int>(g_channelList.count());
  for (int i = 0; i < n; ++i) {
    int idx = ((start + dir * i) % n + n) % n;
    const Channel* c = g_channelList.getChannel(static_cast<size_t>(idx));
    if (c != nullptr && channelPlayableNow(c, static_cast<size_t>(idx))) {
      return idx;
    }
  }
  return -1;
}

// 指定エリア(JIS都道府県コード)に所在する、いま再生できる最初の局。テーブルは
// プラットフォーム→エリア順なので、NHKの拠点がある県ならNHK(必ず再生可)、そうでなければ
// 在圏のradiko局(所在地では無料開放されているのが普通)が返る。該当なし/エリア不明なら
// -1。
int firstPlayableChannelInArea(uint8_t area) {
  if (area == 0) {
    return -1;
  }
  int n = static_cast<int>(g_channelList.count());
  for (int i = 0; i < n; ++i) {
    const Channel* c = g_channelList.getChannel(static_cast<size_t>(i));
    if (c != nullptr && c->getArea() == area &&
        channelPlayableNow(c, static_cast<size_t>(i))) {
      return i;
    }
  }
  return -1;
}

// いま再生できる最初の radiko 局(excludeIndex は除く)。無ければ -1。
int firstPlayableRadikoChannel(int excludeIndex) {
  int n = static_cast<int>(g_channelList.count());
  for (int i = 0; i < n; ++i) {
    if (i == excludeIndex) {
      continue;
    }
    const Channel* c = g_channelList.getChannel(static_cast<size_t>(i));
    if (c != nullptr && c->getPlatform() == ChannelPlatform::kRadiko &&
        channelPlayableNow(c, static_cast<size_t>(i))) {
      return i;
    }
  }
  return -1;
}

// 選局の1段階(認証・URL生成・プレイリスト取得)を最大
// config::kChannelSwitchRetryCount 回 試す。全て失敗したら
// false(呼び出し側はその時点で選局を諦める)。
template <typename F>
bool retryBounded(const char* what, F&& attempt) {
  for (int i = 1; i <= config::kChannelSwitchRetryCount; ++i) {
    if (attempt()) {
      return true;
    }
    LOGE("%s failed (attempt %d/%d)", what, i,
         config::kChannelSwitchRetryCount);
    if (i < config::kChannelSwitchRetryCount) {
      delay(config::kChannelSwitchRetryDelayMs);
    }
  }
  return false;
}

// 指定チャンネルのマスタープレイリストからライブエッジ付近まで組み立て直す。
// 起動時の初回選局・チャンネル切り替え・タイムフリーのシーク(同一局への張り直し)で使う共通処理。
// 鍵(s_keyUrl)は refreshPlaylist -> ensureKey が新しいkeyUrlとの比較で自動的に
// 再取得してくれるので、ここで明示的にリセットする必要はない。
// 戻り値: 再生キューまで組み立てられたら
// true。いずれかの段階が有限リトライを使い切ったら
// false(以前は無限リトライだったため、特定の局が恒久的に壊れている[認証キー変更・
// 新たな403等]とそこで永久にハングし、選局操作もキューに積まれたまま届かなかった)。
// false のとき s_mediaPlaylistUrl 等は途中の状態のまま。呼び出し側は
// fallbackToBootChannel() で退避する。
bool switchToChannel(int index, int32_t seekDelta, bool isSeek, bool isAbsolute,
                     time_t absEpoch) {
  const Channel* ch =
      (index < 0) ? nullptr
                  : g_channelList.getChannel(static_cast<size_t>(index));
  if (ch == nullptr) {
    LOGE("invalid channel index %d (count=%u)", index,
         static_cast<unsigned>(g_channelList.count()));
    return false;
  }

  ensureSharedClient();

  // 呼び出し経路(radioStreamResume()経由か、それ以外の選局要求か)に依らず、選局できた
  // 時点でもう「停止中」ではない。ここ一箇所でfalseへ戻すことで不変条件を保証する。
  s_stopped.store(false, std::memory_order_relaxed);
  // 前のセッションの ENDLIST
  // を持ち越さない(途中の段階で失敗したとき、producerStep() が 別の局のつもりで
  // continueTimefreeAfterEndList() を呼ばないように)。
  s_timefreeEndReached = false;

  // 直前まで JCBA を受けていたら WebSocket を閉じる。共有 TLS クライアントを
  // WebSocket ホストに繋いだままだと、続く
  // HTTP(認証/プレイリスト)がそこへ流れてしまう。
  if (s_jcbaActive) {
    jcbaDisconnect();
    s_jcbaActive = false;
  }

  // 無料利用で在圏外(プレミアム専用)の radiko
  // 局が指定されたら、在圏の再生可能な 局へフォールバックする。放置すると
  // type=b で 403 が返り続け、下の retryBounded 群を 使い切って
  // kBootChannelIndex へ落ちるだけになる(在圏NHKの方が親切)。通常の選局は
  // requestChannelSwitch() が事前に弾くので、ここへ来るのは主に起動時(保存済み
  // インデックスがプレミアム専用局)。
  if (ch->getPlatform() == ChannelPlatform::kRadiko) {
    if (!retryBounded("radiko auth", [] {
          return radikoEnsureAuthorized(s_sharedClient, s_sharedHttp);
        })) {
      return false;
    }
    if (!radikoStationPlayable(
            g_channelList.getFreeAreaMask(static_cast<size_t>(index)))) {
      // まず在圏エリアの再生可能な先頭局へ。エリアが取れなければ前方スキャンで代用。
      int fb = firstPlayableChannelInArea(radikoAreaNumber());
      if (fb < 0) {
        fb = playableChannelFrom(index, +1);
      }
      LOGW(
          "channel [%d] %s needs radiko premium (station JP%u, tuner %s); "
          "falling back to [%d]",
          index, ch->getName(), static_cast<unsigned>(ch->getArea()),
          radikoArea().c_str(), fb);
      if (fb < 0) {
        LOGE("no playable channel available; aborting switch");
        return false;
      }
      index = fb;
      ch = g_channelList.getChannel(static_cast<size_t>(index));
      isSeek = false;  // 別局へ移ったのでシーク指定は無効。
      isAbsolute =
          false;  // 別局へのフォールバックが日時ジャンプになってはいけない。
    }
  }

  g_channelList.setCurrentIndex(static_cast<size_t>(index));
  s_currentPlatform = ch->getPlatform();
  LOGI("switching to channel: %04d %s [JP%u %s]",
       g_channelList.tuneCodeForIndex(static_cast<size_t>(index)),
       ch->getName(), static_cast<unsigned>(ch->getArea()),
       g_channelList.getCurrentAreaName());

  if (ch->getPlatform() == ChannelPlatform::kJcba ||
      ch->getPlatform() == ChannelPlatform::kFmPlapla) {
    // HLS ではなく WebSocket。プレイリスト系の状態は使わないので空にしておく
    // (producerStep() は s_jcbaActive を見て jcbaStep() へ分岐する)。
    s_playMode = PlayMode::kLive;
    s_currentRadikoToken = "";
    s_queue.clear();
    JcbaService service = jcbaServiceForPlatform(ch->getPlatform());
    if (!retryBounded("jcba connect", [&] {
          return jcbaConnect(s_sharedClient, s_sharedHttp, service,
                             ch->getStreamId(), config::kJcbaBurstSec);
        })) {
      return false;
    }
    s_jcbaActive = true;
    s_jcbaLastDataMs = millis();
    s_jcbaConsecutiveFailures = 0;
    s_consecutiveSegmentFailures = 0;
    s_consecutivePlaylistFailures = 0;
    return true;
  }

  String masterUrl;
  s_currentRadikoToken = "";
  s_currentOriginHeader = "";
  if (ch->getPlatform() == ChannelPlatform::kRadiko) {
    // 再生モードの決定。通常の選局(isSeek=false)は必ずライブに戻す。
    // シークでも、システム時刻が未同期なら安全側でライブにする。
    if (isAbsolute) {
      // 絶対日時ジャンプ。radiko + 時刻同期は呼び出し側で確認済み。
      // ウィンドウへクランプして無条件に kTimefree。
      time_t now = time(nullptr);
      time_t oldest = 0, newest = 0;
      timefreeWindowBounds(now, &oldest, &newest);
      time_t head = absEpoch;
      if (head < oldest) {
        head = oldest;
      }
      if (head > newest) {
        head = newest;
      }
      s_playMode = PlayMode::kTimefree;
      s_timefreeHeadEpoch = head;
    } else if (!isSeek || seekDelta == kSeekToLive || !timeSyncIsValid()) {
      s_playMode = PlayMode::kLive;
    } else {
      time_t head = 0;
      s_playMode = resolveSeekTarget(seekDelta, &head);
      if (s_playMode == PlayMode::kTimefree) {
        s_timefreeHeadEpoch = head;
      }
    }

    if (s_playMode == PlayMode::kTimefree) {
      if (!retryBounded("radiko timefree URL build", [&] {
            return radikoBuildTimefreeUrl(s_sharedClient, s_sharedHttp,
                                          ch->getStreamId(),
                                          s_timefreeHeadEpoch, masterUrl);
          })) {
        return false;
      }
      LOGI("radiko timefree URL (head=%ld) = %s",
           static_cast<long>(s_timefreeHeadEpoch), masterUrl.c_str());
    } else {
      if (!retryBounded("radiko live URL build", [&] {
            return radikoBuildLiveUrl(s_sharedClient, s_sharedHttp,
                                      ch->getStreamId(), masterUrl);
          })) {
        return false;
      }
      LOGI("radiko live URL = %s", masterUrl.c_str());
    }
    s_currentRadikoToken = radikoAuthToken();
  } else if (ch->getPlatform() == ChannelPlatform::kListenRadio) {
    // 認証不要、ChannelIdだけでURLが決まる。配信サーバがOriginをCORSとして必須で
    // 見るため、以降のmaster/media/セグメント取得すべてにOriginヘッダを付ける。
    s_playMode = PlayMode::kLive;
    masterUrl = listenRadioBuildLiveUrl(ch->getStreamId());
    s_currentOriginHeader = kListenRadioOrigin;
  } else {
    // NHKはstreamIdにストリーム番号だけが入っているので、ここでURLに展開する。
    s_playMode = PlayMode::kLive;
    masterUrl = nhkBuildLiveUrl(ch->getStreamId());
  }

  String mediaUrl;
  if (!retryBounded("master playlist fetch", [&] {
        if (hls::fetchMasterPlaylist(s_sharedClient, s_sharedHttp, masterUrl,
                                     mediaUrl, s_currentRadikoToken,
                                     s_currentOriginHeader)) {
          return true;
        }
        reauthorizeRadikoIfActive();  // トークン失効なら次の試行で新トークンを使う
        return false;
      })) {
    return false;
  }
  LOGI("media playlist = %s", mediaUrl.c_str());
  s_mediaPlaylistUrl = mediaUrl;

  if (!retryBounded("media playlist/key fetch", [] {
        if (refreshPlaylist(/*isInitial=*/true)) {
          return true;
        }
        reauthorizeRadikoIfActive();
        return false;
      })) {
    return false;
  }
  s_consecutiveSegmentFailures = 0;
  s_consecutivePlaylistFailures = 0;
  return true;
}

// switchToChannel()
// が有限リトライを使い切って失敗したときの退避。認証不要で常に有効な
// config::kBootChannelIndex へライブ選局し直す。これも失敗したら何もせず戻り、
// producerStep() の通常の失敗経路(Wi-Fi 再接続 + resync)に任せる。
// 失敗した局が既に kBootChannelIndex なら再試行しない(無限ループ防止)。
// 保存済みチャンネル(channel_store)は書き換えない:
// 一時的な障害で好みの局を上書き しないため(そもそも channel_store は loop()
// 側のタスクからしか触れない)。次回起動は
// 再び保存局を試し、また失敗すればここへ来る。ユーザーは通常の選局操作でいつでも
// 別の局へ移れる(そのとき保存される)。
void fallbackToBootChannel(int failedIndex) {
  // まず radiko
  // で再生可能な最初の局へ(失敗した局自身は除く)。見つからない/それも失敗なら
  // 認証不要の kBootChannelIndex へ退避する。
  int radikoIdx = firstPlayableRadikoChannel(failedIndex);
  if (radikoIdx >= 0) {
    LOGE(
        "channel [%d] unreachable after retries; falling back to first "
        "playable radiko [%d]",
        failedIndex, radikoIdx);
    if (switchToChannel(radikoIdx, 0, /*isSeek=*/false, /*isAbsolute=*/false,
                        /*absEpoch=*/0)) {
      return;
    }
    LOGE("radiko fallback [%d] also failed", radikoIdx);
    if (radikoIdx == config::kBootChannelIndex) {
      return;
    }
  }
  if (failedIndex == config::kBootChannelIndex) {
    LOGE("boot channel [%d] itself unreachable; leaving to resync path",
         failedIndex);
    return;
  }
  LOGE("falling back to boot channel [%d]", config::kBootChannelIndex);
  if (!switchToChannel(config::kBootChannelIndex, 0, /*isSeek=*/false,
                       /*isAbsolute=*/false, /*absEpoch=*/0)) {
    LOGE("boot channel fallback also failed");
  }
}

// タイムフリーのセッションが to(config::kTimefreeSessionSpanSec 先)に達して
// #EXT-X-ENDLIST に
// なり、積んだセグメントも使い切ったときに、続きの時刻で新しいセッションを張り直す。
// PCM
// の破棄もデコーダの作り直しもしない(同じ局・同じ形式の続きなので、そのまま繋がる)。
// s_timefreeHeadEpoch は最後に送り出したセグメントの終端(秒、切り上げ)。seek
// はそれより 1秒先にする: radiko は seek をセグメント境界へ切り下げるので、PDT
// のミリ秒を秒へ丸めた
// 誤差があっても、続きのセグメントの先頭に落ちる(1本前には戻らない)。
void continueTimefreeAfterEndList() {
  const int index = static_cast<int>(g_channelList.getCurrentIndex());
  const time_t resume = s_timefreeHeadEpoch + 1;
  time_t oldest = 0, newest = 0;
  timefreeWindowBounds(time(nullptr), &oldest, &newest);

  bool ok;
  if (resume >= newest) {
    // 続きがもうタイムフリーの直近端より新しい。ライブへ戻る。
    LOGI("timefree ENDLIST reached at %ld; caught up, returning to live",
         static_cast<long>(s_timefreeHeadEpoch));
    ok = switchToChannel(index, kSeekToLive, /*isSeek=*/true,
                         /*isAbsolute=*/false, /*absEpoch=*/0);
  } else {
    LOGI("timefree ENDLIST reached; continuing with a new session from %ld",
         static_cast<long>(resume));
    s_timefreeSkipBeforeEpoch = s_timefreeHeadEpoch;
    ok =
        switchToChannel(index, 0, /*isSeek=*/true, /*isAbsolute=*/true, resume);
    s_timefreeSkipBeforeEpoch = 0;
  }
  if (!ok) {
    fallbackToBootChannel(index);
  }
}

// JCBA/FMぷらぷら(WebSocket)受信の1サイクル分。HLS の playSegment()
// に相当するが、 セグメント単位ではなくフレーム(Ogg
// ページ、320ms)単位で戻るので、選局要求への応答が速い。
// 切断・無データが続いたら jcbaConnect() し直し、それも続くなら Wi-Fi
// ごと繋ぎ直す (HLS の「連続失敗 → wifiForceReconnect + resync」と同じ考え方)。
void jcbaStep() {
  if (!jcbaIsConnected()) {
    const Channel* ch =
        g_channelList.getChannel(g_channelList.getCurrentIndex());
    if (ch == nullptr || (ch->getPlatform() != ChannelPlatform::kJcba &&
                          ch->getPlatform() != ChannelPlatform::kFmPlapla)) {
      s_jcbaActive = false;  // 起こらないはず(切り替えは switchToChannel が
                             // s_jcbaActive を落とす)
      return;
    }
    LOGI("jcba reconnecting (%d consecutive failures)",
         s_jcbaConsecutiveFailures);
    JcbaService service = jcbaServiceForPlatform(ch->getPlatform());
    if (!jcbaConnect(s_sharedClient, s_sharedHttp, service, ch->getStreamId(),
                     config::kJcbaBurstSec)) {
      s_jcbaConsecutiveFailures++;
      if (s_jcbaConsecutiveFailures >= config::kMaxConsecutiveSegmentFailures) {
        LOGE("jcba reconnect keeps failing, forcing Wi-Fi reconnect");
        wifiForceReconnect();
        s_jcbaConsecutiveFailures = 0;
      }
      delay(config::kChannelSwitchRetryDelayMs);
      return;
    }
    s_jcbaLastDataMs = millis();
    return;
  }

  JcbaReadResult r = jcbaReadPage(config::kJcbaReadWaitMs, feedOpusPacket);
  uint32_t now = millis();
  switch (r) {
    case JcbaReadResult::kOk:
      s_jcbaLastDataMs = now;
      s_jcbaConsecutiveFailures = 0;
      // free/largestの差が時間とともに開いていくなら断片化が進行している兆候(HLS
      // と同じ)。 buffered は s_audioStreamBuffer
      // の未消費バイト(≈前方バッファ。burst ぶんが目安)。
      if (now - s_jcbaLastLogMs >= 5000) {
        s_jcbaLastLogMs = now;
        LOGI(
            "jcba pages=%lu packets=%lu frames=%lu buffered=%u free=%u "
            "largest=%u stack(fetch/decode)=%u/%u",
            static_cast<unsigned long>(jcbaPageCount()),
            static_cast<unsigned long>(jcbaPacketCount()),
            static_cast<unsigned long>(opusPlayerFrameCount()),
            static_cast<unsigned>(
                xStreamBufferBytesAvailable(s_audioStreamBuffer)),
            static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
            fetchStackFree(), decodeStackFree());
      }
      break;
    case JcbaReadResult::kNoData:
      if (now - s_jcbaLastDataMs > config::kJcbaStallTimeoutMs) {
        LOGW("jcba stream stalled for %lums; reconnecting",
             static_cast<unsigned long>(now - s_jcbaLastDataMs));
        jcbaDisconnect();
        s_jcbaConsecutiveFailures++;
      }
      break;
    case JcbaReadResult::kDisconnected:
      LOGW("jcba stream disconnected; reconnecting");
      jcbaDisconnect();
      s_jcbaConsecutiveFailures++;
      break;
  }
}

// ネットワーク取得タスクの1サイクル分。以前はArduinoのloop()から直接呼んでいたが、
// 別コアの専用タスク(fetchTaskFn)から繰り返し呼ぶように変更した。
void producerStep() {
  // 「停止」中に一度だけ解放処理をするためのラッチ。producerStep()(fetchタスク)だけが
  // 触るのでプレーンなboolでよい。関数staticなので呼び出しをまたいで持続する。
  static bool s_wasStopped = false;

  SwitchRequest req;
  if (xQueueReceive(s_channelQueue, &req, 0) == pdTRUE) {
    // 以降、switchToChannel()が戻るまでplaySegment()/jcbaStep()は呼ばれない。つまり
    // 旧チャンネル分のfeedDownstream()はもう二度と起こらないと確定する。
    s_oldChannelFetchStopped.store(true, std::memory_order_relaxed);
    if (!switchToChannel(req.index, req.seekDelta, req.isSeek, req.isAbsolute,
                         req.absEpoch)) {
      fallbackToBootChannel(req.index);
    }
    return;
  }

  // 「停止」中:
  // 選局要求(上のxQueueReceive)が来ない限りここで足踏みする。初回だけ
  // 共有TLS接続とJCBAのWebSocketを閉じる(以降は再接続しない)。
  if (s_stopped.load(std::memory_order_relaxed)) {
    if (!s_wasStopped) {
      LOGI("stop: closing shared connection%s",
           s_jcbaActive ? " and JCBA websocket" : "");
      if (s_jcbaActive) {
        jcbaDisconnect();
        s_jcbaActive = false;
      }
      s_sharedHttp.end();
      s_sharedClient.stop();
      s_queue.clear();
      s_wasStopped = true;
    }
    delay(200);
    return;
  }
  s_wasStopped = false;

  if (!wifiEnsureConnected()) {
    delay(200);
    return;
  }

  if (s_jcbaActive) {
    jcbaStep();
    return;
  }

  if (s_queue.size() <= 1 && !s_timefreeEndReached && playlistReloadDue()) {
    refreshPlaylist(false);
  }

  if (s_queue.empty()) {
    if (s_timefreeEndReached) {
      continueTimefreeAfterEndList();
      return;
    }
    delay(200);
    return;
  }

  hls::Segment seg = s_queue.front();
  s_queue.pop_front();

  size_t fedBytes = 0;
  SegmentResult result = playSegment(seg, fedBytes);
  if (result == SegmentResult::AbortedForChannelSwitch) {
    // 失敗としてカウントしない。s_nextSequenceToPlayも進めない
    // (このセグメントは再生できていない)。次のproducerStep()先頭で
    // 実際の切り替え処理が行われる。
    return;
  }

  // free/largestの差が時間とともに開いていくなら断片化が進行している兆候。
  unsigned freeHeap = static_cast<unsigned>(ESP.getFreeHeap());
  unsigned largestBlock =
      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  // タイムフリーの再生位置を、送り出したセグメントの終端(PDT +
  // EXTINF、秒へ切り上げ)へ
  // 進める。失敗したセグメントも読み飛ばして先へ進むので同じ扱い。PDT/EXTINF
  // が取れない
  // ときだけ従来どおり5秒の概算で進める(実際は約5.035秒なので1時間あたり25秒ほどずれる)。
  // ENDLIST
  // 後の張り直し(continueTimefreeAfterEndList)の位置もこの値から決める。
  if (s_playMode == PlayMode::kTimefree) {
    if (seg.programDateTime > 0 && seg.durationMs > 0) {
      s_timefreeHeadEpoch = seg.programDateTime +
                            static_cast<time_t>((seg.durationMs + 999) / 1000);
    } else if (result == SegmentResult::Ok) {
      s_timefreeHeadEpoch += config::kRadikoSegmentDurationSec;
    }
  }
  if (result == SegmentResult::Ok) {
    s_consecutiveSegmentFailures = 0;
    // fed>0なのにframes=0のまま増えないなら、バイトは流れているのにデコーダが
    // フレームを1つも出せていない(=データが壊れているか同期が取れていない)。
    LOGD(
        "segment %llu played OK fed=%uB queue=%u frames=%u free=%u largest=%u "
        "stack(fetch/decode)=%u/%u",
        static_cast<unsigned long long>(seg.sequence),
        static_cast<unsigned>(fedBytes), static_cast<unsigned>(s_queue.size()),
        static_cast<unsigned>(aacPlayerFrameCount()), freeHeap, largestBlock,
        fetchStackFree(), decodeStackFree());
  } else {
    s_consecutiveSegmentFailures++;
    LOGW("segment %llu failed fed=%uB consecutive=%d free=%u largest=%u",
         static_cast<unsigned long long>(seg.sequence),
         static_cast<unsigned>(fedBytes), s_consecutiveSegmentFailures,
         freeHeap, largestBlock);
  }
  s_nextSequenceToPlay = seg.sequence + 1;

  if (s_consecutiveSegmentFailures >= config::kMaxConsecutiveSegmentFailures ||
      s_consecutivePlaylistFailures >=
          config::kMaxConsecutivePlaylistFailures) {
    LOGE("too many consecutive failures, forcing Wi-Fi reconnect and resync");
    wifiForceReconnect();
    reauthorizeRadikoIfActive();
    resyncFromLiveEdge();
  }
}

void fetchTaskFn(void* /*param*/) {
  for (;;) {
    producerStep();
  }
}

// デコード側(decodeTaskFn
// だけが触る)の「いま初期化されているデコーダ」。起動時は main.cpp が
// aacPlayerBegin() 済みなので kAac。s_decoderResetPending
// はチャンネル切り替えの 破棄完了で立ち、次のレコードを受けたときに(その codec
// で)デコーダを作り直す。
// 破棄完了の時点ではなく「次のレコードを受けたとき」に作り直すのは、そのとき初めて
// 新チャンネルの codec が(レコードヘッダから)確定するため。
AudioCodec s_consumerCodec = AudioCodec::kAac;
bool s_decoderResetPending = false;

// レコードの codec
// に合わせてデコーダを用意する。切り替え時は旧デコーダを閉じてから (codec_arena
// を共用しているので、Helix を閉じずに Opus を初期化してはいけない)。 AAC→AAC
// の作り直しは、Helix 内部の frame_buffer に残った旧チャンネルの端数バイトを
// 捨てるため(クリアする公開 API が無い)。
void ensureDecoder(AudioCodec codec) {
  if (!s_decoderResetPending && codec == s_consumerCodec) {
    return;
  }
  LOGI("decoder (re)init for %s", codec == AudioCodec::kOpus ? "opus" : "aac");
  bool ok;
  if (codec == AudioCodec::kOpus) {
    aacPlayerEnd();
    ok = opusPlayerBegin();
  } else {
    opusPlayerEnd();
    ok = aacPlayerBegin();
  }
  if (!ok) {
    LOGE("decoder re-init failed");
  }
  s_consumerCodec = codec;
  s_decoderResetPending = false;
}

// s_audioStreamBuffer から n バイトぴったり受け取る。途中で破棄モードに入ったら
// (レコードの続きは捨てられる運命なので)false で抜ける。1バイトも来ないまま
// idleTimeout を超えた場合も false(呼び出し側は次のサイクルでやり直す)。
// 途中まで来ているなら producer
// が続きを書いている最中なので、捨てずに待ち続ける
// (ただし異常に長く止まったら諦めて false。以降のレコード境界はずれるが、
// 次の選局の破棄モードで復旧する)。
bool receiveExact(uint8_t* out, size_t n, TickType_t idleTimeout) {
  size_t got = 0;
  int stalls = 0;
  while (got < n) {
    if (s_discardMode.load(std::memory_order_relaxed)) {
      return false;
    }
    size_t r = xStreamBufferReceive(s_audioStreamBuffer, out + got, n - got,
                                    idleTimeout);
    if (r == 0) {
      if (got == 0) {
        return false;
      }
      if (++stalls >= 100) {
        LOGE("record truncated: got %u of %u bytes", static_cast<unsigned>(got),
             static_cast<unsigned>(n));
        return false;
      }
      continue;
    }
    got += r;
    stalls = 0;
  }
  return true;
}

// デコード専用タスクの1サイクル分。ネットワークI/Oは一切行わず、
// 別コアのfetchTaskFnが溜めたレコードをデコーダへ渡すだけ。
void consumerStep() {
  static uint8_t buf[config::kAudioRecordMaxPayload];
  // 「停止」中に一度だけデコーダを解放するためのラッチ。consumerStep()(デコード専用
  // タスク)だけが触るのでプレーンなboolでよい。
  static bool s_wasStopped = false;

  if (s_notifyPaused.load(std::memory_order_relaxed)) {
    // 通知音(モールス等)を鳴らしている間。s_audioStreamBufferからの取り出しも
    // デコーダへの供給もしない。取得タスクはバッファが埋まるとfeedDownstream()で
    // 待つ(音声データは失わない)。停止が数秒に及ぶとプレイリストが古くなり、
    // 復帰後にライブエッジへ再同期することがある(通知は短く保つ前提)。
    s_notifyPausedAck.store(true, std::memory_order_relaxed);
    vTaskDelay(pdMS_TO_TICKS(20));
    return;
  }
  s_notifyPausedAck.store(false, std::memory_order_relaxed);

  if (s_discardMode.load(std::memory_order_relaxed)) {
    // 旧チャンネルの残り(バッファに溜まった数秒分)をデコーダへ渡さず読み捨てる。
    // 空になった時点で、以降に届くのは新チャンネルのデータのみになるので通常動作に戻す。
    // ただし「空に見える」だけでは不十分で、s_oldChannelFetchStopped(fetch側が
    // 旧チャンネル分のfeedDownstream()をもう呼ばないと確定した)も揃うまでは
    // 読み捨てを続ける。
    size_t n = xStreamBufferReceive(s_audioStreamBuffer, buf, sizeof(buf), 0);
    if (n == 0 && s_oldChannelFetchStopped.load(std::memory_order_relaxed)) {
      s_discardMode.store(false, std::memory_order_relaxed);
      // デコーダの作り直しは次のレコード到着時(ensureDecoder)。ここでは予約だけ。
      // 以降に届くレコードは必ず新チャンネルのものなので、そのレコードの codec
      // で 作り直せば、旧チャンネルの端数バイト(Helix の frame_buffer
      // 内)も一緒に消える。
      LOGI("discard done; decoder reset pending");
      s_decoderResetPending = true;
      // ここから先にデコーダが出すPCMは、必ず新チャンネルのものになる。
      // i2sSpeakerFlush()で止めておいたenqueueの受け付けを再開する
      // (止めたままだと無音が続くので、必ず呼ぶ)。
      i2sSpeakerResume();
    }
    return;
  }

  // 「停止」中:
  // s_discardMode(上)より後で見る。resumeはswitchToChannel()経由で必ず
  // discardModeを立てるので、resume開始後はここに来る前に上のブロックへ入る。
  if (s_stopped.load(std::memory_order_relaxed)) {
    if (!s_wasStopped) {
      LOGI("stop: releasing decoder");
      if (s_consumerCodec == AudioCodec::kOpus) {
        opusPlayerEnd();
      } else {
        aacPlayerEnd();
      }
      // バッファに残っている分は再生しない(resume時に選局し直すので新規に取得される)。
      uint8_t discard[64];
      while (xStreamBufferReceive(s_audioStreamBuffer, discard, sizeof(discard),
                                  0) > 0) {
      }
      s_wasStopped = true;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    return;
  }
  s_wasStopped = false;

  // レコード単位で受ける。ヘッダが来なければ 50ms
  // で戻って上のフラグ類を見直す。
  AudioRecordHeader hdr;
  if (!receiveExact(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr),
                    pdMS_TO_TICKS(50))) {
    return;
  }
  size_t len =
      static_cast<size_t>(hdr.lenLo) | (static_cast<size_t>(hdr.lenHi) << 8);
  if (len == 0 || len > sizeof(buf) ||
      hdr.codec > static_cast<uint8_t>(AudioCodec::kOpus)) {
    // producer は必ず整合したレコードを書くので、ここに来たら境界がずれている。
    // 復旧手段は無いので、破棄モード(次の選局)まで読み捨てる。
    LOGE("bad record header codec=%u len=%u; stream desynced", hdr.codec,
         static_cast<unsigned>(len));
    return;
  }
  // ペイロードは producer がヘッダに続けて書いている最中なので、待てば必ず来る
  // (来ないのは破棄モードに入ったときだけ → receiveExact が false)。
  if (!receiveExact(buf, len, pdMS_TO_TICKS(1000))) {
    return;
  }
  AudioCodec codec = static_cast<AudioCodec>(hdr.codec);
  ensureDecoder(codec);
  if (codec == AudioCodec::kOpus) {
    opusPlayerFeedPacket(buf, len);
  } else {
    aacPlayerFeed(buf, len);
  }
}

void decodeTaskFn(void* /*param*/) {
  for (;;) {
    consumerStep();
  }
}

// 選局/シークの共通処理。indexは絶対インデックス(範囲外は無視)。
// isSeek=false … 通常の選局。isSeek=true …
// 同一局へのタイムフリーのシーク張り直し (index には現在の局を渡す。seekDelta
// は現在位置からのずらし秒、kSeekToLive でライブ復帰)。 戻り値:
// 切り替え要求をキューへ公開したら true。範囲外や、無料利用で在圏外
// (プレミアム専用)の局を指定した場合は何もせず false(現在の再生を維持する)。
bool requestChannelSwitch(int index, int32_t seekDelta = 0, bool isSeek = false,
                          bool isAbsolute = false, time_t absEpoch = 0) {
  size_t count = g_channelList.count();
  if (count == 0 || index < 0 || index >= static_cast<int>(count)) {
    return false;
  }
  // 無料利用で在圏外(プレミアム専用)の radiko 局は、接続しても 403 になるだけ。
  // 切り替え自体を拒否して現在の再生を維持する(シーク=同一局は対象外)。
  if (!isSeek) {
    const Channel* ch = g_channelList.getChannel(static_cast<size_t>(index));
    if (ch != nullptr && ch->getPlatform() == ChannelPlatform::kRadiko &&
        !radikoStationPlayable(
            g_channelList.getFreeAreaMask(static_cast<size_t>(index)))) {
      LOGW(
          "channel switch to [%d] %s rejected: not free in tuner area %s "
          "(needs radiko premium)",
          index, ch->getName(), radikoArea().c_str());
      return false;
    }
  }
  // 保存はここ(loop()と同じタスク)で予約する。実際の切り替えを行う
  // switchToChannel()はfetchTaskFn(別コア)から呼ばれるため、channel_store側の
  // 状態をそちらから触るとchannelStoreTick()(loop()側)と競合してしまう。
  // シーク(同一局)でも保存インデックスは変わらないので呼んで問題ない。
  channelStoreNotifyChanged(index);

  // フラグを先に張り、xQueueOverwrite()は最後に行う。順序が逆だと、
  // 「キューへ公開 → (別コアのproducerStep()が要求を取り出して
  // s_oldChannelFetchStopped=trueにする) → こちらがfalseで踏み潰す」
  // というレースが起きる。キューは既に空なので二度とtrueに戻らず、
  // consumerStep()が新チャンネルのデータを永久に読み捨て続けて無音になる。
  // 公開前ならproducerStep()は要求を観測できないので、この窓は生じない。
  s_oldChannelFetchStopped.store(false, std::memory_order_relaxed);
  s_discardMode.store(true, std::memory_order_relaxed);
  i2sSpeakerFlush();
  SwitchRequest reqObj{index, seekDelta, isSeek, isAbsolute, absEpoch};
  xQueueOverwrite(s_channelQueue, &reqObj);
  // デコーダのreset(aacPlayerBegin())はconsumerStep()側(デコード専用タスク)に移動した。
  // aacPlayerFeed()と同じタスクからしか触らないという不変条件を保つため。
  return true;
}

}  // namespace

bool radioStreamReserveMemory() {
  if (s_audioStreamStorage != nullptr) {
    return true;
  }
  // CPUからしか触らない(DMAは使わない)ので、PSRAMがあればそちらに置いて内部ヒープを節約する。
  s_audioStreamStorage = static_cast<uint8_t*>(
      heap_caps_malloc(config::kAudioStreamBufferBytes + 1,
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (s_audioStreamStorage != nullptr) {
    LOGI("audio stream buffer in PSRAM (%u bytes)",
         static_cast<unsigned>(config::kAudioStreamBufferBytes + 1));
  } else {
    s_audioStreamStorage = static_cast<uint8_t*>(
        heap_caps_malloc(config::kAudioStreamBufferBytes + 1, MALLOC_CAP_8BIT));
  }
  if (s_audioStreamStorage == nullptr) {
    LOGE("audio stream buffer alloc failed (%u bytes, largest free block=%u)",
         static_cast<unsigned>(config::kAudioStreamBufferBytes + 1),
         static_cast<unsigned>(
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    return false;
  }
  return true;
}

size_t radioStreamBufferedBytes() {
  return s_audioStreamBuffer != nullptr
             ? xStreamBufferBytesAvailable(s_audioStreamBuffer)
             : 0;
}

void radioStreamBegin(int initialChannelIndex) {
  if (!radioStreamReserveMemory()) {
    return;
  }
  s_channelQueue = xQueueCreateStatic(
      1, sizeof(SwitchRequest), s_channelQueueStorage, &s_channelQueueStruct);

  // 初期局がradikoでなくても先に認証してエリアを確定させる。未認証だと
  // radikoStationPlayable() が全局を再生可能と判定し、在圏外の局を選んでから
  // switchToChannel() 内でフォールバックすることになる(次局送りなどで発生)。
  // 失敗しても致命的ではない(初回のradiko選局時に再度認証を試みる)。
  ensureSharedClient();
  if (!retryBounded("radiko auth (boot)", [] {
        return radikoEnsureAuthorized(s_sharedClient, s_sharedHttp);
      })) {
    LOGW("radiko auth at boot failed; will retry on first radiko tune");
  }

  if (!switchToChannel(initialChannelIndex, 0, /*isSeek=*/false,
                       /*isAbsolute=*/false, /*absEpoch=*/0)) {
    fallbackToBootChannel(initialChannelIndex);
  }

  s_audioStreamBuffer = xStreamBufferCreateStatic(
      config::kAudioStreamBufferBytes, 1, s_audioStreamStorage,
      &s_audioStreamBufferStruct);
  if (s_audioStreamBuffer == nullptr) {
    LOGE("xStreamBufferCreateStatic failed (should not happen; static alloc)");
  }

  s_fetchTask = xTaskCreateStaticPinnedToCore(
      fetchTaskFn, "hls_fetch", config::kFetchTaskStackBytes, nullptr,
      config::kFetchTaskPriority, s_fetchTaskStack, &s_fetchTaskBuffer,
      config::kFetchTaskCore);
  s_decodeTask = xTaskCreateStaticPinnedToCore(
      decodeTaskFn, "audio_decode", config::kDecodeTaskStackBytes, nullptr,
      config::kDecodeTaskPriority, s_decodeTaskStack, &s_decodeTaskBuffer,
      config::kDecodeTaskCore);
  LOGI("fetch/decode tasks started on core %d, free heap=%u",
       config::kFetchTaskCore, static_cast<unsigned>(ESP.getFreeHeap()));
}

bool radioStreamSetChannel(int index) { return requestChannelSwitch(index); }

// 再生を止め、解放できるリソース(共有TLS接続・JCBAのWebSocket・AAC/OpusデコーダのHelix/
// Opusワークバッファ)を解放する。Wi-Fi接続は維持する(切断すると再接続に数秒かかるため、
// 呼び出し元が求める「すぐ止めてすぐ再開できる」性質と噛み合わない)。
// I2Sは即座にi2sSpeakerFlush()するので体感的にはここで音が止まる。TLS/デコーダの解放は
// producerStep()/consumerStep()側が次のサイクルで行う(数十ms以内)。
// タイムフリー再生中に呼んだ場合、radioStreamResume()はライブへ戻る(タイムフリー位置は
// 保持しない)。どのコア/タスクから呼んでもよい。
void radioStreamStop() {
  s_stopped.store(true, std::memory_order_relaxed);
  i2sSpeakerFlush();
}

// radioStreamStop()の後、直前に選局していた局へ選局し直す形で再生を再開する
// (switchToChannel()を再度通すので、認証・プレイリスト取得等をすべてやり直す)。
// 停止中でなくても無害(単なる同一局への選局し直しになる)。どのコア/タスクから呼んでもよい。
void radioStreamResume() {
  requestChannelSwitch(static_cast<int>(g_channelList.getCurrentIndex()));
}

// 局リストはコンパイル時テーブル(channel_list.cppでstatic_assert(kStationCount
// > 0))なので
// count()は常に1以上。next/prevは必ず有効なインデックスになり、失敗のケースは無い。
// 無料利用時は在圏外(プレミアム専用)の局を飛ばして次/前の再生可能な局へ送る
// (在圏局は必ず存在するので playableChannelFrom() は -1
// を返さない想定だが、念のため
//  戻り値をそのまま使う)。
int radioStreamNextChannel() {
  int n = static_cast<int>(g_channelList.count());
  int start = (static_cast<int>(g_channelList.getCurrentIndex()) + 1) % n;
  int next = playableChannelFrom(start, +1);
  if (next < 0) {
    next = start;
  }
  requestChannelSwitch(next);
  return next;
}

int radioStreamPreviousChannel() {
  int n = static_cast<int>(g_channelList.count());
  int start = (static_cast<int>(g_channelList.getCurrentIndex()) - 1 + n) % n;
  int prev = playableChannelFrom(start, -1);
  if (prev < 0) {
    prev = start;
  }
  requestChannelSwitch(prev);
  return prev;
}

// エリア送り/プラットフォーム送りの共通処理。first(切り替え先の先頭局)から前方に
// 再生できる局を探して切り替える。先頭局が無料利用で在圏外(プレミアム専用)のradiko局
// だと requestChannelSwitch()
// に弾かれて何も起きない(無音で気付けない)ので、next/prevと
// 同じく飛ばして最寄りの再生可能局へ移る(その結果、隣のエリアへ抜けることもある)。
static int switchToPlayableFrom(int first) {
  int target = playableChannelFrom(first, +1);
  if (target < 0) {
    target = first;
  }
  requestChannelSwitch(target);
  return target;
}

int radioStreamNextArea() {
  return switchToPlayableFrom(g_channelList.nextAreaFirstIndex());
}

int radioStreamPreviousArea() {
  return switchToPlayableFrom(g_channelList.previousAreaFirstIndex());
}

int radioStreamNextPlatform() {
  return switchToPlayableFrom(g_channelList.nextPlatformFirstIndex());
}

int radioStreamPreviousPlatform() {
  return switchToPlayableFrom(g_channelList.previousPlatformFirstIndex());
}

// タイムフリーのシーク。現在の再生位置(ライブ再生中ならその時点のnow)から
// deltaSec 秒 ずらした位置へ、同じ局のまま再生し直す。負で過去、正で未来方向。
// radikoチャンネルかつシステム時刻が同期済みのときだけ有効(それ以外は無視)。
void radioStreamSeekRelative(int deltaSec) {
  const Channel* ch = g_channelList.getChannel(g_channelList.getCurrentIndex());
  if (ch == nullptr || ch->getPlatform() != ChannelPlatform::kRadiko) {
    LOGW("seek ignored: current channel is not radiko");
    return;
  }
  if (!timeSyncIsValid()) {
    LOGW("seek ignored: system clock not synced");
    return;
  }
  requestChannelSwitch(static_cast<int>(g_channelList.getCurrentIndex()),
                       static_cast<int32_t>(deltaSec),
                       /*isSeek=*/true);
}

// 現在の局のタイムフリー再生を、絶対的な壁時計時刻(UNIX時間)へ飛ばす。
// 相対シークと違い現在位置を基準にしないので、resolveSeekTarget は通さない
// (switchToChannel が isAbsolute 経路でウィンドウへクランプするだけ)。
// radikoチャンネルかつシステム時刻が同期済みのときだけ有効(それ以外は何もせず
// false)。
bool radioStreamSeekAbsolute(time_t targetEpoch) {
  const Channel* ch = g_channelList.getChannel(g_channelList.getCurrentIndex());
  if (ch == nullptr || ch->getPlatform() != ChannelPlatform::kRadiko) {
    LOGW("absolute seek ignored: current channel is not radiko");
    return false;
  }
  if (!timeSyncIsValid()) {
    LOGW("absolute seek ignored: system clock not synced");
    return false;
  }
  return requestChannelSwitch(
      static_cast<int>(g_channelList.getCurrentIndex()), /*seekDelta=*/0,
      /*isSeek=*/true, /*isAbsolute=*/true, /*absEpoch=*/targetEpoch);
}

// now を基準にしたタイムフリー到達可能ウィンドウ。radioStreamSeekAbsolute()
// の内部クランプと 同じ境界を、呼び出し側の事前チェック用に公開する。
void radioStreamTimefreeWindow(time_t now, time_t* oldest, time_t* newest) {
  timefreeWindowBounds(now, oldest, newest);
}

// タイムフリー再生を終えてライブ再生へ戻す。ライブ再生中に呼んでも無害。
void radioStreamReturnToLive() {
  const Channel* ch = g_channelList.getChannel(g_channelList.getCurrentIndex());
  if (ch == nullptr || ch->getPlatform() != ChannelPlatform::kRadiko) {
    return;
  }
  requestChannelSwitch(static_cast<int>(g_channelList.getCurrentIndex()),
                       kSeekToLive, /*isSeek=*/true);
}

void radioStreamPauseForNotification() {
  s_notifyPausedAck.store(false, std::memory_order_relaxed);
  s_notifyPaused.store(true, std::memory_order_relaxed);
  // consumerStep()が現在のサイクル(最大で受信タイムアウト50ms)を終え、供給停止を
  // 確認応答するまで待つ。ここを抜けた時点で、デコードタスクはもうi2s_speakerへ
  // enqueueしていない = 呼び出し元が唯一のプロデューサになれる。
  for (int i = 0; i < 50 && !s_notifyPausedAck.load(std::memory_order_relaxed);
       ++i) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void radioStreamResumeAfterNotification() {
  s_notifyPaused.store(false, std::memory_order_relaxed);
}
