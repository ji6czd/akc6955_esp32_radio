#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstddef>
#include <cstdint>

// JCBA サイマルラジオ(www.jcbasimul.com)/ FMぷらぷら(fmplapla.com)の受信。両者は
// 同じ配信バックエンドで、select_stream のホストが違うだけ(JcbaService で切り替える)。
// 配信は HLS ではなく、
//   1) GET /api/select_stream?station=<id>&channel=0&quality=high&burst=<秒>
//      → {"code":200,"location":"wss://<host>:443/socket?burst=N","token":"<JWT>"}
//      (JWT の有効期限は発行から15秒。取ったらすぐ接続する)
//   2) location へ WebSocket 接続(サブプロトコル "listener.fmplapla.com" が必須)
//   3) 接続直後にテキストフレームで token を送る
//   4) 以降、バイナリフレーム = Ogg ページ1枚(先頭2枚は OpusHead / OpusTags、
//      あとは Opus 20ms パケット×16 のページ。48kHz ステレオ ≈60kbps)
// という方式(2026-09 に実測。tools ではなくプランファイル/メモリに詳細あり)。
// burst=N は接続直後にサーバが N 秒ぶんをまとめて先送りする指定で、これがそのまま
// 再生の前方バッファ(ネットワーク揺らぎの吸収代)になる。
//
// client/http は radio_stream.cpp の共有 TLS 接続をそのまま使う(radiko_client と同じ理由)。
// select_stream の HTTP と WebSocket は接続先ホストが違うので、内部で繋ぎ直す。

enum class JcbaService : uint8_t {
  kJcba,      // www.jcbasimul.com
  kFmPlapla,  // fmplapla.com
};

// select_stream → WebSocket 接続 → token 送信まで。成功すれば以降 jcbaReadPage() で受信できる。
// 既に接続中なら一度切ってから繋ぐ。
bool jcbaConnect(WiFiClientSecure& client, HTTPClient& http, JcbaService service, const char* stationId, int burstSec);

// WebSocket を閉じる(未接続なら何もしない)。HLS の局へ切り替える前にも呼ぶこと
// (共有クライアントを WebSocket ホストに繋いだままにしないため)。
void jcbaDisconnect();

bool jcbaIsConnected();

enum class JcbaReadResult {
  kOk,            // ページを1枚処理した(パケットを onPacket に渡した。ヘッダページなら0個)
  kNoData,        // waitMs 以内にフレームが始まらなかった(エラーではない)
  kDisconnected,  // 切断・プロトコルエラー。jcbaConnect() し直すこと
};

// 次のフレーム(Ogg ページ)を受け、含まれる Opus パケットを順に onPacket へ渡す。
// OpusHead/OpusTags は読み飛ばす。ページをまたぐパケットは内部で連結する。
// waitMs はフレームの先頭を待つ時間(呼び出し側が選局要求を見に行けるよう短めに)。
JcbaReadResult jcbaReadPage(uint32_t waitMs, void (*onPacket)(const uint8_t* data, size_t len));

// 接続後に処理した音声ページ数 / パケット数(診断ログ用。jcbaConnect() でゼロに戻る)。
uint32_t jcbaPageCount();
uint32_t jcbaPacketCount();
