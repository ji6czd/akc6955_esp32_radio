// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_WS
#define LOG_MODULE_TAG "WS"
#include "websocket_client.h"

#include <Arduino.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>

#include <cstring>

#include "log_config.h"

namespace {

constexpr uint8_t kOpContinuation = 0x0;
constexpr uint8_t kOpText = 0x1;
constexpr uint8_t kOpBinary = 0x2;
constexpr uint8_t kOpClose = 0x8;
constexpr uint8_t kOpPing = 0x9;
constexpr uint8_t kOpPong = 0xA;

// RFC 6455 で定められた固定 GUID。Sec-WebSocket-Accept の検証に使う。
constexpr char kAcceptGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// 大文字小文字を無視した前方一致(HTTP ヘッダ名用)。
bool startsWithNoCase(const char* s, const char* prefix) {
  while (*prefix) {
    if (tolower(static_cast<unsigned char>(*s)) != tolower(static_cast<unsigned char>(*prefix))) {
      return false;
    }
    ++s;
    ++prefix;
  }
  return true;
}

// 行頭・行末の空白を除いた値部分を返す(ヘッダ行 "Name: value" の value)。
const char* headerValue(const char* line) {
  const char* p = strchr(line, ':');
  if (p == nullptr) {
    return "";
  }
  ++p;
  while (*p == ' ' || *p == '\t') {
    ++p;
  }
  return p;
}

}  // namespace

bool WebSocketClient::connected() const { return client_ != nullptr && client_->connected(); }

bool WebSocketClient::readExact(uint8_t* buf, size_t n, uint32_t timeoutMs) {
  size_t got = 0;
  uint32_t lastByteMs = millis();
  while (got < n) {
    int avail = client_->available();
    if (avail > 0) {
      int r = client_->read(buf + got, n - got);
      if (r > 0) {
        got += static_cast<size_t>(r);
        lastByteMs = millis();
        continue;
      }
    }
    if (!client_->connected() && client_->available() == 0) {
      return false;
    }
    if (millis() - lastByteMs > timeoutMs) {
      return false;
    }
    delay(2);
  }
  return true;
}

bool WebSocketClient::readHeaderLine(char* line, size_t cap, uint32_t timeoutMs) {
  // CRLF まで1バイトずつ読む(ハンドシェイク応答は数百バイトなので効率は問わない)。
  // cap を超えた分は捨てて、行の切れ目だけ守る。
  size_t len = 0;
  uint32_t start = millis();
  for (;;) {
    uint8_t c;
    if (!readExact(&c, 1, timeoutMs)) {
      return false;
    }
    if (c == '\n') {
      break;
    }
    if (c != '\r' && len + 1 < cap) {
      line[len++] = static_cast<char>(c);
    }
    if (millis() - start > timeoutMs) {
      return false;
    }
  }
  line[len] = '\0';
  return true;
}

