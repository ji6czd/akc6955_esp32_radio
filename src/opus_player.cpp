// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_OPUS
#define LOG_MODULE_TAG "OPUS"
#include "opus_player.h"

#include <opus.h>

#include "codec_arena.h"
#include "i2s_speaker.h"
#include "log_config.h"

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
// 1回の opus_decode() が返せる最大サンプル数/ch。JCBA は 20ms(960)固定(実測、
// 全パケット)。Opus 仕様上は 120ms(5760)まであり得るが、それに備えると PCM バッファが
// 23KB になるので 20ms に絞る。もし長いパケットが来たら opus_decode() が
// OPUS_BUFFER_TOO_SMALL を返して、そのパケットを捨てる(下の LOGW で分かる)。
constexpr int kMaxFrameSamples = 960;

OpusDecoder* g_decoder = nullptr;
uint32_t g_frameCount = 0;
bool g_firstFrameLogged = false;
int16_t g_pcm[kMaxFrameSamples * kChannels];

}  // namespace

bool opusPlayerBegin() {
  int need = opus_decoder_get_size(kChannels);
  if (need <= 0 || static_cast<size_t>(need) > codecArenaBytes()) {
    LOGE("Opus decoder needs %d bytes but codec arena is %u; raise config::kCodecArenaBytes", need,
         static_cast<unsigned>(codecArenaBytes()));
    g_decoder = nullptr;
    return false;
  }
  OpusDecoder* dec = reinterpret_cast<OpusDecoder*>(codecArena());
  int err = opus_decoder_init(dec, kSampleRate, kChannels);
  if (err != OPUS_OK) {
    LOGE("opus_decoder_init failed: %s", opus_strerror(err));
    g_decoder = nullptr;
    return false;
  }
  static bool s_sizeLogged = false;
  if (!s_sizeLogged) {
    s_sizeLogged = true;
    LOGI("Opus decoder state %d bytes in %u-byte codec arena (%s)", need, static_cast<unsigned>(codecArenaBytes()),
         opus_get_version_string());
  }
  g_decoder = dec;
  g_frameCount = 0;
  g_firstFrameLogged = false;
  return true;
}

void opusPlayerEnd() { g_decoder = nullptr; }

void opusPlayerFeedPacket(const uint8_t* data, size_t len) {
  if (g_decoder == nullptr || len == 0) {
    return;
  }
  int n = opus_decode(g_decoder, data, static_cast<opus_int32>(len), g_pcm, kMaxFrameSamples, 0);
  if (n <= 0) {
    // 壊れたパケットは飛ばす(PLC は入れていない。20ms 抜けるだけ)。
    LOGW("opus_decode failed: %s (len=%u)", opus_strerror(n), static_cast<unsigned>(len));
    return;
  }
  ++g_frameCount;
  if (!g_firstFrameLogged) {
    LOGI("first frame after begin: %dHz %dch samples/ch=%d", kSampleRate, kChannels, n);
    g_firstFrameLogged = true;
  }
  i2sSpeakerEnqueue(g_pcm, static_cast<size_t>(n) * kChannels, kSampleRate);
}

uint32_t opusPlayerFrameCount() { return g_frameCount; }
