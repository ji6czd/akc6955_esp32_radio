// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_AAC
#define LOG_MODULE_TAG "AAC"
#include "aac_player.h"

#include <AACDecoderHelix.h>
#include <Arduino.h>

#include "codec_arena.h"
#include "i2s_speaker.h"
#include "log_config.h"

namespace {

// どちらもaacPlayerBegin()(=チャンネル切り替えのたびに呼ばれる)でリセットする。
// 起動後1回きりではなく「切り替えごとの最初のフレーム」を出したいため。
bool g_firstFrameLogged = false;
uint32_t g_frameCount = 0;

// aacPlayerBegin()の実行中だけtrue。AACDecoderHelix::begin()は内部でend()を呼び、
// end()はAACFreeDecoder()の前にflush()で残バイトをデコードしてコールバックを叩く。
// そのフレームは旧チャンネルのものなので、i2sSpeakerFlush()で破棄量を確定した後に
// enqueueされると新チャンネルの音として再生されてしまう。ここで塞ぐ。
bool g_dropFrames = false;

// i2sSpeakerEnqueue()はPCMリングバッファへコピーして返るだけ(実際のI2S書き込みは
// 専用タスクが行う)ため、M5.Speaker.playRaw()と同様に戻ってきた時点でpcm_bufferは
// 再利用してよい。
void onAacFrame(_AACFrameInfo& info, short* pcm_buffer, size_t len, void* /*ref*/) {
  if (g_dropFrames) {
    // aacPlayerBegin()内のend()が呼ぶflush()から来たフレーム。中身は旧チャンネルの
    // 端数バイトなので、新チャンネルの音として鳴らさずに捨てる。
    return;
  }
  ++g_frameCount;
  if (!g_firstFrameLogged) {
    LOGI("first frame after begin: %dHz %dch samples=%u, free=%d", info.sampRateOut, info.nChans,
         static_cast<unsigned>(len), static_cast<unsigned>(ESP.getFreeHeap()));
    g_firstFrameLogged = true;
  }

  // SBR無効ビルドのHelixはHE-AACのコア層だけをデコードするため、局によって
  // sampRateOutが変わりうる(SBR前提の48kHz申告 vs 実際のコア層レート)。
  // 実際の書き込み時に適用されるサンプルレートはi2sSpeakerEnqueue()側で管理する。
  i2sSpeakerEnqueue(reinterpret_cast<const int16_t*>(pcm_buffer), len, static_cast<uint32_t>(info.sampRateOut));
}

// Helix のデコーダ状態(AACDecInfo + PSInfoBase、約29KB)をヒープではなく codec_arena の
// 静的領域に置く版。素の AACDecoderHelix は begin() のたびに AACInitDecoder()(malloc)、
// end() のたびに AACFreeDecoder()(free)を行うので、チャンネル切り替えごとに ~30KB の
// 確保・解放が走っていた。ここでは allocateDecoder() を AACInitDecoderPre()(呼び出し側
// バッファ)に、end() を「何も free しない」版に差し替える。
// フレーム入力バッファ(2100B)と PCM 出力バッファ(8KB)は CommonHelix 側の Vector で
// ヒープに取られる(ライブラリの構造上、差し替え口が無い)。ただし解放は一度もしない:
// 素の end() は resize(0) で捨てて次の begin() で new[] し直すが、Opus 再生中はヒープが
// 薄く(TLS セッション + 断片化で largest ≈8KB)、8KB の new[] が bad_alloc → abort した
// (実機で再現)。Vector::resize() は容量が足りていれば再確保しないので、起動時
// (Wi-Fi 前、ヒープが一番空いているとき)に取ったバッファをそのまま持ち続ける。
class ArenaAacDecoder : public libhelix::AACDecoderHelix {
 public:
  using AACDecoderHelix::AACDecoderHelix;
  ~ArenaAacDecoder() override { end(); }

  void end() override {
    // 素の end() は flush()(frame_buffer に残った端数バイトを最後までデコード)を呼ぶが、
    // ここでは呼ばない。残りは旧チャンネルのフレーム途中で切れたバイト列で、出力は
    // どのみち捨てる(aacPlayerEnd の g_dropFrames)うえ、切れたフレームを AACDecode() に
    // 食わせると Helix のビットストリーム読み出しが範囲外を踏んで LoadStoreError で落ちた
    // (実機で再現: RefillBitstreamCache ← raac_DecodeNoiselessData ← flush)。
    // 残りバイトは次の begin() の frame_buffer.reset() で消える。
    // AACFreeDecoder() も呼ばない(静的領域を free するとヒープが壊れる)。
    decoder = nullptr;
    // CommonHelix::end() も呼ばない(frame_buffer/pcm_buffer を解放してしまう)。
    // 次の begin() が frame_buffer.reset() と両バッファの memset をやり直す。
    active = false;
    memset(&aacFrameInfo, 0, sizeof(aacFrameInfo));
  }

 protected:
  bool allocateDecoder() override {
    if (decoder == nullptr) {
      int need = AACGetDecoderSize();
      if (need <= 0 || static_cast<size_t>(need) > codecArenaBytes()) {
        LOGE("AAC decoder needs %d bytes but codec arena is %u; raise config::kCodecArenaBytes", need,
             static_cast<unsigned>(codecArenaBytes()));
        return false;
      }
      decoder = AACInitDecoderPre(codecArena(), static_cast<int>(codecArenaBytes()));
    }
    memset(&aacFrameInfo, 0, sizeof(aacFrameInfo));
    return decoder != nullptr;
  }
};

ArenaAacDecoder g_decoder(onAacFrame);

}  // namespace

bool aacPlayerBegin() {
  static bool s_sizeLogged = false;
  if (!s_sizeLogged) {
    s_sizeLogged = true;
    LOGI("AAC decoder state %d bytes in %u-byte codec arena", AACGetDecoderSize(),
         static_cast<unsigned>(codecArenaBytes()));
  }
  g_firstFrameLogged = false;
  g_frameCount = 0;
  g_dropFrames = true;
  bool ok = g_decoder.begin();
  g_dropFrames = false;
  return ok;
}

void aacPlayerEnd() {
  // ArenaAacDecoder::end() は flush() しないのでコールバックは来ないはずだが、
  // 万一来ても旧チャンネルの端数なので捨てる(begin() と同じ扱い)。
  g_dropFrames = true;
  g_decoder.end();
  g_dropFrames = false;
}

void aacPlayerFeed(const uint8_t* data, size_t len) { g_decoder.write(data, len); }

uint32_t aacPlayerFrameCount() { return g_frameCount; }
