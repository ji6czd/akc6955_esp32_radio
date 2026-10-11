// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_JCBA
#define LOG_MODULE_TAG "JCBA"
#include "jcba_client.h"

#include <Arduino.h>

#include <cstring>

#include "config.h"
#include "log_config.h"
#include "websocket_client.h"

namespace {

constexpr char kJcbaSelectStreamUrlPrefix[] = "https://www.jcbasimul.com/api/select_stream?channel=0&quality=high";
constexpr char kFmPlaplaSelectStreamUrlPrefix[] = "https://fmplapla.com/api/select_stream?channel=0&quality=high";
// FMぷらぷら側の select_stream は参照実装(WebRadio_Japan)がOriginヘッダ付きPOSTで
// 叩いている。JCBAはGET・Origin無しで動く(実測済み)ので、こちらはサービスごとに変える。
constexpr char kFmPlaplaOrigin[] = "https://fmplapla.com";
constexpr char kSubprotocol[] = "listener.fmplapla.com";
constexpr uint32_t kHandshakeTimeoutMs = 8000;
// フレーム途中(ヘッダを読み始めた後)のバイト待ち。サーバは 320ms ごとにページを送るので
// 途中で詰まるのは異常。
constexpr uint32_t kPayloadTimeoutMs = 3000;

// Opus の1パケット(20ms)は仕様上最大 1275 バイト。JCBA の実測は 115〜263 バイト。
// ページをまたいで連結する場合もこの中に収める。超えたらそのパケットは捨てる。
// radio_stream.cpp のレコード上限(config::kAudioRecordMaxPayload)と揃えておく。
constexpr size_t kMaxPacketBytes = config::kAudioRecordMaxPayload;

struct StreamInfo {
  char host[64];
  uint16_t port;
  char path[96];
  char token[1024];  // JWT。実測 ≈590 文字
};

WebSocketClient s_ws;
bool s_connected = false;
uint32_t s_pageCount = 0;
uint32_t s_packetCount = 0;

// ページをまたぐパケットの連結用。Ogg のラッシング値 255 は「まだ続く」の意味なので、
// 255 で終わったページの端数を次のページの先頭パケットへ繋げる。
uint8_t s_packet[kMaxPacketBytes];
size_t s_carry = 0;
bool s_dropping = false;  // kMaxPacketBytes を超えたパケットを、終わりまで読み捨て中

// JSON の "key":"value" から value を取り出す(エスケープは \/ と \" だけ戻す)。
// 本文は {"code":200,"location":"wss://...","token":"..."} の1行で、汎用パーサは要らない。
bool jsonString(const String& body, const char* key, char* out, size_t cap) {
  int k = body.indexOf(key);
  if (k < 0) {
    return false;
  }
  int q = body.indexOf('"', k + static_cast<int>(strlen(key)));
  if (q < 0) {
    return false;
  }
  size_t n = 0;
  for (int i = q + 1; i < static_cast<int>(body.length()); ++i) {
    char c = body[i];
    if (c == '"') {
      out[n] = '\0';
      return true;
    }
    if (c == '\\' && i + 1 < static_cast<int>(body.length())) {
      c = body[++i];
    }
    if (n + 1 >= cap) {
      return false;
    }
    out[n++] = c;
  }
  return false;
}

// "wss://host[:port]/path" を分解する。ws:// は使わない(TLS 前提)。
bool parseWssUrl(const char* url, StreamInfo& info) {
  const char* p = url;
  if (strncmp(p, "wss://", 6) != 0) {
    return false;
  }
  p += 6;
  const char* slash = strchr(p, '/');
  const char* hostEnd = slash != nullptr ? slash : p + strlen(p);
  const char* colon = static_cast<const char*>(memchr(p, ':', static_cast<size_t>(hostEnd - p)));
  const char* nameEnd = colon != nullptr ? colon : hostEnd;
  size_t hostLen = static_cast<size_t>(nameEnd - p);
  if (hostLen == 0 || hostLen >= sizeof(info.host)) {
    return false;
  }
  memcpy(info.host, p, hostLen);
  info.host[hostLen] = '\0';
  info.port = 443;
  if (colon != nullptr) {
    int port = atoi(colon + 1);
    if (port <= 0 || port > 65535) {
      return false;
    }
    info.port = static_cast<uint16_t>(port);
  }
  const char* path = slash != nullptr ? slash : "/";
  if (strlen(path) >= sizeof(info.path)) {
    return false;
  }
  strcpy(info.path, path);
  return true;
}

bool selectStream(WiFiClientSecure& client, HTTPClient& http, JcbaService service, const char* stationId, int burstSec,
                  StreamInfo& info) {
  const char* prefix =
      (service == JcbaService::kFmPlapla) ? kFmPlaplaSelectStreamUrlPrefix : kJcbaSelectStreamUrlPrefix;
  char url[256];
  int n = snprintf(url, sizeof(url), "%s&burst=%d&station=%s", prefix, burstSec, stationId);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(url)) {
    return false;
  }
  // 共有クライアントは WebSocket ホストに繋がったままかもしれない。HTTPClient は
  // 「接続済みなら再利用」するので、別ホスト宛の前に必ず切る。
  client.stop();
  if (!http.begin(client, url)) {
    LOGE("http.begin failed");
    return false;
  }
  int code;
  if (service == JcbaService::kFmPlapla) {
    http.addHeader("Origin", kFmPlaplaOrigin);
    code = http.POST(static_cast<uint8_t*>(nullptr), 0);
  } else {
    code = http.GET();
  }
  String body = (code == HTTP_CODE_OK) ? http.getString() : String();
  http.end();
  if (code != HTTP_CODE_OK) {
    LOGE("select_stream HTTP %d", code);
    return false;
  }
  // "code":200 以外は 404(局が無い/準備中: 再試行)か 401/403(国外など: 不可)。
  int k = body.indexOf("\"code\":");
  int apiCode = k >= 0 ? body.substring(k + 7).toInt() : -1;
  if (apiCode != 200) {
    LOGE("select_stream code=%d body=%s", apiCode, body.c_str());
    return false;
  }
  char location[192];
  if (!jsonString(body, "\"location\"", location, sizeof(location)) ||
      !jsonString(body, "\"token\"", info.token, sizeof(info.token))) {
    LOGE("select_stream: unexpected body: %s", body.c_str());
    return false;
  }
  if (!parseWssUrl(location, info)) {
    LOGE("select_stream: unexpected location: %s", location);
    return false;
  }
  LOGI("select_stream ok: %s:%u%s token=%u chars", info.host, static_cast<unsigned>(info.port), info.path,
       static_cast<unsigned>(strlen(info.token)));
  return true;
}