bool WebSocketClient::connect(WiFiClientSecure& client, const char* host, uint16_t port, const char* path,
                              const char* subprotocol, uint32_t timeoutMs) {
  client_ = &client;
  remaining_ = 0;
  masked_ = false;

  // 共有クライアントは直前まで別ホスト(select_stream API)に繋がっていることが多い。
  client.stop();
  if (!client.connect(host, port, static_cast<int32_t>(timeoutMs))) {
    LOGE("tcp/tls connect to %s:%u failed", host, static_cast<unsigned>(port));
    return false;
  }

  // Sec-WebSocket-Key: ランダム16バイトの base64(24文字)。
  uint8_t keyRaw[16];
  for (size_t i = 0; i < sizeof(keyRaw); i += 4) {
    uint32_t r = esp_random();
    memcpy(keyRaw + i, &r, 4);
  }
  unsigned char keyB64[32];
  size_t keyLen = 0;
  mbedtls_base64_encode(keyB64, sizeof(keyB64), &keyLen, keyRaw, sizeof(keyRaw));
  keyB64[keyLen] = '\0';

  // 期待する Sec-WebSocket-Accept = base64(sha1(key + GUID))。
  unsigned char acceptSrc[24 + sizeof(kAcceptGuid)];
  memcpy(acceptSrc, keyB64, keyLen);
  memcpy(acceptSrc + keyLen, kAcceptGuid, sizeof(kAcceptGuid) - 1);
  unsigned char digest[20];
  mbedtls_sha1(acceptSrc, keyLen + sizeof(kAcceptGuid) - 1, digest);
  unsigned char expectAccept[32];
  size_t acceptLen = 0;
  mbedtls_base64_encode(expectAccept, sizeof(expectAccept), &acceptLen, digest, sizeof(digest));
  expectAccept[acceptLen] = '\0';

  char req[512];
  int n = snprintf(req, sizeof(req),
                   "GET %s HTTP/1.1\r\n"
                   "Host: %s\r\n"
                   "Upgrade: websocket\r\n"
                   "Connection: Upgrade\r\n"
                   "Sec-WebSocket-Key: %s\r\n"
                   "Sec-WebSocket-Version: 13\r\n"
                   "Sec-WebSocket-Protocol: %s\r\n"
                   "\r\n",
                   path, host, reinterpret_cast<const char*>(keyB64), subprotocol);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(req)) {
    LOGE("handshake request too long");
    client.stop();
    return false;
  }
  if (client.write(reinterpret_cast<const uint8_t*>(req), static_cast<size_t>(n)) != static_cast<size_t>(n)) {
    LOGE("handshake write failed");
    client.stop();
    return false;
  }

  // 応答: ステータス行 + ヘッダ群 + 空行。
  char line[256];
  if (!readHeaderLine(line, sizeof(line), timeoutMs)) {
    LOGE("no handshake response");
    client.stop();
    return false;
  }
  if (strncmp(line, "HTTP/1.1 101", 12) != 0) {
    LOGE("handshake rejected: %s", line);
    client.stop();
    return false;
  }
  bool acceptOk = false;
  bool upgradeOk = false;
  bool protocolOk = false;
  for (;;) {
    if (!readHeaderLine(line, sizeof(line), timeoutMs)) {
      LOGE("handshake headers truncated");
      client.stop();
      return false;
    }
    if (line[0] == '\0') {
      break;
    }
    if (startsWithNoCase(line, "Sec-WebSocket-Accept:")) {
      acceptOk = strcmp(headerValue(line), reinterpret_cast<const char*>(expectAccept)) == 0;
    } else if (startsWithNoCase(line, "Upgrade:")) {
      upgradeOk = startsWithNoCase(headerValue(line), "websocket");
    } else if (startsWithNoCase(line, "Sec-WebSocket-Protocol:")) {
      protocolOk = strcmp(headerValue(line), subprotocol) == 0;
    }
  }
  if (!acceptOk || !upgradeOk) {
    LOGE("handshake invalid (accept=%d upgrade=%d)", acceptOk, upgradeOk);
    client.stop();
    return false;
  }
  if (!protocolOk) {
    // サーバがサブプロトコルを返さない場合も RFC 上はあり得るが、JCBA は必ず返す。
    // 合わなければ以降のフレームの意味が違う可能性があるので警告だけ出して続ける。
    LOGW("server did not echo subprotocol %s", subprotocol);
  }
  LOGI("connected to wss://%s:%u%s", host, static_cast<unsigned>(port), path);
  return true;
}

bool WebSocketClient::writeFrame(uint8_t opcode, const uint8_t* payload, size_t len) {
  if (client_ == nullptr || !client_->connected()) {
    return false;
  }
  // クライアント→サーバは必ずマスクする(RFC 6455 5.3)。ヘッダは最大 2+8+4 バイト。
  uint8_t hdr[14];
  size_t h = 0;
  hdr[h++] = static_cast<uint8_t>(0x80 | (opcode & 0x0F));  // FIN=1
  if (len < 126) {
    hdr[h++] = static_cast<uint8_t>(0x80 | len);
  } else if (len < 65536) {
    hdr[h++] = 0x80 | 126;
    hdr[h++] = static_cast<uint8_t>(len >> 8);
    hdr[h++] = static_cast<uint8_t>(len);
  } else {
    hdr[h++] = 0x80 | 127;
    for (int i = 7; i >= 0; --i) {
      hdr[h++] = static_cast<uint8_t>(static_cast<uint64_t>(len) >> (8 * i));
    }
  }
  uint32_t maskWord = esp_random();
  uint8_t mask[4];
  memcpy(mask, &maskWord, 4);
  memcpy(hdr + h, mask, 4);
  h += 4;
  if (client_->write(hdr, h) != h) {
    return false;
  }
  // マスク済みペイロードを小分けに送る(送信バッファを持たない)。
  uint8_t chunk[256];
  size_t sent = 0;
  while (sent < len) {
    size_t n = len - sent;
    if (n > sizeof(chunk)) {
      n = sizeof(chunk);
    }
    for (size_t i = 0; i < n; ++i) {
      chunk[i] = payload[sent + i] ^ mask[(sent + i) & 3];
    }
    if (client_->write(chunk, n) != n) {
      return false;
    }
    sent += n;
  }
  return true;
}

