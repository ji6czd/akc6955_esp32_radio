#pragma once

#include <mbedtls/aes.h>

#include <cstddef>
#include <cstdint>

// HLSのAES-128-CBC(EXT-X-KEY: METHOD=AES-128)をストリーム的に復号する。
// IVはRFC 8216のデフォルト規則(セグメントの絶対メディアシーケンス番号をbig-endian
// 16バイトにしたもの)に従う。
class AesCbcStreamDecryptor {
 public:
  AesCbcStreamDecryptor();
  ~AesCbcStreamDecryptor();

  // keyはちょうど16バイト。sequenceNumberはこのセグメントの絶対メディアシーケンス番号。
  void begin(const uint8_t key[16], uint64_t sequenceNumber);

  // その場(in-place)復号。lenは16の倍数であること。
  // セグメント内で複数チャンクに分けて呼び続けても、CBCの連鎖(IV更新)は自動的に正しく繋がる。
  void update(uint8_t* buf, size_t len);

 private:
  mbedtls_aes_context ctx_;
  uint8_t iv_[16];
};