// 1フレーム = 通常 Ogg ページ1枚だが、複数ページが入っていても順に処理する。
JcbaReadResult parsePages(void (*onPacket)(const uint8_t*, size_t)) {
  while (s_ws.payloadRemaining() >= 27) {
    uint8_t hdr[27];
    if (s_ws.readPayload(hdr, sizeof(hdr), kPayloadTimeoutMs) != static_cast<int>(sizeof(hdr))) {
      return JcbaReadResult::kDisconnected;
    }
    if (memcmp(hdr, "OggS", 4) != 0) {
      LOGW("frame is not an Ogg page (%02X %02X %02X %02X); dropping", hdr[0], hdr[1], hdr[2], hdr[3]);
      s_carry = 0;
      s_dropping = false;
      return s_ws.skipPayload(kPayloadTimeoutMs) ? JcbaReadResult::kOk : JcbaReadResult::kDisconnected;
    }
    uint8_t nseg = hdr[26];
    uint8_t segs[255];
    if (nseg > 0 && s_ws.readPayload(segs, nseg, kPayloadTimeoutMs) != static_cast<int>(nseg)) {
      return JcbaReadResult::kDisconnected;
    }
    for (uint8_t i = 0; i < nseg; ++i) {
      size_t seg = segs[i];
      if (seg > 0) {
        if (s_dropping || s_carry + seg > kMaxPacketBytes) {
          if (!s_dropping) {
            LOGW("opus packet exceeds %u bytes; dropping", static_cast<unsigned>(kMaxPacketBytes));
            s_dropping = true;
          }
          uint8_t sink[255];
          if (s_ws.readPayload(sink, seg, kPayloadTimeoutMs) != static_cast<int>(seg)) {
            return JcbaReadResult::kDisconnected;
          }
        } else {
          if (s_ws.readPayload(s_packet + s_carry, seg, kPayloadTimeoutMs) != static_cast<int>(seg)) {
            return JcbaReadResult::kDisconnected;
          }
          s_carry += seg;
        }
      }
      if (seg < 255) {
        // ラッシング値が 255 未満 = パケットの終わり。
        if (!s_dropping && s_carry > 0) {
          bool isHeader =
              s_carry >= 8 && (memcmp(s_packet, "OpusHead", 8) == 0 || memcmp(s_packet, "OpusTags", 8) == 0);
          if (isHeader) {
            LOGI("%.8s page (%u bytes)", reinterpret_cast<const char*>(s_packet), static_cast<unsigned>(s_carry));
          } else {
            onPacket(s_packet, s_carry);
            ++s_packetCount;
          }
        }
        s_carry = 0;
        s_dropping = false;
      }
    }
    ++s_pageCount;
  }
  if (s_ws.payloadRemaining() > 0) {
    LOGW("%u trailing bytes after Ogg pages; dropping", static_cast<unsigned>(s_ws.payloadRemaining()));
    if (!s_ws.skipPayload(kPayloadTimeoutMs)) {
      return JcbaReadResult::kDisconnected;
    }
  }
  return JcbaReadResult::kOk;
}

}  // namespace

