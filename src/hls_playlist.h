#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <vector>

// HLSのプレイリスト(m3u8)のテキスト解析とURL解決のみを担当する。
// 暗号化・音声デコードなど他の関心事は含まない。
//
// client/httpは呼び出し側(radio_stream.cpp)が所有する共有インスタンスを渡す。
// PSRAM非搭載機ではTLSセッションを複数同時に保持する余裕がないため、プレイリスト/鍵/
// セグメントの取得すべてで同一の接続を使い回す必要がある(実機でOOMを確認済み)。
namespace hls {

struct Segment {
  uint64_t sequence = 0;
  String url;
  // #EXT-X-PROGRAM-DATE-TIME をエポック秒にしたもの(ミリ秒は切り捨て)。0 = タグ無し。
  // radikoのタイムフリーの再生位置(s_timefreeHeadEpoch)を、要求した seek 値や概算ではなく
  // 実際のセグメントの放送時刻から決めるために使う(seek はセグメント境界へ切り下げられ、
  // セグメント長も5秒ちょうどではないため)。
  time_t programDateTime = 0;
  // #EXTINF の長さ(ミリ秒)。0 = 取れなかった。
  uint32_t durationMs = 0;
};

struct MediaPlaylist {
  uint64_t firstSequence = 0;
  std::vector<Segment> segments;
  String keyUrl;  // 解決済みの絶対URL。空文字なら暗号化なし。
  int targetDurationSec = 0;
  // #EXT-X-ENDLIST があった(これ以上セグメントは増えない)。radikoのタイムフリーは
  // 要求した to に達するとこれが付く。
  bool endList = false;
};

// base(refが見つかったプレイリスト自身のURL)を基準に、refを絶対URLへ解決する。
// 絶対URL(http/https)・ルート相対("/...")・ディレクトリ相対の3パターンに対応する。
String resolveUrl(const String& base, const String& ref);

// マスタープレイリストを取得し、最初のバリアントのメディアプレイリストの絶対URLを返す。
// radikoAuthTokenが空でなければ X-Radiko-AuthToken ヘッダとして付与する(NHKは不要)。
// originHeaderが空でなければ Origin ヘッダとして付与し、あわせて Accept-Encoding: identity
// も送る(ListenRadioはOriginをCORSとして必須で見るうえ、圧縮を許すとプレイリスト本文が
// gzipで返ってくることがあるため)。
bool fetchMasterPlaylist(WiFiClientSecure& client, HTTPClient& http, const String& masterUrl,
                         String& outMediaPlaylistUrl, const String& radikoAuthToken = "",
                         const String& originHeader = "");

// メディアプレイリストを取得・解析する。radikoAuthToken/originHeaderの扱いはfetchMasterPlaylistと同じ。
bool fetchMediaPlaylist(WiFiClientSecure& client, HTTPClient& http, const String& mediaUrl, MediaPlaylist& out,
                        const String& radikoAuthToken = "", const String& originHeader = "");

}  // namespace hls
