#include "aes_decryptor.h"

#include <cstring>

AesCbcStreamDecryptor::AesCbcStreamDecryptor() { mbedtls_aes_init(&ctx_); }

AesCbcStreamDecryptor::~AesCbcStreamDecryptor() { mbedtls_aes_free(&ctx_); }

void AesCbcStreamDecryptor::begin(const uint8_t key[16], uint64_t sequenceNumber) {
  mbedtls_aes_setkey_dec(&ctx_, key, 128);

  memset(iv_, 0, sizeof(iv_));
  for (int i = 0; i < 8; ++i) {
    iv_[15 - i] = static_cast<uint8_t>((sequenceNumber >> (8 * i)) & 0xFF);
  }
}

void AesCbcStreamDecryptor::update(uint8_t* buf, size_t len) {
  mbedtls_aes_crypt_cbc(&ctx_, MBEDTLS_AES_DECRYPT, len, iv_, buf, buf);
}
