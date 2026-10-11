// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_PLS
#define LOG_MODULE_TAG "GZ"
#include "gzip_inflate.h"

#include <miniz_tinfl.h>

#include "log_config.h"

namespace {

// m5tinfl_decompressorは内部テーブル込みで約8KBあり(m_look_up[3][1024]のmz_int16が
// 大半)、fetchタスクのスタック(config::kFetchTaskStackBytes、数KB)に積むと確実に
// オーバーフローする。gzip展開はfetchタスクからしか呼ばれない(排他)ので、静的領域に
// 固定で1つだけ持つ。
m5tinfl_decompressor s_decompressor;

// gzip固定ヘッダ(10バイト)+可変フィールドをスキップし、raw deflateデータの開始位置を返す。
// 不正な形式なら0(=呼び出し側で長さチェックに失敗させる)。
size_t gzipHeaderSize(const uint8_t* in, size_t inLen) {
  if (inLen < 10 || in[0] != 0x1F || in[1] != 0x8B || in[2] != 0x08) {
    return 0;
  }
  uint8_t flags = in[3];
  size_t pos = 10;

  if (flags & 0x04) {  // FEXTRA
    if (pos + 2 > inLen) {
      return 0;
    }
    uint16_t extraLen = static_cast<uint16_t>(in[pos]) | (static_cast<uint16_t>(in[pos + 1]) << 8);
    pos += 2 + extraLen;
  }
  if (flags & 0x08) {  // FNAME(NUL終端)
    while (pos < inLen && in[pos] != 0) {
      ++pos;
    }
    ++pos;
  }
  if (flags & 0x10) {  // FCOMMENT(NUL終端)
    while (pos < inLen && in[pos] != 0) {
      ++pos;
    }
    ++pos;
  }
  if (flags & 0x02) {  // FHCRC
    pos += 2;
  }
  return (pos < inLen) ? pos : 0;
}

}  // namespace

bool gzipInflateIfNeeded(const uint8_t* in, size_t inLen, uint8_t* out, size_t outCapacity, size_t* outLen) {
  if (inLen < 2 || in[0] != 0x1F || in[1] != 0x8B) {
    return false;
  }
  size_t headerLen = gzipHeaderSize(in, inLen);
  if (headerLen == 0) {
    LOGW("malformed gzip header (%u bytes)", static_cast<unsigned>(inLen));
    *outLen = 0;
    return false;
  }

  // gzipの末尾8バイト(CRC32+ISIZE)はm5tinfl_decompress()に読ませない
  // (raw deflateストリームの終端はブロックヘッダのBFINALで検出されるので不要)。
  const uint8_t* deflateData = in + headerLen;
  size_t deflateLen = inLen - headerLen;

  // m5tinfl_decompress_mem_to_mem()は使わない: 内部で同サイズのm5tinfl_decompressorを
  // スタックに積むため、fetchタスクのスタックでオーバーフローする(上のs_decompressorの
  // コメント参照)。同じロジックを静的インスタンスで手書きする。
  m5tinfl_init(&s_decompressor);
  size_t inConsumed = deflateLen;
  size_t outProduced = outCapacity;
  m5tinfl_status status = m5tinfl_decompress(&s_decompressor, deflateData, &inConsumed, out, out, &outProduced,
                                             M5TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  if (status != M5TINFL_STATUS_DONE) {
    LOGW("m5tinfl_decompress failed (status=%d, in=%u, cap=%u)", static_cast<int>(status),
         static_cast<unsigned>(deflateLen), static_cast<unsigned>(outCapacity));
    *outLen = 0;
    return false;
  }
  *outLen = outProduced;
  return true;
}
