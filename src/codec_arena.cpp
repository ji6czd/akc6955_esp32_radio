#include "codec_arena.h"

#include <esp_heap_caps.h>

namespace {

// ESP32(PSRAMなし)では静的DRAMに収まらないので、最初のcodecArena()呼び出し時にヒープから
// 1回だけ確保する(以降は解放しない)。呼び出しはHelix/Opusのbegin()で、ヒープが一番空いている
// Wi-Fi接続前に行われる。
// Helix の AACInitDecoderPre() は内部で8バイト整列を前提に切り出すので、先頭も8に揃える。
// Opus の opus_decoder_init() も同様(構造体先頭の整列要件)。
uint8_t* s_arena = nullptr;

}  // namespace

uint8_t* codecArena() {
  if (s_arena == nullptr) {
    s_arena = static_cast<uint8_t*>(heap_caps_aligned_alloc(8, config::kCodecArenaBytes, MALLOC_CAP_8BIT));
  }
  return s_arena;
}