bool jcbaConnect(WiFiClientSecure& client, HTTPClient& http, JcbaService service, const char* stationId, int burstSec) {
  jcbaDisconnect();
  static StreamInfo s_info;  // token が 1KB あるのでタスクスタックに置かない
  if (!selectStream(client, http, service, stationId, burstSec, s_info)) {
    return false;
  }
  // token の有効期限は 15 秒。ここから先は間を空けない。
  if (!s_ws.connect(client, s_info.host, s_info.port, s_info.path, kSubprotocol, kHandshakeTimeoutMs)) {
    return false;
  }
  if (!s_ws.sendText(s_info.token, strlen(s_info.token))) {
    LOGE("token send failed");
    s_ws.close();
    return false;
  }
  s_connected = true;
  s_pageCount = 0;
  s_packetCount = 0;
  s_carry = 0;
  s_dropping = false;
  return true;
}

void jcbaDisconnect() {
  if (s_connected) {
    s_ws.close();
    s_connected = false;
    LOGI("disconnected (pages=%lu packets=%lu)", static_cast<unsigned long>(s_pageCount),
         static_cast<unsigned long>(s_packetCount));
  }
}

bool jcbaIsConnected() { return s_connected && s_ws.connected(); }

JcbaReadResult jcbaReadPage(uint32_t waitMs, void (*onPacket)(const uint8_t* data, size_t len)) {
  if (!s_connected) {
    return JcbaReadResult::kDisconnected;
  }
  WebSocketClient::FrameInfo fi;
  int r = s_ws.beginFrame(waitMs, &fi);
  if (r == 0) {
    return JcbaReadResult::kNoData;
  }
  if (r < 0) {
    s_connected = false;
    return JcbaReadResult::kDisconnected;
  }
  if (!fi.binary) {
    // サーバからテキストフレームは来ない想定。来ても中身は使わない。
    LOGW("text frame (%u bytes) ignored", static_cast<unsigned>(fi.length));
    if (!s_ws.skipPayload(kPayloadTimeoutMs)) {
      s_connected = false;
      return JcbaReadResult::kDisconnected;
    }
    return JcbaReadResult::kOk;
  }
  JcbaReadResult result = parsePages(onPacket);
  if (result == JcbaReadResult::kDisconnected) {
    s_connected = false;
  }
  return result;
}

uint32_t jcbaPageCount() { return s_pageCount; }
uint32_t jcbaPacketCount() { return s_packetCount; }
