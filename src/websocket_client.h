#pragma once

#include <WiFiClientSecure.h>

#include <cstddef>
#include <cstdint>

// 最小限の WebSocket クライアント(RFC 6455、wss のみ)。JCBA サイマルの受信用。
// 外部ライブラリを入れるほどの機能は要らない: 送るのはトークン1フレームだけ、受けるのは
// サーバからのバイナリフレーム(Ogg ページ)で、必要なのはハンドシェイク・フレームヘッダ
// 解析・ping への pong・close の応答だけ。
//
// TLS 接続は呼び出し側(radio_stream.cpp)の共有 WiFiClientSecure を借りる(PSRAM 非搭載機
// では TLS セッションを2つ同時に持てないため、HLS 用と同じ1本を使い回す)。
// フレームのペイロードは丸ごとバッファせず、beginFrame() でヘッダだけ読んだあと
// readPayload() で必要なぶんずつ取り出す(呼び出し側が Ogg ページを流し読みできるように)。
class WebSocketClient {
 public:
  struct FrameInfo {
    uint64_t length;  // ペイロード長
    bool binary;      // opcode 2(binary)なら true、1(text)なら false
    bool fin;
  };

  // client は接続済みでなくてよい(内部で stop → connect する)。subprotocol は
  // Sec-WebSocket-Protocol に載せ、サーバの応答が 101 でなければ失敗。
  bool connect(WiFiClientSecure& client, const char* host, uint16_t port, const char* path, const char* subprotocol,
               uint32_t timeoutMs);

  // テキストフレームを1つ送る(クライアント→サーバなのでマスクを付ける)。
  bool sendText(const char* text, size_t len);

  // 次のデータフレーム(text/binary/continuation)のヘッダを読む。ping/pong/close は
  // ここで処理して呼び出し側には見せない(ping には pong を返す、close なら -1)。
  //   1  … データフレームのヘッダを読めた(info に長さ等)。続けて readPayload() で取り出す。
  //   0  … waitMs 以内にフレームが始まらなかった(データ無し。エラーではない)。
  //  -1  … 切断またはプロトコルエラー。
  // 前のフレームのペイロードが残っていれば読み捨ててから次へ進む。
  int beginFrame(uint32_t waitMs, FrameInfo* info);

  // 現在のフレームの残りペイロードから最大 len バイト読む。戻り値は読めたバイト数
  // (要求どおり読めなければ -1: タイムアウト/切断)。残りが無ければ 0。
  int readPayload(uint8_t* buf, size_t len, uint32_t timeoutMs);

  // 現在のフレームの残りペイロードを読み捨てる。
  bool skipPayload(uint32_t timeoutMs);

  uint64_t payloadRemaining() const { return remaining_; }

  // close フレームを送ってから TCP を切る(送れなくても切る)。
  void close();

  bool connected() const;

 private:
  bool readExact(uint8_t* buf, size_t n, uint32_t timeoutMs);
  bool writeFrame(uint8_t opcode, const uint8_t* payload, size_t len);
  bool readHeaderLine(char* line, size_t cap, uint32_t timeoutMs);

  WiFiClientSecure* client_ = nullptr;
  uint64_t remaining_ = 0;  // 現在のフレームの未読ペイロード
  bool masked_ = false;     // サーバ→クライアントは通常マスク無しだが、念のため対応
  uint8_t maskKey_[4] = {0, 0, 0, 0};
  uint64_t maskPos_ = 0;
};