bool WebSocketClient::sendText(const char* text, size_t len) {
  return writeFrame(kOpText, reinterpret_cast<const uint8_t*>(text), len);
}

int WebSocketClient::beginFrame(uint32_t waitMs, FrameInfo* info) {
  if (client_ == nullptr) {
    return -1;
  }
  if (remaining_ > 0 && !skipPayload(2000)) {
    return -1;
  }
  for (;;) {
    // フレームの先頭2バイトが waitMs 以内に来なければ「データ無し」で戻る
    // (呼び出し側が選局要求などを見に行けるように、ここで長く塞がない)。
    // 先頭が来たら残りのヘッダは短いので固定の猶予で読み切る。
    uint8_t b[2];
    if (client_->available() < 1) {
      uint32_t start = millis();
      while (client_->available() < 1) {
        if (!client_->connected()) {
          return -1;
        }
        if (millis() - start >= waitMs) {
          return 0;
        }
        delay(2);
      }
    }
    if (!readExact(b, 2, 2000)) {
      return -1;
    }
    bool fin = (b[0] & 0x80) != 0;
    uint8_t opcode = b[0] & 0x0F;
    masked_ = (b[1] & 0x80) != 0;
    uint64_t len = b[1] & 0x7F;
    if (len == 126) {
      uint8_t ext[2];
      if (!readExact(ext, 2, 2000)) {
        return -1;
      }
      len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (len == 127) {
      uint8_t ext[8];
      if (!readExact(ext, 8, 2000)) {
        return -1;
      }
      len = 0;
      for (int i = 0; i < 8; ++i) {
        len = (len << 8) | ext[i];
      }
    }
    if (masked_) {
      if (!readExact(maskKey_, 4, 2000)) {
        return -1;
      }
      maskPos_ = 0;
    }
    remaining_ = len;

    switch (opcode) {
      case kOpText:
      case kOpBinary:
      case kOpContinuation:
        info->length = len;
        info->binary = (opcode == kOpBinary);
        info->fin = fin;
        return 1;
      case kOpPing: {
        // 制御フレームは 125 バイト以下。同じペイロードで pong を返す。
        uint8_t body[125];
        size_t n = len <= sizeof(body) ? static_cast<size_t>(len) : sizeof(body);
        if (n > 0 && readPayload(body, n, 2000) != static_cast<int>(n)) {
          return -1;
        }
        if (remaining_ > 0 && !skipPayload(2000)) {
          return -1;
        }
        LOGD("ping -> pong (%u bytes)", static_cast<unsigned>(n));
        writeFrame(kOpPong, body, n);
        continue;
      }
      case kOpPong:
        if (!skipPayload(2000)) {
          return -1;
        }
        continue;
      case kOpClose:
        LOGI("close frame from server");
        skipPayload(500);
        writeFrame(kOpClose, nullptr, 0);
        client_->stop();
        return -1;
      default:
        LOGW("unknown opcode 0x%X", opcode);
        if (!skipPayload(2000)) {
          return -1;
        }
        continue;
    }
  }
}

int WebSocketClient::readPayload(uint8_t* buf, size_t len, uint32_t timeoutMs) {
  if (remaining_ == 0) {
    return 0;
  }
  if (len > remaining_) {
    len = static_cast<size_t>(remaining_);
  }
  if (!readExact(buf, len, timeoutMs)) {
    return -1;
  }
  if (masked_) {
    for (size_t i = 0; i < len; ++i) {
      buf[i] ^= maskKey_[(maskPos_ + i) & 3];
    }
    maskPos_ += len;
  }
  remaining_ -= len;
  return static_cast<int>(len);
}

bool WebSocketClient::skipPayload(uint32_t timeoutMs) {
  uint8_t sink[256];
  while (remaining_ > 0) {
    size_t n = remaining_ < sizeof(sink) ? static_cast<size_t>(remaining_) : sizeof(sink);
    if (readPayload(sink, n, timeoutMs) < 0) {
      return false;
    }
  }
  return true;
}

void WebSocketClient::close() {
  if (client_ == nullptr) {
    return;
  }
  if (client_->connected()) {
    writeFrame(kOpClose, nullptr, 0);
    client_->stop();
  }
  remaining_ = 0;
}
