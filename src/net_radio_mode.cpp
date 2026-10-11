// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_MAIN
#define LOG_MODULE_TAG "NETRADIO"
#include "net_radio_mode.h"

#include <Arduino.h>

#include <ctime>

#include "channel_list.h"
#include "config.h"
#include "log_config.h"
#include "morse_code.h"
#include "radio_stream.h"
#include "time_sync.h"

namespace {

// 起動時(最初にこのモードへ入ったとき)に再生する局の直接選局コード。
constexpr int kBootTuneCode = 1006;

// パネルのキー/ボタンから変換した、このモード内の操作。
enum class Command : uint8_t {
  kNone = 0,
  kStop,
  kNextChannel,
  kPreviousChannel,
  kNum0,
  kNum1,
  kNum2,
  kNum3,
  kNum4,
  kNum5,
  kNum6,
  kNum7,
  kNum8,
  kNum9,
  // FuncA/B = 前/次のエリア(同じプラットフォーム内)、FuncC/D =
  // 前/次のプラットフォーム。
  kFuncA,
  kFuncB,
  kFuncC,
  kFuncD,
  // 以降のテンキー入力を絶対日時
  // ddhhmm(6桁)のタイムフリージャンプとして解釈する
  // 入力へ入る。今はどのキーにも割り当てていない。専用キーを決めたらそこへ割り当てる。
  kDateJump,
  // タイムフリー(radiko)のシーク。prev=過去へ /
  // next=未来へ。押すたびにその分ずれる
  kSeekPrev1,
  kSeekNext1,
  kSeekPrev10,
  kSeekNext10,
  kSeekPrev60,
  kSeekNext60,
  kSeekPrev360,
  kSeekNext360,
};

void nop() {}

void morsePlayChannelId(int index) {
  const Channel* ch = g_channelList.getChannel(index);
  ASSERT(
      ch !=
      nullptr);  // 呼び出し元が範囲内のindexを渡す契約(直接選局は検証済み、next/prevは
                 // % count)
  const char* currentId = ch->getStreamId();
  morsePlay(currentId);
  // 液晶が無いので、いまどのコードにいるかはここのログで追えるようにしておく。
  LOGI("playing current channel in morse: %s (%04d %s)", currentId,
       g_channelList.tuneCodeForIndex(static_cast<size_t>(index)),
       ch->getName());
}

// --- テンキー入力(通常=4桁直接選局 / kDateJump後=6桁日時ジャンプ)
// -------------- 入力中の桁を貯めるバッファ。桁が来るたび lastDigitMs
// を更新し、commandTick()が config::kDirectTuneTimeoutMs
// の無入力で破棄する。桁がそろった時点で確定する。
//   kChannel  … 4桁の直接選局コード(プラットフォーム番号1桁 + 局番号3桁。例:
//   2013 =
//               radikoの13局目。channel_list.h参照)。通常のテンキー入力。
//   kDateJump … 6桁 ddhhmm。Command::kDateJump
//   で入り、現在局のタイムフリー再生位置を
//               指定する(現在は割り当てキー無し)。
// バッファは6桁ぶん確保し、両モードで共有する。

enum class DigitEntryKind : uint8_t { kChannel, kDateJump };

char s_digits[config::kDateJumpDigits];
uint8_t s_digitCount = 0;
uint8_t s_expectedDigits = config::kDirectTuneDigits;
DigitEntryKind s_entryKind = DigitEntryKind::kChannel;
uint32_t s_lastDigitMs = 0;

// 入力状態を「チャンネル4桁」の初期状態へ戻す。桁数もkindも消す。
void resetEntry() {
  s_digitCount = 0;
  s_entryKind = DigitEntryKind::kChannel;
  s_expectedDigits = config::kDirectTuneDigits;
}

// cmd が数字キーなら 0〜9、それ以外は -1。
int digitForCommand(Command cmd) {
  switch (cmd) {
    case Command::kNum0:
      return 0;
    case Command::kNum1:
      return 1;
    case Command::kNum2:
      return 2;
    case Command::kNum3:
      return 3;
    case Command::kNum4:
      return 4;
    case Command::kNum5:
      return 5;
    case Command::kNum6:
      return 6;
    case Command::kNum7:
      return 7;
    case Command::kNum8:
      return 8;
    case Command::kNum9:
      return 9;
    default:
      return -1;
  }
}

// 入力中の桁を捨てる。announce=true かつ桁が貯まっていたときだけ「?」を鳴らす
// (範囲外確定・タイムアウト用)。数字以外のコマンドで割り込まれた場合は
// announce=false (ユーザーの意図的な操作なので無音)。桁ゼロで放置された
// kDateJump kind も必ず消す。
void discardDigits(bool announce) {
  bool hadDigits = s_digitCount > 0;
  resetEntry();
  if (announce && hadDigits) {
    morsePlay("?");
  }
}

// 4桁を直接選局コードとして数値化し、選局を確定する。未割り当てのコードなら「?」。
// 呼び出し前に s_digitCount==kDirectTuneDigits。
void commitDirectTune() {
  int code = 0;
  for (uint8_t i = 0; i < config::kDirectTuneDigits; ++i) {
    code = code * 10 + (s_digits[i] - '0');
  }
  resetEntry();

  int index = g_channelList.indexForTuneCode(code);
  if (index < 0) {
    LOGW("direct tune: %04d is not assigned", code);
    morsePlay("?");
    return;
  }
  LOGI("direct tune: %04d -> index %d", code, index);
  if (!radioStreamSetChannel(index)) {
    // 無料利用で在圏外(プレミアム専用)の局。切り替えは行われないので「?」で知らせる。
    LOGW("direct tune: %04d rejected (radiko premium required)", code);
    morsePlay("?");
    return;
  }
  morsePlayChannelId(index);
}

// --- kDateJump: 6桁 ddhhmm の絶対日時タイムフリージャンプ
// ---------------------

// 日時ジャンプが使える条件(現在局がradiko / 時刻同期済み)。
bool dateJumpAvailable() {
  const Channel* ch = g_channelList.getChannel(g_channelList.getCurrentIndex());
  return ch != nullptr && ch->getPlatform() == ChannelPlatform::kRadiko &&
         timeSyncIsValid();
}

// kDateJump: 以降のテンキー入力を絶対日時 ddhhmm として解釈するモードへ入る。
// 条件を満たさなければ何もせず「?」(途中のチャンネル入力があれば無音で捨てる)。
void beginDateJumpEntry() {
  if (!dateJumpAvailable()) {
    resetEntry();
    morsePlay("?");
    return;
  }
  resetEntry();
  s_entryKind = DigitEntryKind::kDateJump;
  s_expectedDigits = config::kDateJumpDigits;
  s_lastDigitMs = millis();  // 桁ゼロでもタイムアウト監視の起点にする
}

enum class DateJumpError : uint8_t { kNone, kBadTime, kBadDay, kOutOfWindow };

// dd/hh/mm から絶対エポック秒を作る。システムTZはJST-9なので localtime_r/mktime
// は JST壁時計で動く。今月で組み立てて未来なら1か月戻す。再localtime_rで
// dd/hh/mm が 一致するか確認し、2/30・31日のない月 等の正規化ずれを弾く。
DateJumpError buildDateJumpEpoch(int dd, int hh, int mm, time_t* outEpoch) {
  if (hh > 23 || mm > 59) {
    return DateJumpError::kBadTime;
  }
  if (dd < 1 || dd > 31) {
    return DateJumpError::kBadDay;
  }

  const time_t now = time(nullptr);
  struct tm baseTm = {};
  localtime_r(&now, &baseTm);  // 現在のJST年/月を土台にする

  auto build = [&](int monthDelta, time_t* out) -> bool {
    struct tm t = baseTm;
    t.tm_mon += monthDelta;  // -1 は mktime が前年12月へ正規化する
    t.tm_mday = dd;
    t.tm_hour = hh;
    t.tm_min = mm;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    time_t e = mktime(&t);
    if (e == static_cast<time_t>(-1)) {
      return false;
    }
    *out = e;
    return true;
  };

  time_t target = 0;
  if (!build(0, &target)) {
    return DateJumpError::kBadDay;
  }
  if (target > now && !build(-1, &target)) {
    return DateJumpError::kBadDay;
  }

  // 日付正規化ずれ検出(例: 2月31日 -> 3月頭にずれる)
  struct tm chk = {};
  localtime_r(&target, &chk);
  if (chk.tm_mday != dd || chk.tm_hour != hh || chk.tm_min != mm) {
    return DateJumpError::kBadDay;
  }

  // タイムフリー到達可能ウィンドウ判定(範囲外は拒否 = 再生しない)
  time_t oldest = 0, newest = 0;
  radioStreamTimefreeWindow(now, &oldest, &newest);
  if (target < oldest || target > newest) {
    return DateJumpError::kOutOfWindow;
  }

  *outEpoch = target;
  return DateJumpError::kNone;
}

// 6桁そろった時点で呼ばれる(呼び出し前に s_digitCount==kDateJumpDigits)。
void commitDateJump() {
  const int dd = (s_digits[0] - '0') * 10 + (s_digits[1] - '0');
  const int hh = (s_digits[2] - '0') * 10 + (s_digits[3] - '0');
  const int mm = (s_digits[4] - '0') * 10 + (s_digits[5] - '0');
  resetEntry();  // 何かを再入させる前に必ず初期化

  if (!dateJumpAvailable()) {
    LOGW("date jump: guard failed at commit");
    morsePlay("?");
    return;
  }

  time_t target = 0;
  DateJumpError err = buildDateJumpEpoch(dd, hh, mm, &target);
  if (err != DateJumpError::kNone) {
    LOGW("date jump %02d%02d%02d rejected (err=%d)", dd, hh, mm,
         static_cast<int>(err));
    morsePlay("?");
    return;
  }

  LOGI("date jump %02d%02d%02d -> epoch %ld", dd, hh, mm,
       static_cast<long>(target));
  if (!radioStreamSeekAbsolute(target)) {
    morsePlay("?");
  }
  // 成功時は無音(既存シーク系と同じ)
}

// 数字キー1つを取り込む。期待桁数そろったら確定する(モードにより選局 or
// 日時ジャンプ)。
void pushDigit(int d) {
  // commit*()/discardDigits()が必ず0へ戻し、期待桁数で必ずcommitするので
  // ここに来るとき s_digitCount は 0..s_expectedDigits-1。将来その不変条件を
  // 壊す変更が入ったら、配列オーバーランになる前にここで落とす。
  ASSERT(s_digitCount < s_expectedDigits);
  s_digits[s_digitCount++] = static_cast<char>('0' + d);
  s_lastDigitMs = millis();
  if (s_digitCount >= s_expectedDigits) {
    if (s_entryKind == DigitEntryKind::kDateJump) {
      commitDateJump();
    } else {
      commitDirectTune();
    }
  }
}

// 暫定割当。実機で配置を確認しながら調整すること。
Command commandForKey(PanelKey key) {
  switch (key) {
    case PanelKey::kTact1:
      return Command::kNum1;
    case PanelKey::kTact2:
      return Command::kNum2;
    case PanelKey::kTact3:
      return Command::kNum3;
    case PanelKey::kTact4:
      return Command::kNum4;
    case PanelKey::kTact5:
      return Command::kNum5;
    case PanelKey::kTact6:
      return Command::kNum6;
    case PanelKey::kTact7:
      return Command::kNum7;
    case PanelKey::kTact8:
      return Command::kNum8;
    case PanelKey::kTact9:
      return Command::kNum9;
    case PanelKey::kTact0:
      return Command::kNum0;
    case PanelKey::kFunc1:
      return Command::kFuncA;
    case PanelKey::kFunc2:
      return Command::kFuncB;
    case PanelKey::kFunc3:
      return Command::kFuncC;
    case PanelKey::kFunc4:
      return Command::kFuncD;
    case PanelKey::kFunc5:
      return Command::kStop;
    case PanelKey::kEncoderPlus:
      return Command::kNextChannel;
    case PanelKey::kEncoderMinus:
      return Command::kPreviousChannel;
    default:
      return Command::kNone;
  }
}

void handleCommand(Command cmd) {
  // 数字キーはテンキー直接選局の桁として取り込み、他のコマンド処理へは進めない。
  int digit = digitForCommand(cmd);
  if (digit >= 0) {
    pushDigit(digit);
    return;
  }
  // kDateJumpは「以降の入力は日時指定」への切り替え。discardDigits()より前で捕捉する
  // (入力途中の桁の扱いは beginDateJumpEntry() 側で決める)。
  if (cmd == Command::kDateJump) {
    beginDateJumpEntry();
    return;
  }
  // 数字以外の有効なコマンドが来たら、入力途中の桁は捨てる(無音)。
  if (cmd != Command::kNone) {
    discardDigits(false);
  }

  int index;
  switch (cmd) {
    case Command::kNextChannel:
      index = radioStreamNextChannel();
      morsePlayChannelId(index);
      break;
    case Command::kPreviousChannel:
      index = radioStreamPreviousChannel();
      morsePlayChannelId(index);
      break;
    case Command::kSeekPrev1:
      radioStreamSeekRelative(-1 * 60);
      break;
    case Command::kSeekNext1:
      radioStreamSeekRelative(1 * 60);
      break;
    case Command::kSeekPrev10:
      radioStreamSeekRelative(-10 * 60);
      break;
    case Command::kSeekNext10:
      radioStreamSeekRelative(10 * 60);
      break;
    case Command::kSeekPrev60:
      radioStreamSeekRelative(-60 * 60);
      break;
    case Command::kSeekNext60:
      radioStreamSeekRelative(60 * 60);
      break;
    case Command::kSeekPrev360:
      radioStreamSeekRelative(-360 * 60);
      break;
    case Command::kSeekNext360:
      radioStreamSeekRelative(360 * 60);
      break;
    // エリア送り(同じプラットフォーム内)とプラットフォーム送り。どちらも切り替え先の
    // 先頭局へ移り、next/prevと同じく切り替え先のIDをモールスで知らせる(先頭局が無料
    // 利用で聴けないradiko局なら radio_stream 側がその先の再生可能局へ送る)。
    case Command::kFuncA:
      index = radioStreamPreviousArea();
      morsePlayChannelId(index);
      break;
    case Command::kFuncB:
      index = radioStreamNextArea();
      morsePlayChannelId(index);
      break;
    case Command::kFuncC:
      index = radioStreamPreviousPlatform();
      morsePlayChannelId(index);
      break;
    case Command::kFuncD:
      index = radioStreamNextPlatform();
      morsePlayChannelId(index);
      break;
    case Command::kStop:
      radioStreamStop();
      break;
    // kNum0〜kNum9 / kDateJump は上で return
    // 済み。ここに来るのは未割り当ての操作だけ。
    case Command::kNone:
    default:
      nop();
      break;
  }
}

void commandTick() {
  // 何も入力していない通常状態(kChannel かつ桁ゼロ)なら監視不要。
  // bare kDateJump 待ち(桁ゼロ)は監視対象 = タイムアウトで kChannel へ戻す。
  if (s_digitCount == 0 && s_entryKind == DigitEntryKind::kChannel) {
    return;
  }
  if (millis() - s_lastDigitMs >= config::kDirectTuneTimeoutMs) {
    LOGI("digit entry timed out (kind=%d, %u digit(s))",
         static_cast<int>(s_entryKind), static_cast<unsigned>(s_digitCount));
    discardDigits(
        true);  // 桁ゼロ(bare kDateJump)なら discardDigits が無音で戻す
  }
}

class NetRadioMode : public Mode {
 public:
  const char* name() const override { return "net radio"; }

  void enter(int) override {
    if (!m_started) {
      const int index = g_channelList.indexForTuneCode(kBootTuneCode);
      if (index < 0) {
        LOGE("tune code %d not found", kBootTuneCode);
        return;
      }
      radioStreamBegin(index);
      m_started = true;
    } else {
      radioStreamResume();
    }
  }

  void exit() override {
    discardDigits(false);
    if (m_started) radioStreamStop();
  }

  void onEvent(const PanelEvent& ev) override {
    if (ev.type == PanelEventType::kKeyDown) {
      handleCommand(commandForKey(static_cast<PanelKey>(ev.id)));
    }
  }

  void tick() override { commandTick(); }

 private:
  bool m_started = false;
};

}  // namespace

Mode& netRadioMode() {
  static NetRadioMode mode;
  return mode;
}
